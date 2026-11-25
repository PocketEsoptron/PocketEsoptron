#include "finds.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../files.hpp"
#include "../game_tables.hpp"
#include "../helpers.hpp"
#include "../item_catalog.hpp"
#include "../game_server.hpp"
#include "../marker_index.hpp"
#include "../regrow.hpp"
#include "../settings.hpp"
#include "floors.hpp"
#include "scene.hpp"

namespace Finds {
    // the game's reason for an object taken
    static constexpr uint32_t DONE_REASON = 2;

    template <typename List, typename If>
    static void eraseIf(List& list, If test) {
        list.erase(std::remove_if(list.begin(), list.end(), test), list.end());
    }

    template <typename If>
    static void eraseIf(std::unordered_set<int32_t>& set, If test) {
        for (auto it = set.begin(); it != set.end();)
            it = test(*it) ? set.erase(it) : std::next(it);
    }

    Finder::Finder(Links::Links& links) : m_links(links) {}
    Finder::~Finder() = default;

    void Finder::SetCatalog(std::shared_ptr<const ItemCatalog::Catalog> catalog) {
        m_catalog = std::move(catalog);
        m_index = nullptr;
        if (m_catalog) {
            std::shared_ptr<const MarkerIndex::Index> current = MarkerIndex::Current();
            m_index = current && &current->Catalog() == m_catalog.get() ? current : std::make_shared<const MarkerIndex::Index>(m_catalog);
        }
        m_types = m_catalog ? Links::TypesOf(*m_catalog) : Links::Types();
        m_regrowsByName = m_catalog ? Links::RegrowsByName(*m_catalog) : std::map<std::string, int>();
        m_waiting.clear();
        m_puzzles = Puzzles::Tracker();
    }

    void Finder::SetDone(std::unordered_set<int32_t> done, std::vector<Growing> growing) {
        m_done = std::move(done);
        m_givenObjects.clear();
        m_givenPoints.clear();
        for (Growing& g : growing) {
            if (g.at <= 0)
                continue;
            if (!g.group) {
                if (!g.points.empty())
                    m_givenPoints.push_back({ g.at, std::move(g.points) });
                continue;
            }
            Back& back = m_givenObjects[{ g.group, g.config }];
            back.at = std::max(back.at, g.at);
            back.points.insert(back.points.end(), g.points.begin(), g.points.end());
        }
        // made again on the next Settle, which knows the time
        m_nextBack = INT64_MIN;
    }

    // what's passed over, from what's found, given and waiting to grow back at now
    void Finder::remake(int64_t now) {
        m_taken = m_done;
        m_taken.insert(m_found.begin(), m_found.end());
        m_growingTaken.clear();
        for (auto& [point, at] : m_backPoints)
            m_growingTaken.insert(point);
        m_nextBack = INT64_MAX;
        for (auto it = m_givenObjects.begin(); it != m_givenObjects.end();) {
            if (it->second.at <= now) {
                it = m_givenObjects.erase(it);
                continue;
            }
            m_growingTaken.insert(it->second.points.begin(), it->second.points.end());
            m_nextBack = std::min(m_nextBack, it->second.at);
            ++it;
        }
        eraseIf(m_givenPoints, [now](const Back& b) { return b.at <= now; });
        for (const Back& b : m_givenPoints) {
            m_growingTaken.insert(b.points.begin(), b.points.end());
            m_nextBack = std::min(m_nextBack, b.at);
        }
    }

    void Finder::SetServer(const GameServer::Server* server) {
        m_server = server;
    }

    void Finder::Feed(const World::Event& event, const World::World& world, std::vector<Found>& out) {
        Settle(event.ms);
        if (!m_catalog)
            return;
        std::vector<Puzzles::Solved> done;
        m_puzzles.Feed(event, world, m_catalog->game, done);
        solved(done, out);
        switch (event.what) {
        case World::What::Appeared:
            m_links.See(event.object, *m_index, m_catalog->game, m_types);
            wantFloor(event.object, event.ms);
            seen(event.object, event.ms, out);
            dropped(event.object, event.ms);
            break;
        case World::What::Opened: {
            Found found;
            if (event.known && Links::RouteOf(event.object.gadget, m_catalog->game) == Links::Route::Opened
                && find(How::Opened, event.object, event.ms, world, found))
                out.push_back(std::move(found));
            break;
        }
        case World::What::Left:
            if (event.reason == DONE_REASON)
                gone(event, world, out);
            break;
        case World::What::GotItem:
            got(event, world, out);
            break;
        case World::What::Interacted:
            m_interacted = event.object.entity;
            m_interactedMs = event.ms;
            break;
        default:
            break;
        }
    }

    void Finder::SetPlayerFloor(int map, int group, int floor) {
        m_playerMap = 0;
        m_playerFloor = Floors::Of();
        if (!map || !m_index)
            return;
        if (!group) {
            m_playerFloor = { true, MarkerIndex::Floor::Over() };
        } else if (m_index->HasFloor(uint16_t(group), uint16_t(floor))) {
            m_playerFloor = { true, MarkerIndex::Floor::Under(uint16_t(group), uint16_t(floor)) };
        } else {
            return;
        }
        m_playerMap = map;
    }

    Floors::Of Finder::floorOf(uint32_t group, const std::unordered_set<int>& types, int map, double x, double y,
                               double reach) const {
        Floors::Of floor = Floors::At(group, *m_index, types, map, x, y, reach);
        if (!floor.known && map == m_playerMap)
            return m_playerFloor;
        return floor;
    }

    void Finder::wantFloor(const World::Object& object, uint64_t ms) {
        int type = m_links.TypeOf(object.gadget);
        int map = Links::MapOf(object.group);
        Scene::MapPos at;
        if (!type || !object.hasPos || !map || !Scene::ToMap(object.pos, map, at))
            return;
        double reach = std::max(2 * PICKUP_RADIUS, PICKUP_RADIUS + Links::MARGIN);
        if (!Floors::At(object.group, *m_index, typesFor(type), map, at.x, at.y, reach).known)
            m_floorWanted = std::max(m_floorWanted, ms + FLOOR_WANTED);
    }

    void Finder::Settle(uint64_t ms) {
        while (!m_got.empty() && m_got.front().ms + SAME_MOMENT < ms)
            m_got.pop_front();
        while (!m_waiting.empty() && m_waiting.front().ms + SAME_MOMENT < ms)
            m_waiting.pop_front();
        while (!m_drops.empty() && m_drops.front().point.at + DROP_LIES < ms)
            m_drops.pop_front();
        // what grew back can be found again
        int64_t now = int64_t(ms / 1000);
        bool changed = now >= m_nextBack;
        for (auto it = m_backPoints.begin(); it != m_backPoints.end();) {
            if (it->second > now) {
                ++it;
                continue;
            }
            m_found.erase(it->first);
            it = m_backPoints.erase(it);
            changed = true;
        }
        for (auto it = m_backObjects.begin(); it != m_backObjects.end();) {
            if (it->second.at > now) {
                ++it;
                continue;
            }
            m_foundObjects.erase(it->first);
            it = m_backObjects.erase(it);
        }
        if (changed)
            remake(now);
    }

    // An object waiting to grow back that the World sees again is back already:
    // the game doesn't send a picked object before then. It can be found again.
    void Finder::seen(const World::Object& object, uint64_t ms, std::vector<Found>& out) {
        if (!object.group)
            return;
        const Key key = { object.group, object.config };
        int64_t now = int64_t(ms / 1000);
        auto own = m_backObjects.find(key);
        auto given = m_givenObjects.find(key);
        bool ownWaits = own != m_backObjects.end() && own->second.at > now;
        bool givenWaits = given != m_givenObjects.end() && given->second.at > now;
        if (!ownWaits && !givenWaits)
            return;
        Found found;
        found.how = How::Back;
        found.ms = ms;
        found.gadget = object.gadget;
        found.group = object.group;
        found.config = object.config;
        found.type = m_links.TypeOf(object.gadget);
        int map = Links::MapOf(object.group);
        Scene::MapPos at;
        if (object.hasPos && map && Scene::ToMap(object.pos, map, at)) {
            found.map = map;
            found.x = at.x;
            found.y = at.y;
        }
        if (own != m_backObjects.end()) {
            for (int32_t point : own->second.points) {
                m_found.erase(point);
                m_backPoints.erase(point);
            }
            m_backObjects.erase(own);
        }
        m_foundObjects.erase(key);
        if (given != m_givenObjects.end())
            m_givenObjects.erase(given);
        remake(now);
        out.push_back(std::move(found));
    }

    void Finder::Finish(const World::World& world, std::vector<Found>& out) {
        if (!m_catalog)
            return;
        std::vector<Puzzles::Solved> done;
        m_puzzles.Settle(UINT64_MAX, world, m_catalog->game, done);
        solved(done, out);
    }

    // The one map item of the puzzle's type clearly nearest to one of its parts,
    // the parts tried in turn, each on its floor. Not sure when no part has one.
    void Finder::solved(const std::vector<Puzzles::Solved>& solved, std::vector<Found>& out) {
        for (const Puzzles::Solved& s : solved) {
            Found found;
            found.how = How::Solved;
            found.ms = s.ms;
            found.group = s.group;
            found.type = s.type;
            const std::unordered_set<int> types = { s.type };
            for (const World::Object& part : s.at) {
                int map = Links::MapOf(part.group);
                Scene::MapPos at;
                if (!part.hasPos || !map || !Scene::ToMap(part.pos, map, at))
                    continue;
                Floors::Of floor = floorOf(part.group, types, map, at.x, at.y,
                    std::max(2 * PUZZLE_RADIUS, PUZZLE_RADIUS + Links::MARGIN));
                MarkerIndex::Near near = m_index->UniqueNear(types, map, at.x, at.y, PUZZLE_RADIUS, Links::MARGIN,
                    floor.floor, &m_taken);
                if (!near.nearest)
                    continue;
                if (!found.map) {
                    found.gadget = part.gadget;
                    found.config = part.config;
                    found.map = map;
                    found.x = at.x;
                    found.y = at.y;
                }
                found.maybeNone = found.maybeNone || !floor.known;
                if (near.unique && !floor.known) {
                    if (std::find(found.candidates.begin(), found.candidates.end(), near.nearest->id) == found.candidates.end())
                        found.candidates.push_back(near.nearest->id);
                } else if (near.unique) {
                    found.gadget = part.gadget;
                    found.config = part.config;
                    found.map = map;
                    found.x = at.x;
                    found.y = at.y;
                    found.point = near.nearest->id;
                    found.candidates.clear();
                    found.maybeNone = false;
                    break;
                }
                for (const ItemCatalog::Point* p : near.close)
                    if (std::find(found.candidates.begin(), found.candidates.end(), p->id) == found.candidates.end())
                        found.candidates.push_back(p->id);
            }
            if (!found.map)
                continue;
            if (found.point) {
                m_found.insert(found.point);
                m_taken.insert(found.point);
            }
            out.push_back(std::move(found));
        }
    }

    static bool sameMoment(uint64_t a, uint64_t b) {
        return (a > b ? a - b : b - a) <= SAME_MOMENT;
    }

    // the item got that the pickup's gather point gives, 0 when none is
    uint32_t Finder::itsItem(const Gone& gone, const Got& got) const {
        for (const GameTables::Gather& gather : m_catalog->game.gathers(gone.object.gadget))
            for (const auto& [item, count] : got.items)
                if (item == gather.item)
                    return item;
        // a drop gives what the rock really holds, which its row may not say
        if (gone.drop)
            for (const auto& [item, count] : got.items)
                if (itemType(item))
                    return item;
        return 0;
    }

    int Finder::itemType(uint32_t item) const {
        return m_catalog ? Links::ItemType(item, m_catalog->game, m_regrowsByName) : 0;
    }

    // An object without a group that shows up near a gather point as it goes is
    // what it left on the ground, the point's item comes with it. One hit can
    // break a few rocks at once, a drop is the nearest one's
    void Finder::dropped(const World::Object& object, uint64_t ms) {
        if (object.group || !object.hasPos)
            return;
        const Gone* from = nullptr;
        double best = 0;
        for (const Gone& g : m_waiting) {
            if (g.at != g.ms || g.ms > ms || !sameMoment(g.ms, ms) || !g.object.hasPos)
                continue;
            double d = std::hypot(g.object.pos.x - object.pos.x, g.object.pos.z - object.pos.z);
            if (d <= PICKUP_RADIUS && (!from || d < best)) {
                from = &g;
                best = d;
            }
        }
        if (!from)
            return;
        m_drops.push_back({ object.entity, *from });
        if (m_drops.size() > KEEP_DROPS)
            m_drops.pop_front();
    }

    void Finder::gone(const World::Event& event, const World::World& world, std::vector<Found>& out) {
        Gone g = { event.object, event.ms, event.ms };
        // a drop picked up stands for its gather point
        auto drop = std::find_if(m_drops.begin(), m_drops.end(), [&](const Drop& d) { return d.entity == event.object.entity; });
        if (drop != m_drops.end()) {
            g = drop->point;
            g.ms = event.ms;
            g.drop = true;
            m_drops.erase(drop);
        }
        Links::Route route = Links::RouteOf(g.object.gadget, m_catalog->game);
        Found found;
        // a viewpoint goes as it's used, nothing comes with it
        if (route == Links::Route::Used) {
            bool used = m_interacted == g.object.entity && m_interactedMs <= g.ms && g.ms - m_interactedMs <= USED_AFTER;
            if (used && find(How::Used, g.object, g.ms, world, found))
                out.push_back(std::move(found));
            return;
        }
        if (route != Links::Route::PickedUp && route != Links::Route::Regrows)
            return;
        for (const Got& got : m_got)
            if (uint32_t item = sameMoment(got.ms, g.ms) ? itsItem(g, got) : 0) {
                if (find(How::PickedUp, g.object, g.at, world, found, item))
                    out.push_back(std::move(found));
                return;
            }
        m_waiting.push_back(std::move(g));
    }

    void Finder::got(const World::Event& event, const World::World& world, std::vector<Found>& out) {
        Got got = { event.ms, event.items };
        for (auto it = m_waiting.begin(); it != m_waiting.end();) {
            uint32_t item = sameMoment(it->ms, got.ms) ? itsItem(*it, got) : 0;
            if (!item) {
                ++it;
                continue;
            }
            Found found;
            if (find(How::PickedUp, it->object, it->at, world, found, item))
                out.push_back(std::move(found));
            it = m_waiting.erase(it);
        }
        m_got.push_back(std::move(got));
    }

    // the gadget's type, and for a chest the puzzle chests too
    std::unordered_set<int> Finder::typesFor(int type) const {
        std::unordered_set<int> types = { type };
        if (m_types.chests.count(type) || m_types.puzzleChests.count(type))
            types.insert(m_types.puzzleChests.begin(), m_types.puzzleChests.end());
        return types;
    }

    // The one map item of the gadget's types at the object on its floor, not done
    // yet. Not sure when two fit, when another object of those types the World has
    // sits about as near to it, or when its floor isn't known (then any floor's).
    bool Finder::find(How how, const World::Object& object, uint64_t ms, const World::World& world, Found& out, uint32_t item) {
        // the item got tells what it was before the gadget's link does
        int type = itemType(item);
        if (!type)
            type = m_links.TypeOf(object.gadget);
        int map = Links::MapOf(object.group);
        Scene::MapPos at;
        if (!type || !object.hasPos || !map || !Scene::ToMap(object.pos, map, at)
            || m_foundObjects.count({ object.group, object.config }))
            return false;
        // a thing that grows back by the server's clock waits for a server
        Regrow::Rule regrow = item ? Regrow::Of(item, m_catalog->game) : Regrow::Rule();
        int64_t backAt = Regrow::BackAt(regrow, int64_t(ms / 1000), m_server);
        if (regrow.grows() && !backAt)
            return false;
        std::unordered_set<int> types = typesFor(type);
        double radius = how == How::PickedUp && regrow.grows() ? PICKUP_RADIUS : Links::RADIUS;
        Floors::Of floor = floorOf(object.group, types, map, at.x, at.y,
            std::max(2 * radius, radius + Links::MARGIN));
        MarkerIndex::Near near = m_index->UniqueNear(types, map, at.x, at.y, radius, Links::MARGIN,
            floor.floor, regrow.grows() ? &m_growingTaken : &m_taken);
        if (!near.nearest)
            return false;
        out = Found();
        out.how = how;
        out.ms = ms;
        out.gadget = object.gadget;
        out.group = object.group;
        out.config = object.config;
        out.map = map;
        out.x = at.x;
        out.y = at.y;
        out.type = near.nearest->type;
        out.item = item;
        out.backAt = backAt;
        out.byServer = regrow.needsServer();
        m_foundObjects.insert({ object.group, object.config });
        if (backAt)
            m_backObjects[{ object.group, object.config }] = { backAt, {} };

        const ItemCatalog::Point& point = *near.nearest;
        double clear = std::max(2 * near.distance, near.distance + Links::MARGIN);
        for (const auto& [entity, other] : world.Objects()) {
            if (entity == object.entity || (other.group == object.group && other.config == object.config) || !other.hasPos
                || m_foundObjects.count({ other.group, other.config }) || !types.count(m_links.TypeOf(other.gadget))
                || Links::MapOf(other.group) != map)
                continue;
            Scene::MapPos there;
            if (Scene::ToMap(other.pos, map, there) && std::hypot(there.x - point.x, there.y - point.y) < clear
                && std::find(out.rivals.begin(), out.rivals.end(), std::make_pair(other.group, other.config)) == out.rivals.end())
                out.rivals.push_back({ other.group, other.config });
        }
        out.maybeNone = !floor.known;
        if (near.unique && out.rivals.empty() && floor.known) {
            out.point = point.id;
            m_found.insert(point.id);
            m_taken.insert(point.id);
            if (backAt) {
                m_backPoints[point.id] = backAt;
                m_growingTaken.insert(point.id);
                m_backObjects[{ object.group, object.config }].points.push_back(point.id);
            }
            return true;
        }
        if (near.unique)
            out.candidates.push_back(point.id);
        for (const ItemCatalog::Point* p : near.close)
            out.candidates.push_back(p->id);
        return true;
    }

    static const char* FILE_NAME = "learned-links.json";

    Links::Known KnownLinks(const ItemCatalog::Catalog* catalog) {
        Links::Known known;
        if (catalog && !catalog->game.links.empty() && !Links::KnownFromJson(catalog->game.links, known))
            known.clear();
        for (auto& [gadget, type] : Links::Shipped())
            known.insert({ gadget, type });
        if (catalog)
            for (auto& [gadget, type] : Links::ChestsByName(catalog->game))
                known.insert({ gadget, type });
        if (catalog)
            for (auto& [gadget, type] : Links::Viewpoints(*catalog))
                known.insert({ gadget, type });
        if (catalog)
            for (auto& [gadget, type] : Links::GathersByName(catalog->game, *catalog))
                known.insert({ gadget, type });
        return known;
    }

    std::set<int> AutoTypes(const Links::Known& links) {
        std::set<int> out = { Puzzles::SEELIE, Puzzles::TRIAL, Puzzles::MONUMENT, Puzzles::TORCH, Puzzles::SEEDS,
            Links::BURIED_CHEST, Links::SEALED_CHEST, Links::LIGHT_REVEALED_CHEST };
        for (auto& [gadget, type] : links)
            out.insert(type);
        return out;
    }

    bool LoadVotes(const std::filesystem::path& file, Links::Links& links) {
        std::string text = Files::readText(file);
        if (text.empty() || links.FromJson(text))
            return true;
        // kept aside, so the next save doesn't lose what's in it
        std::filesystem::path broken = file;
        broken += ".broken";
        std::error_code error;
        std::filesystem::rename(file, broken, error);
        if (error) {
            std::cout << "Map item links: " << Helpers::utf8(file.filename()) << " doesn't read and can't be moved, "
                      << "starting with no votes and not saving them" << std::endl;
            return false;
        }
        std::cout << "Map item links: " << Helpers::utf8(file.filename()) << " doesn't read, kept it as "
                  << Helpers::utf8(broken.filename()) << ", starting with no votes" << std::endl;
        return true;
    }

    namespace {
        struct Running {
            Links::Links links;
            Finder finder{ links };
            // the catalog the finder has, taken on the first event
            bool followed = false;
            uint64_t catalog = 0;
            // the SetDoneFor version it has
            uint64_t done = 0;
            std::filesystem::path file;
            bool saves = true;
            bool unsaved = false;
            std::chrono::steady_clock::time_point saved;
            PlayerFloor playerFloor;
            // the player's floor, and since when it's shown
            int map = 0, group = 0, floor = 0;
            std::chrono::steady_clock::time_point floorSince;
        };
    }

    // SetDoneFor's, by extension, and what they come to
    static std::mutex doneMutex;
    static std::map<const void*, std::pair<std::unordered_set<int32_t>, std::vector<Growing>>> doneBy;
    static std::unordered_set<int32_t> doneAgreed;
    static std::vector<Growing> growingAll;
    static uint64_t doneVersion = 0;

    static void mergeDone() {
        doneAgreed.clear();
        growingAll.clear();
        doneVersion++;
        // done, and waiting to grow back, by every one of them
        std::unordered_set<int32_t> waiting;
        bool first = true;
        for (auto& [owner, given] : doneBy) {
            std::unordered_set<int32_t> theirs;
            for (const Growing& g : given.second)
                theirs.insert(g.points.begin(), g.points.end());
            if (first) {
                doneAgreed = given.first;
                waiting = std::move(theirs);
                first = false;
                continue;
            }
            eraseIf(doneAgreed, [&](int32_t id) { return !given.first.count(id); });
            eraseIf(waiting, [&](int32_t id) { return !theirs.count(id); });
        }
        for (auto& [owner, given] : doneBy)
            for (Growing g : given.second) {
                eraseIf(g.points, [&](int32_t id) { return !waiting.count(id); });
                if (g.group || !g.points.empty())
                    growingAll.push_back(std::move(g));
            }
    }

    void SetDoneFor(const void* owner, std::unordered_set<int32_t> done, std::vector<Growing> growing) {
        std::lock_guard lock(doneMutex);
        doneBy[owner] = { std::move(done), std::move(growing) };
        mergeDone();
    }

    void ForgetDone(const void* owner) {
        std::lock_guard lock(doneMutex);
        if (doneBy.erase(owner))
            mergeDone();
    }

    bool GrowingRecord::Add(const Found& found, int64_t now) {
        bool changed = false;
        // let go of the ones never unmarked
        for (auto it = m_points.begin(); it != m_points.end();) {
            if (it->second.at < now - FORGET) {
                it = m_points.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (found.how == How::Back) {
            if (!found.group)
                return changed;
            for (auto& [point, entry] : m_points)
                if (entry.objects.count({ found.group, found.config }) && entry.at > now) {
                    entry.at = now;
                    changed = true;
                }
            return changed;
        }
        if (found.backAt <= 0 || found.how == How::Solved)
            return changed;
        std::vector<int32_t> points = found.candidates;
        if (found.point)
            points = { found.point };
        for (int32_t point : points) {
            Entry& entry = m_points[point];
            // picked again after it grew back: the earlier pickups are over
            if (entry.at <= now)
                entry.objects.clear();
            entry.at = std::max(entry.at, found.backAt);
            if (found.group)
                entry.objects.insert({ found.group, found.config });
            changed = true;
        }
        return changed;
    }

    bool GrowingRecord::AllBack(const std::set<int64_t>& points, int64_t now) const {
        if (points.empty())
            return false;
        for (int64_t point : points) {
            auto it = point >= 1 && point <= INT32_MAX ? m_points.find((int32_t)point) : m_points.end();
            if (it == m_points.end() || it->second.at > now)
                return false;
        }
        return true;
    }

    bool GrowingRecord::Unmarked(const std::set<int64_t>& points) {
        bool changed = false;
        for (int64_t point : points)
            if (point >= 1 && point <= INT32_MAX)
                changed = m_points.erase((int32_t)point) > 0 || changed;
        return changed;
    }

    std::string GrowingRecord::ToJson() const {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& [point, entry] : m_points) {
            nlohmann::json objects = nlohmann::json::array();
            for (const auto& [group, config] : entry.objects)
                objects.push_back({ group, config });
            list.push_back({ { "point", point }, { "at", entry.at }, { "objects", std::move(objects) } });
        }
        return nlohmann::json({ { "points", std::move(list) } }).dump();
    }

    void GrowingRecord::FromJson(const std::string& text, int64_t now) {
        m_points.clear();
        nlohmann::json all = nlohmann::json::parse(text, nullptr, false);
        if (!all.is_object() || !all.contains("points") || !all["points"].is_array())
            return;
        for (const nlohmann::json& each : all["points"]) {
            if (!each.is_object())
                continue;
            int64_t point = each.value("point", (int64_t)0), at = each.value("at", (int64_t)0);
            if (point < 1 || point > INT32_MAX || at <= 0 || at < now - FORGET)
                continue;
            Entry entry;
            entry.at = at;
            if (each.contains("objects") && each["objects"].is_array())
                for (const nlohmann::json& object : each["objects"])
                    if (object.is_array() && object.size() == 2 && object[0].is_number_unsigned() && object[1].is_number_unsigned())
                        entry.objects.insert({ object[0].get<uint32_t>(), object[1].get<uint32_t>() });
            m_points[(int32_t)point] = std::move(entry);
        }
    }

    static const char* GROWING_FILE = "growing-finds.json";
    static std::mutex growingMutex;
    static GrowingRecord growing;
    static std::filesystem::path growingFile;

    static int64_t unixNow() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static void saveGrowing() {
        if (!growingFile.empty() && !Files::writeFile(growingFile, growing.ToJson()))
            std::cout << "Map items: can't write " << Helpers::utf8(growingFile) << std::endl;
    }

    static void recordGrowing(const std::vector<Found>& found) {
        std::lock_guard lock(growingMutex);
        int64_t now = unixNow();
        bool changed = false;
        for (const Found& f : found)
            changed = growing.Add(f, now) || changed;
        if (changed)
            saveGrowing();
    }

    bool AllGrownBack(const std::set<int64_t>& points) {
        std::lock_guard lock(growingMutex);
        return growing.AllBack(points, unixNow());
    }

    void Unmarked(const std::set<int64_t>& points) {
        std::lock_guard lock(growingMutex);
        if (growing.Unmarked(points))
            saveGrowing();
    }

    static std::mutex mutex;
    static std::shared_ptr<Running> running;
    static int worldListener = 0;

    static std::mutex listenersMutex;
    static std::vector<std::pair<int, Listener>> listeners;
    static int nextListener = 1;

    static std::mutex recentMutex;
    static std::deque<Numbered> recent;
    static uint64_t nextId = 0;

    static void keep(const std::vector<Found>& found) {
        std::lock_guard lock(recentMutex);
        // a run's first id is past any id an earlier run could have given
        if (!nextId)
            nextId = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) * 1000;
        for (const Found& f : found) {
            recent.push_back({ ++nextId, f });
            if (recent.size() > RECENT)
                recent.pop_front();
        }
    }

    std::vector<Numbered> Recent(uint64_t after) {
        std::lock_guard lock(recentMutex);
        std::vector<Numbered> out;
        for (const Numbered& n : recent)
            if (n.id > after)
                out.push_back(n);
        return out;
    }

    uint64_t LastId() {
        std::lock_guard lock(recentMutex);
        return recent.empty() ? 0 : recent.back().id;
    }

    // votes are saved at most this often, and when a link changes
    static constexpr auto SAVE_EVERY = std::chrono::minutes(1);

    // AutoTypesNow's: the running Finder's learned links, and the types made
    // again when they or the items pack change
    static std::mutex autoMutex;
    static Links::Known autoLearned;
    static uint64_t learnedVersion = 0, autoLearnedSeen = 0, autoCatalog = 0, autoVersion = 0;
    static bool autoMade = false, autoTraffic = false;
    static std::set<int> autoTypes;

    static void setLearned(const Links::Links& links) {
        std::lock_guard lock(autoMutex);
        autoLearned = links.Learned();
        learnedVersion++;
    }

    static void refreshAuto() {
        uint64_t catalog = ItemCatalog::version();
        bool traffic = Settings::readGameTraffic;
        if (autoMade && catalog == autoCatalog && learnedVersion == autoLearnedSeen && traffic == autoTraffic)
            return;
        auto pack = ItemCatalog::current();
        std::set<int> now;
        // nothing is found without the traffic or the game's tables
        if (traffic && pack && pack->game.gadgetCount()) {
            Links::Known links = autoLearned;
            for (auto& [gadget, type] : KnownLinks(pack.get()))
                links[gadget] = type;
            now = AutoTypes(links);
        }
        autoMade = true;
        autoCatalog = catalog;
        autoLearnedSeen = learnedVersion;
        autoTraffic = traffic;
        if (now != autoTypes) {
            autoTypes = std::move(now);
            autoVersion++;
        }
    }

    std::set<int> AutoTypesNow() {
        std::lock_guard lock(autoMutex);
        refreshAuto();
        return autoTypes;
    }

    uint64_t AutoTypesVersion() {
        std::lock_guard lock(autoMutex);
        refreshAuto();
        return autoVersion;
    }

    static void follow(Running& r) {
        r.followed = true;
        auto pack = ItemCatalog::current();
        r.catalog = pack ? pack->version : 0;
        r.links.SetKnown(KnownLinks(pack.get()));
        r.finder.SetCatalog(pack);
    }

    static void save(Running& r) {
        r.unsaved = false;
        r.saved = std::chrono::steady_clock::now();
        if (r.saves && !Files::writeFile(r.file, r.links.ToJson()))
            std::cout << "Map item links: can't write " << Helpers::utf8(r.file) << std::endl;
    }

    static const char* howName(How how) {
        switch (how) {
        case How::Opened: return "opened";
        case How::PickedUp: return "picked up";
        case How::Back: return "back";
        case How::Used: return "used";
        default: return "solved";
        }
    }

    // a floor shown this long is the player's, not one passed on the way
    static constexpr auto FLOOR_STEADY = std::chrono::seconds(3);

    // the player's floor, asked for only while an object whose floor isn't known is around
    static void askFloor(Running& r, uint64_t ms) {
        if (!r.playerFloor || ms > r.finder.FloorWantedUntil()) {
            if (r.map)
                r.finder.SetPlayerFloor(0, 0, 0);
            r.map = 0;
            return;
        }
        int map = 0, group = 0, floor = 0;
        if (!r.playerFloor(map, group, floor))
            map = group = floor = 0;
        auto now = std::chrono::steady_clock::now();
        if (map != r.map || group != r.group || floor != r.floor) {
            r.map = map;
            r.group = group;
            r.floor = floor;
            r.floorSince = now;
        }
        if (r.map && now - r.floorSince >= FLOOR_STEADY)
            r.finder.SetPlayerFloor(r.map, r.group, r.floor);
        else
            r.finder.SetPlayerFloor(0, 0, 0);
    }

    void Start(const std::filesystem::path& folder, PlayerFloor playerFloor) {
        std::lock_guard lock(mutex);
        if (worldListener)
            return;
        running = std::make_shared<Running>();
        running->file = folder / FILE_NAME;
        running->playerFloor = std::move(playerFloor);
        running->saves = LoadVotes(running->file, running->links);
        running->saved = std::chrono::steady_clock::now();
        setLearned(running->links);
        {
            std::lock_guard growingLock(growingMutex);
            growingFile = folder / GROWING_FILE;
            growing.FromJson(Files::readText(growingFile), unixNow());
        }
        worldListener = World::AddListener([](const World::Event& event) {
            std::vector<Found> found;
            {
                std::lock_guard lock(mutex);
                if (!running)
                    return;
                Running& r = *running;
                if (!r.followed || ItemCatalog::version() != r.catalog)
                    follow(r);
                r.finder.SetServer(GameServer::shared());
                {
                    std::lock_guard doneLock(doneMutex);
                    if (r.done != doneVersion) {
                        r.done = doneVersion;
                        r.finder.SetDone(doneAgreed, growingAll);
                    }
                }
                askFloor(r, event.ms);
                World::Visit([&](const World::World& world) { r.finder.Feed(event, world, found); });
                r.unsaved = r.links.TakeUnsaved() || r.unsaved;
                if (r.links.TakeChanged()) {
                    std::cout << "Map item links: " << r.links.Learned().size() << " learned" << std::endl;
                    setLearned(r.links);
                    save(r);
                } else if (r.unsaved && std::chrono::steady_clock::now() - r.saved >= SAVE_EVERY) {
                    save(r);
                }
            }
            if (found.empty())
                return;
            for (const Found& f : found) {
                std::cout << "Map items: " << howName(f.how) << " " << f.gadget << " at " << f.group << "/" << f.config << ", ";
                if (f.how == How::Back) {
                    std::cout << "grew back";
                } else if (f.point) {
                    std::cout << "point " << f.point;
                } else {
                    std::cout << "not sure:";
                    for (int32_t c : f.candidates)
                        std::cout << " " << c;
                }
                std::cout << " (type " << f.type << ")" << std::endl;
            }
            recordGrowing(found);
            keep(found);
            std::vector<std::pair<int, Listener>> now;
            {
                std::lock_guard lock(listenersMutex);
                now = listeners;
            }
            for (const Found& f : found)
                for (auto& [id, listener] : now)
                    listener(f);
        });
    }

    void Stop() {
        int id = 0;
        std::shared_ptr<Running> was;
        {
            std::lock_guard lock(mutex);
            id = worldListener;
            worldListener = 0;
            was = std::move(running);
        }
        if (id)
            World::RemoveListener(id);
        if (was && was->unsaved)
            save(*was);
    }

    int AddListener(Listener listener) {
        std::lock_guard lock(listenersMutex);
        int id = nextListener++;
        listeners.push_back({ id, std::move(listener) });
        return id;
    }

    void RemoveListener(int id) {
        std::lock_guard lock(listenersMutex);
        for (size_t i = 0; i < listeners.size(); i++)
            if (listeners[i].first == id) {
                listeners.erase(listeners.begin() + i);
                return;
            }
    }
}

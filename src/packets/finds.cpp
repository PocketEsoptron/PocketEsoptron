#include "finds.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include "../files.hpp"
#include "../game_tables.hpp"
#include "../helpers.hpp"
#include "../item_catalog.hpp"
#include "../marker_index.hpp"
#include "../settings.hpp"
#include "scene.hpp"

namespace Finds {
    // the game's reason for an object taken
    static constexpr uint32_t DONE_REASON = 2;

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
        m_waiting.clear();
        m_puzzles = Puzzles::Tracker();
    }

    void Finder::SetDone(std::unordered_set<int32_t> done) {
        m_taken = std::move(done);
        m_taken.insert(m_found.begin(), m_found.end());
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
        default:
            break;
        }
    }

    void Finder::Settle(uint64_t ms) {
        while (!m_got.empty() && m_got.front().ms + SAME_MOMENT < ms)
            m_got.pop_front();
        while (!m_waiting.empty() && m_waiting.front().ms + SAME_MOMENT < ms)
            m_waiting.pop_front();
    }

    void Finder::Finish(const World::World& world, std::vector<Found>& out) {
        if (!m_catalog)
            return;
        std::vector<Puzzles::Solved> done;
        m_puzzles.Settle(UINT64_MAX, world, m_catalog->game, done);
        solved(done, out);
    }

    // The one map item of the puzzle's type clearly nearest to one of its parts,
    // the parts tried in turn. Not sure when no part has one.
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
                MarkerIndex::Near near = m_index->UniqueNear(types, map, at.x, at.y, PUZZLE_RADIUS, Links::MARGIN,
                    MarkerIndex::Floor::Any(), &m_taken);
                if (!near.nearest)
                    continue;
                if (!found.map) {
                    found.gadget = part.gadget;
                    found.config = part.config;
                    found.map = map;
                    found.x = at.x;
                    found.y = at.y;
                }
                if (near.unique) {
                    found.gadget = part.gadget;
                    found.config = part.config;
                    found.map = map;
                    found.x = at.x;
                    found.y = at.y;
                    found.point = near.nearest->id;
                    found.candidates.clear();
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

    // the item the pickup's gather point gives
    bool Finder::gotItsItem(const Gone& gone, const Got& got) const {
        for (const GameTables::Gather& gather : m_catalog->game.gathers(gone.object.gadget))
            for (const auto& [item, count] : got.items)
                if (item == gather.item)
                    return true;
        return false;
    }

    void Finder::gone(const World::Event& event, const World::World& world, std::vector<Found>& out) {
        if (Links::RouteOf(event.object.gadget, m_catalog->game) != Links::Route::PickedUp)
            return;
        Found found;
        Gone g = { event.object, event.ms };
        for (const Got& got : m_got)
            if (sameMoment(got.ms, g.ms) && gotItsItem(g, got)) {
                if (find(How::PickedUp, g.object, g.ms, world, found))
                    out.push_back(std::move(found));
                return;
            }
        m_waiting.push_back(std::move(g));
    }

    void Finder::got(const World::Event& event, const World::World& world, std::vector<Found>& out) {
        Got got = { event.ms, event.items };
        for (auto it = m_waiting.begin(); it != m_waiting.end();) {
            if (!sameMoment(it->ms, got.ms) || !gotItsItem(*it, got)) {
                ++it;
                continue;
            }
            Found found;
            if (find(How::PickedUp, it->object, it->ms, world, found))
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

    // The one map item of the gadget's types at the object, not done yet. Not sure
    // when two fit, or when another object of those types the World has sits about
    // as near to it.
    bool Finder::find(How how, const World::Object& object, uint64_t ms, const World::World& world, Found& out) {
        int type = m_links.TypeOf(object.gadget);
        int map = Links::MapOf(object.group);
        Scene::MapPos at;
        if (!type || !object.hasPos || !map || !Scene::ToMap(object.pos, map, at)
            || m_foundObjects.count({ object.group, object.config }))
            return false;
        std::unordered_set<int> types = typesFor(type);
        MarkerIndex::Near near = m_index->UniqueNear(types, map, at.x, at.y, Links::RADIUS, Links::MARGIN,
            MarkerIndex::Floor::Any(), &m_taken);
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
        m_foundObjects.insert({ object.group, object.config });

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
        if (near.unique && out.rivals.empty()) {
            out.point = point.id;
            m_found.insert(point.id);
            m_taken.insert(point.id);
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
            std::filesystem::path file;
            bool saves = true;
            bool unsaved = false;
            std::chrono::steady_clock::time_point saved;
        };
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
        default: return "solved";
        }
    }

    void Start(const std::filesystem::path& folder) {
        std::lock_guard lock(mutex);
        if (worldListener)
            return;
        running = std::make_shared<Running>();
        running->file = folder / FILE_NAME;
        running->saves = LoadVotes(running->file, running->links);
        running->saved = std::chrono::steady_clock::now();
        setLearned(running->links);
        worldListener = World::AddListener([](const World::Event& event) {
            std::vector<Found> found;
            {
                std::lock_guard lock(mutex);
                if (!running)
                    return;
                Running& r = *running;
                if (!r.followed || ItemCatalog::version() != r.catalog)
                    follow(r);
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
                if (f.point) {
                    std::cout << "point " << f.point;
                } else {
                    std::cout << "not sure:";
                    for (int32_t c : f.candidates)
                        std::cout << " " << c;
                }
                std::cout << " (type " << f.type << ")" << std::endl;
            }
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

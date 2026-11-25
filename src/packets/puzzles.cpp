#include "puzzles.hpp"
#include <algorithm>
#include <string>
#include "../game_tables.hpp"

namespace Puzzles {
    // the torches and fire basins of the world's puzzles, from the gadget names
    // (FireBasin, ImmortalFire, TorchPuzzle and the regions' own), leaving out
    // enemies' torches, event domains, hide and seek, quests and Miliastra builds
    static constexpr uint32_t TORCHES[] = {
        70220031, 70310001, 70310002, 70310003, 70310011, 70310012, 70310015, 70310034, 70310147, 70310238,
        70310475, 70310479, 70320005, 70320020, 70330278, 70330335, 70330336, 70330394, 70330395, 70330411,
        70330704, 70330729, 70331421, 70331422, 70331536, 70331537, 70360338, 70900393, 73003038, 73005126,
        73054002, 73076016, 73122070,
    };
    // the elemental monuments (Tablet in the game's names), not the ones that
    // follow the player
    static constexpr uint32_t MONUMENTS[] = {
        70310300, 70310301, 70330332, 70330400, 70330401, 70330402, 70330403, 70330404, 70330405, 70330441,
        70900007, 70900008, 70900009, 70900039, 70900049, 70900050, 70900051, 70900052, 70900053, 70900054,
        70900055, 70900056, 70900057, 70900058, 70900102, 73174005,
    };
    // the time trial starts (Challengestarter), on land and under water, not the events' ones
    static constexpr uint32_t TRIAL_STARTS[] = {
        70330334, 70330343, 70330344, 70330438, 70330439, 70330440, 70330658, 70330659, 70330660, 70330735,
        70330736, 70330737, 70330763, 70330764, 70330765, 70350082, 70350083, 70350084, 70350092, 70350093,
        70350094, 70350357, 73003068,
    };
    static constexpr uint32_t SEEDS_LIST[] = { 70290526, 70320004 };
    // the Seelies HoYoLAB has under Seelie, Electro and Warming Seelies have their own types
    static constexpr uint32_t SEELIES[] = { 70710010, 70330741, 74006087 };
    // where a Seelie flies home to (Lamp_Post)
    static constexpr uint32_t COURTS[] = { 70290293, 70330331, 70710006, 70900274 };

    // states: a locked chest, a lit torch or monument (201 to 299), a won trial,
    // a Seelie home at its court
    static constexpr uint32_t LOCKED = 101, LIT = 201, WON = 202, ARRIVED = 203;
    // a long trip leaves a lot behind
    static constexpr size_t MAX_GROUPS = 20000;
    static constexpr size_t MAX_OBJECTS = 50000;
    static constexpr size_t MAX_DONE = 50000;

    template <size_t N>
    static bool isAny(uint32_t gadget, const uint32_t (&list)[N]) {
        return std::find(std::begin(list), std::end(list), gadget) != std::end(list);
    }

    static Part listed(uint32_t gadget) {
        if (isAny(gadget, TORCHES)) return Part::Torch;
        if (isAny(gadget, MONUMENTS)) return Part::Monument;
        if (isAny(gadget, TRIAL_STARTS)) return Part::TrialStart;
        if (isAny(gadget, SEEDS_LIST)) return Part::Seed;
        if (isAny(gadget, SEELIES)) return Part::Seelie;
        if (isAny(gadget, COURTS)) return Part::Court;
        return Part::None;
    }

    // the model name of each listed gadget, and the part it is
    static std::unordered_map<std::string, Part> listedNames(const GameTables::Tables& tables) {
        std::unordered_map<std::string, Part> out;
        auto add = [&](const auto& list, Part part) {
            for (uint32_t id : list) {
                GameTables::Gadget g;
                if (tables.gadget(id, g) && !g.name.empty())
                    out.emplace(std::string(g.name), part);
            }
        };
        add(TORCHES, Part::Torch);
        add(MONUMENTS, Part::Monument);
        add(TRIAL_STARTS, Part::TrialStart);
        add(SEEDS_LIST, Part::Seed);
        add(SEELIES, Part::Seelie);
        add(COURTS, Part::Court);
        return out;
    }

    static Part byName(uint32_t gadget, const GameTables::Tables& tables, const std::unordered_map<std::string, Part>& names) {
        Part part = listed(gadget);
        if (part != Part::None)
            return part;
        GameTables::Gadget g;
        if (!tables.gadget(gadget, g) || g.name.empty())
            return Part::None;
        auto it = names.find(std::string(g.name));
        return it == names.end() ? Part::None : it->second;
    }

    Part PartOf(uint32_t gadget, const GameTables::Tables& tables) {
        return byName(gadget, tables, listedNames(tables));
    }

    Part Tracker::partOf(uint32_t gadget, const GameTables::Tables& tables) {
        auto known = m_parts.find(gadget);
        if (known != m_parts.end())
            return known->second;
        if (m_parts.size() >= MAX_GROUPS)
            m_parts.clear();
        if (m_namesOf != &tables) {
            m_names = listedNames(tables);
            m_namesOf = &tables;
        }
        Part part = byName(gadget, tables, m_names);
        m_parts[gadget] = part;
        return part;
    }

    void Tracker::Clear() {
        m_groups.clear();
        m_first.clear();
        m_arrived.clear();
        m_changed.clear();
        m_opened.clear();
    }

    void Tracker::Feed(const World::Event& event, const World::World& world, const GameTables::Tables& tables,
        std::vector<Solved>& out) {
        Settle(event.ms, world, tables, out);
        const World::Object& o = event.object;
        switch (event.what) {
        case World::What::Appeared:
            // every placed object counts, a chest locked in a puzzle's group can come before its parts
            if (!o.group)
                return;
            if (m_groups.size() >= MAX_GROUPS || m_first.size() >= MAX_OBJECTS)
                Clear();
            m_groups[o.group].insert(o.entity);
            m_first.emplace(o.entity, o);
            if (partOf(o.gadget, tables) != Part::None)
                m_arrived.insert(o.group);
            break;
        case World::What::StateChanged:
            if (m_groups.count(o.group))
                m_changed.insert(o.group);
            break;
        case World::What::Opened:
            if (m_groups.count(o.group))
                m_opened.insert(o.group);
            break;
        case World::What::SceneChanged:
            finish(world, tables, out);
            Clear();
            break;
        default:
            break;
        }
    }

    void Tracker::Settle(uint64_t ms, const World::World& world, const GameTables::Tables& tables, std::vector<Solved>& out) {
        if (ms == m_ms)
            return;
        finish(world, tables, out);
        m_ms = ms;
    }

    void Tracker::finish(const World::World& world, const GameTables::Tables& tables, std::vector<Solved>& out) {
        std::unordered_set<uint32_t> groups = m_arrived;
        groups.insert(m_changed.begin(), m_changed.end());
        groups.insert(m_opened.begin(), m_opened.end());
        for (uint32_t group : groups) {
            bool opened = m_opened.count(group) > 0;
            check(group, m_changed.count(group) > 0 || opened, opened, world, tables, out);
        }
        m_arrived.clear();
        m_changed.clear();
        m_opened.clear();
    }

    // A group that only came in is looked at too, so what is done already is
    // known and never comes out later
    void Tracker::check(uint32_t group, bool changed, bool opened, const World::World& world,
        const GameTables::Tables& tables, std::vector<Solved>& out) {
        auto members = m_groups.find(group);
        if (members == m_groups.end())
            return;
        std::vector<World::Object> torches, monuments, starts, seeds, seelies;
        bool locked = false, court = false;
        for (auto it = members->second.begin(); it != members->second.end();) {
            const World::Object* now = world.Find(*it);
            if (!now) {
                // gone: a Seelie's spot still counts, it flew home
                auto first = m_first.find(*it);
                if (first != m_first.end() && partOf(first->second.gadget, tables) == Part::Seelie) {
                    seelies.push_back(first->second);
                    ++it;
                    continue;
                }
                m_first.erase(*it);
                it = members->second.erase(it);
                continue;
            }
            World::Object at = *now;
            auto first = m_first.find(*it);
            if (first != m_first.end() && first->second.hasPos) {
                at.pos = first->second.pos;
                at.hasPos = true;
            }
            if (now->hasState && now->state == LOCKED)
                locked = true;
            switch (partOf(now->gadget, tables)) {
            case Part::Torch: torches.push_back(at); break;
            case Part::Monument: monuments.push_back(at); break;
            case Part::TrialStart: starts.push_back(at); break;
            case Part::Seed: seeds.push_back(at); break;
            case Part::Seelie: seelies.push_back(at); break;
            case Part::Court: court = court || (now->hasState && now->state == ARRIVED); break;
            default: break;
            }
            ++it;
        }

        auto add = [&](int type, uint32_t config, std::vector<World::Object> at) {
            std::tuple<int, uint32_t, uint32_t> key{ type, group, config };
            if (m_done.size() + m_doneBefore.size() >= MAX_DONE) {
                m_done.clear();
                m_doneBefore.clear();
            }
            // one that came in done is never done just now
            if (!changed) {
                m_doneBefore.insert(key);
                return;
            }
            if (m_doneBefore.count(key) || !m_done.insert(key).second)
                return;
            out.push_back({ type, m_ms, group, std::move(at) });
        };
        auto lit = [](const World::Object& o) { return o.hasState && o.state >= LIT && o.state < 300; };

        if (!torches.empty() && !locked && std::all_of(torches.begin(), torches.end(), lit))
            add(TORCH, 0, torches);
        // each monument can have its own map item, and the whole puzzle one more
        if (!monuments.empty() && std::all_of(monuments.begin(), monuments.end(),
            [](const World::Object& o) { return o.hasState && (o.state == LIT || o.state == WON); })) {
            for (const World::Object& monument : monuments)
                add(MONUMENT, monument.config, { monument });
        }
        // the seeds can be done in any order, each has its own map item
        for (const World::Object& seed : seeds)
            if (seed.hasState && seed.state == LIT)
                add(SEEDS, seed.config, { seed });
        // a chest can show up during a trial and go again when the time runs out,
        // only a won start or its chest opened says it's done
        bool won = std::any_of(starts.begin(), starts.end(), [](const World::Object& o) { return o.hasState && o.state == WON; });
        if (won || (opened && !starts.empty()))
            add(TRIAL, 0, starts);
        // a Seelie done long ago is gone, only its court is left, and the map item
        // sits where the Seelie started, so only the ones seen flying home count
        if (court && !seelies.empty())
            add(SEELIE, 0, seelies);
    }
}

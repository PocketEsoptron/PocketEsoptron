#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "world.hpp"

namespace GameTables { class Tables; }

// Puzzles the player solves while the overlay listens: torch puzzles, elemental
// monuments, Tri-Yana Seeds, time trials and Seelies. All parts of one puzzle
// share a group and the game sends each change of their state, so the group
// says when the puzzle is done. One that comes in done already is left alone.

namespace Puzzles {
    // HoYoLAB's types
    inline constexpr int SEELIE = 18, TRIAL = 64, MONUMENT = 70, TORCH = 71, SEEDS = 407;

    enum class Part { None, Torch, Monument, TrialStart, Seed, Seelie, Court };

    // what part of a puzzle a gadget is: a listed one, or any other gadget with
    // the same model name as one
    extern Part PartOf(uint32_t gadget, const GameTables::Tables& tables);

    struct Solved {
        int type = 0;
        uint64_t ms = 0;
        uint32_t group = 0;
        // the parts its map item can sit at, where they were first seen
        std::vector<World::Object> at;
    };

    class Tracker {
    public:
        // Events of one moment belong together (a monument puzzle that failed
        // resets in the same moment its last monument lights), so what they
        // finish comes out once a later moment starts, or with Settle
        void Feed(const World::Event& event, const World::World& world, const GameTables::Tables& tables,
            std::vector<Solved>& out);
        void Settle(uint64_t ms, const World::World& world, const GameTables::Tables& tables, std::vector<Solved>& out);
        void Clear();

    private:
        void finish(const World::World& world, const GameTables::Tables& tables, std::vector<Solved>& out);
        void check(uint32_t group, bool changed, bool opened, const World::World& world, const GameTables::Tables& tables,
            std::vector<Solved>& out);
        Part partOf(uint32_t gadget, const GameTables::Tables& tables);

        std::unordered_map<uint32_t, Part> m_parts;
        // the listed gadgets' model names in m_namesOf
        std::unordered_map<std::string, Part> m_names;
        const GameTables::Tables* m_namesOf = nullptr;
        // the puzzle parts of each group, by entity
        std::unordered_map<uint32_t, std::set<uint64_t>> m_groups;
        // where each part was when it first showed up
        std::unordered_map<uint64_t, World::Object> m_first;
        // type, group and config of what came out, and of what came in done
        std::set<std::tuple<int, uint32_t, uint32_t>> m_done, m_doneBefore;
        // the moment being read and the groups it touched
        uint64_t m_ms = 0;
        std::unordered_set<uint32_t> m_arrived, m_changed, m_opened;
    };
}

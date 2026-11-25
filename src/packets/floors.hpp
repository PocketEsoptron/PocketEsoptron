#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "../marker_index.hpp"

// Which floor a game object is on, so a chest opened under ground never takes
// the map item over it. The game puts a place's objects in groups, and a
// group's objects nearly always sit on one floor. Where the group isn't known and
// HoYoLAB has an underground point of the object's types near, the floor
// isn't known.

namespace Floors {
    struct Of {
        bool known = false;
        // Floor::Any() while it isn't known
        MarkerIndex::Floor floor;
    };

    // the groups the build ships with their floors, by group
    extern std::vector<std::pair<uint32_t, MarkerIndex::Floor>> Shipped();
    // The floor of a group's objects, by the groups the build ships. A floor
    // the items pack has no point on isn't known (HoYoLAB gave it another id).
    extern Of OfGroup(uint32_t group, const MarkerIndex::Index& index);
    // The floor of an object of the group at (map, x, y) whose map item is one
    // of the types within reach: its group's, else over ground when no
    // underground point of the types is within reach, else not known.
    extern Of At(uint32_t group, const MarkerIndex::Index& index, const std::unordered_set<int>& types,
                 int map, double x, double y, double reach);
}

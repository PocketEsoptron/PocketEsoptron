#pragma once
#include <memory>
#include <vector>
#include "learner.hpp"

// Game positions to HoYoLAB map positions, one formula per map:
//   map x = xx * game x + xz * game z + x0
//   map y = yx * game x + yz * game z + y0
// The game's y is the height. Underground floors share Teyvat's formula; which
// floor a point is on is the caller's to say (the overlay's floor, the height).

namespace Scene {
    struct Formula {
        int map = 0;
        double xx = 0, xz = 0, x0 = 0;
        double yx = 0, yz = 0, y0 = 0;
    };

    struct MapPos {
        int map = 0;
        double x = 0, y = 0;
        // the game's height
        float height = 0;
    };

    // the ones the build knows: Teyvat (map 2)
    extern std::vector<Formula> Shipped();

    // false when there's no formula for map
    extern bool ToMap(const Learner::Vec3& game, int map, MapPos& out);
    extern bool Knows(int map);

    // replaces the shipped ones for every map formulas has
    extern void SetFormulas(const std::vector<Formula>& formulas);
}

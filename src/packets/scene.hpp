#pragma once
#include <memory>
#include <string>
#include <vector>
#include "learner.hpp"

// Game positions to HoYoLAB map positions, one formula per map:
//   map x = xx * game x + xz * game z + x0
//   map y = yx * game x + yz * game z + y0
// The game's y is the height. Underground floors share Teyvat's formula; which
// floor a point is on is the caller's to say (the overlay's floor, the height).
// The items pack's game/scenes.json replaces the shipped formula of every map
// it has.

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
    // [{map, xx, xz, x0, yx, yz, y0}] and back, false when it doesn't read
    extern std::string ToJson(const std::vector<Formula>& formulas);
    extern bool FromJson(const std::string& text, std::vector<Formula>& out);

    // false when there's no formula for map
    extern bool ToMap(const Learner::Vec3& game, int map, MapPos& out);
    extern bool Knows(int map);
}

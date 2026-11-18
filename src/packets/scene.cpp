#include "scene.hpp"
#include <map>
#include <mutex>

namespace Scene {
    using Table = std::map<int, Formula>;

    std::vector<Formula> Shipped() {
        Formula teyvat;
        teyvat.map = 2;
        teyvat.xz = -1;
        teyvat.x0 = -747.6;
        teyvat.yx = -1;
        teyvat.y0 = 2322.7;
        return { teyvat };
    }

    static std::shared_ptr<const Table> make(const std::vector<Formula>& over) {
        auto table = std::make_shared<Table>();
        for (const Formula& f : Shipped())
            (*table)[f.map] = f;
        for (const Formula& f : over)
            (*table)[f.map] = f;
        return table;
    }

    static std::mutex mutex;
    static std::shared_ptr<const Table> table = make({});

    static std::shared_ptr<const Table> now() {
        std::lock_guard lock(mutex);
        return table;
    }

    void SetFormulas(const std::vector<Formula>& formulas) {
        auto made = make(formulas);
        std::lock_guard lock(mutex);
        table = made;
    }

    bool Knows(int map) {
        return now()->count(map) > 0;
    }

    bool ToMap(const Learner::Vec3& game, int map, MapPos& out) {
        std::shared_ptr<const Table> formulas = now();
        auto f = formulas->find(map);
        if (f == formulas->end())
            return false;
        const Formula& k = f->second;
        out.map = map;
        out.x = k.xx * game.x + k.xz * game.z + k.x0;
        out.y = k.yx * game.x + k.yz * game.z + k.y0;
        out.height = game.y;
        return true;
    }
}

#include "scene.hpp"
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../item_catalog.hpp"

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

    std::string ToJson(const std::vector<Formula>& formulas) {
        nlohmann::json all = nlohmann::json::array();
        for (const Formula& f : formulas)
            all.push_back({ { "map", f.map }, { "xx", f.xx }, { "xz", f.xz }, { "x0", f.x0 }, { "yx", f.yx }, { "yz", f.yz }, { "y0", f.y0 } });
        return all.dump();
    }

    bool FromJson(const std::string& text, std::vector<Formula>& out) {
        nlohmann::json all = nlohmann::json::parse(text, nullptr, false);
        if (!all.is_array())
            return false;
        std::vector<Formula> read;
        for (const nlohmann::json& f : all) {
            if (!f.is_object() || !f.contains("map") || !f["map"].is_number_integer())
                return false;
            Formula one;
            one.map = f["map"].get<int>();
            double* parts[] = { &one.xx, &one.xz, &one.x0, &one.yx, &one.yz, &one.y0 };
            const char* names[] = { "xx", "xz", "x0", "yx", "yz", "y0" };
            for (int i = 0; i < 6; i++) {
                if (!f.contains(names[i]) || !f[names[i]].is_number())
                    return false;
                *parts[i] = f[names[i]].get<double>();
            }
            read.push_back(one);
        }
        out = std::move(read);
        return true;
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
    static std::shared_ptr<const Table> table;
    static uint64_t tableOf = 0;  // the catalog version it was made with

    static std::shared_ptr<const Table> now() {
        std::lock_guard lock(mutex);
        if (!table || tableOf != ItemCatalog::version()) {
            std::vector<Formula> released;
            auto catalog = ItemCatalog::current();
            if (catalog)
                FromJson(catalog->game.scenes, released);
            table = make(released);
            tableOf = catalog ? catalog->version : 0;
        }
        return table;
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

#pragma once
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>
#include "item_catalog.hpp"

// The catalog's points in a grid per map, to ask which point of some types
// is at a map position, and whether it is the only one that close.

namespace MarkerIndex {
    // which points count by their floor
    struct Floor {
        bool any = true;
        // 0 and 0 is over ground
        uint16_t group = 0, floor = 0;
        static Floor Any() { return {}; }
        static Floor Over() { return { false, 0, 0 }; }
        static Floor Under(uint16_t group, uint16_t floor) { return { false, group, floor }; }
    };

    struct Near {
        // nullptr when none is within the radius
        const ItemCatalog::Point* nearest = nullptr;
        double distance = 0;
        // the next one of the types, nullptr when there's none within the reach looked at
        const ItemCatalog::Point* second = nullptr;
        double secondDistance = 0;
        // nearest is set and the second is clearly further: past max(2 d, d + margin)
        bool unique = false;
    };

    class Index {
    public:
        explicit Index(std::shared_ptr<const ItemCatalog::Catalog> catalog);

        // types: the type ids that count; taken: point ids already given to something else
        Near UniqueNear(const std::unordered_set<int>& types, int map, double x, double y, double radius,
                        double margin = 5, Floor floor = Floor::Any(),
                        const std::unordered_set<int32_t>* taken = nullptr) const;

        const ItemCatalog::Catalog& Catalog() const { return *m_catalog; }
        uint64_t Version() const { return m_catalog->version; }

    private:
        static constexpr double CELL = 64;
        static uint64_t key(int map, int64_t cx, int64_t cy);

        std::shared_ptr<const ItemCatalog::Catalog> m_catalog;
        // (cell key, index into the catalog's allPoints), by key
        std::vector<std::pair<uint64_t, uint32_t>> m_cells;
    };

    // the index of the catalog in use, made again when a new catalog is in;
    // nullptr while there's none
    extern std::shared_ptr<const Index> Current();
}

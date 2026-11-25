#include "marker_index.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace MarkerIndex {
    uint64_t Index::key(int map, int64_t cx, int64_t cy) {
        return (uint64_t(uint16_t(map)) << 48) | (uint64_t(uint32_t(int32_t(cx)) & 0xFFFFFF) << 24)
             | (uint64_t(uint32_t(int32_t(cy))) & 0xFFFFFF);
    }

    Index::Index(std::shared_ptr<const ItemCatalog::Catalog> catalog) : m_catalog(std::move(catalog)) {
        const std::vector<ItemCatalog::Point>& all = m_catalog->allPoints();
        m_cells.reserve(all.size());
        for (uint32_t i = 0; i < all.size(); i++) {
            const ItemCatalog::Point& p = all[i];
            m_cells.push_back({ key(p.map, int64_t(std::floor(p.x / CELL)), int64_t(std::floor(p.y / CELL))), i });
            if (p.group)
                m_floors.insert(uint32_t(p.group) << 16 | p.floor);
        }
        std::sort(m_cells.begin(), m_cells.end());
    }

    bool Index::UnderNear(const std::unordered_set<int>& types, int map, double x, double y, double reach) const {
        const std::vector<ItemCatalog::Point>& all = m_catalog->allPoints();
        int64_t x0 = int64_t(std::floor((x - reach) / CELL)), x1 = int64_t(std::floor((x + reach) / CELL));
        int64_t y0 = int64_t(std::floor((y - reach) / CELL)), y1 = int64_t(std::floor((y + reach) / CELL));
        for (int64_t cx = x0; cx <= x1; cx++)
            for (int64_t cy = y0; cy <= y1; cy++) {
                uint64_t k = key(map, cx, cy);
                auto cell = std::lower_bound(m_cells.begin(), m_cells.end(), std::make_pair(k, uint32_t(0)));
                for (; cell != m_cells.end() && cell->first == k; ++cell) {
                    const ItemCatalog::Point& p = all[cell->second];
                    if (p.map == map && p.group && types.count(p.type) && std::hypot(p.x - x, p.y - y) <= reach)
                        return true;
                }
            }
        return false;
    }

    Near Index::UniqueNear(const std::unordered_set<int>& types, int map, double x, double y, double radius, double margin,
                           Floor floor, const std::unordered_set<int32_t>* taken) const {
        Near out;
        // a second one past this is clearly further for any nearest within the radius
        double reach = std::max(2 * radius, radius + margin);
        const std::vector<ItemCatalog::Point>& all = m_catalog->allPoints();
        int64_t x0 = int64_t(std::floor((x - reach) / CELL)), x1 = int64_t(std::floor((x + reach) / CELL));
        int64_t y0 = int64_t(std::floor((y - reach) / CELL)), y1 = int64_t(std::floor((y + reach) / CELL));
        double best = INFINITY, next = INFINITY;
        const ItemCatalog::Point *first = nullptr, *second = nullptr;
        std::vector<std::pair<double, const ItemCatalog::Point*>> within;
        for (int64_t cx = x0; cx <= x1; cx++)
            for (int64_t cy = y0; cy <= y1; cy++) {
                uint64_t k = key(map, cx, cy);
                auto cell = std::lower_bound(m_cells.begin(), m_cells.end(), std::make_pair(k, uint32_t(0)));
                for (; cell != m_cells.end() && cell->first == k; ++cell) {
                    const ItemCatalog::Point& p = all[cell->second];
                    if (p.map != map || !types.count(p.type))
                        continue;
                    if (!floor.any && (p.group != floor.group || p.floor != floor.floor))
                        continue;
                    if (taken && taken->count(p.id))
                        continue;
                    double d = std::hypot(p.x - x, p.y - y);
                    if (d > reach)
                        continue;
                    within.push_back({ d, &p });
                    if (d < best) {
                        next = best;
                        second = first;
                        best = d;
                        first = &p;
                    } else if (d < next) {
                        next = d;
                        second = &p;
                    }
                }
            }
        if (first && best <= radius) {
            out.nearest = first;
            out.distance = best;
            out.unique = !second || next > std::max(2 * best, best + margin);
            out.second = second;
            out.secondDistance = second ? next : 0;
            if (!out.unique) {
                double clear = std::max(2 * best, best + margin);
                std::sort(within.begin(), within.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first || (a.first == b.first && a.second->id < b.second->id);
                });
                for (auto& [d, p] : within)
                    if (d <= clear)
                        out.close.push_back(p);
            }
        }
        return out;
    }

    std::shared_ptr<const Index> Current() {
        static std::mutex mutex;
        static std::shared_ptr<const Index> index;
        std::shared_ptr<const ItemCatalog::Catalog> catalog = ItemCatalog::current();
        std::lock_guard lock(mutex);
        if (!catalog)
            return nullptr;
        if (!index || index->Version() != catalog->version)
            index = std::make_shared<Index>(catalog);
        return index;
    }
}

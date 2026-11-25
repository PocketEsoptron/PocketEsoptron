#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <catch2/catch_test_macros.hpp>
#include "installed_pack.hpp"
#include "item_files.hpp"
#include "marker_index.hpp"
#include "packets/floors.hpp"

// HoYoLAB's chest types (Chests and Puzzle Chest)
static const std::unordered_set<int> CHESTS = { 17, 44, 45, 46, 269, 69, 75, 879 };

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data";

    // tests/data/marker_index: HoYoLAB's chest points around the chests of the learner cuts
    std::shared_ptr<const ItemCatalog::Catalog> cut() {
        static std::shared_ptr<const ItemCatalog::Catalog> loaded = [] {
            std::string raw;
            if (!ItemFiles::pack(DATA / "marker_index", raw))
                return std::shared_ptr<ItemCatalog::Catalog>();
            return ItemCatalog::load(raw);
        }();
        REQUIRE(loaded);
        return loaded;
    }

    bool same(MarkerIndex::Floor a, MarkerIndex::Floor b) {
        return a.any == b.any && a.group == b.group && a.floor == b.floor;
    }
}

TEST_CASE("A chest of a group the build doesn't know, by the underground chest 58577, isn't sure of its floor", "[floors]") {
    // 58577 is on floor 78 of group 90, 1.25 from 55917 over ground
    MarkerIndex::Index index(cut());
    const ItemCatalog::Point* under = cut()->point(58577);
    const ItemCatalog::Point* over = cut()->point(55917);
    REQUIRE(under);
    REQUIRE(over);
    CHECK(!Floors::At(0, index, CHESTS, 2, over->x, over->y, 20).known);
    CHECK(!Floors::At(0, index, CHESTS, 2, under->x, under->y, 20).known);
    // other types don't count
    CHECK(Floors::At(0, index, { 128 }, 2, over->x, over->y, 20).known);
}

TEST_CASE("A chest of a group the build doesn't know is over ground with no underground chest within reach", "[floors]") {
    MarkerIndex::Index index(cut());
    int checked = 0;
    for (const ItemCatalog::Point& p : cut()->allPoints()) {
        if (p.group || !CHESTS.count(p.type))
            continue;
        bool underNear = false;
        for (const ItemCatalog::Point& q : cut()->allPoints())
            underNear = underNear || (q.group && q.map == p.map && CHESTS.count(q.type) && std::hypot(q.x - p.x, q.y - p.y) <= 20);
        INFO(p.id);
        Floors::Of of = Floors::At(0, index, CHESTS, p.map, p.x, p.y, 20);
        CHECK(of.known == !underNear);
        if (of.known)
            CHECK(same(of.floor, MarkerIndex::Floor::Over()));
        checked++;
    }
    CHECK(checked > 50);
}

TEST_CASE("Every underground floor the build ships a group for is one the items pack has", "[floors]") {
    auto catalog = installedPack();
    if (!catalog)
        SKIP("no items pack on this PC");
    MarkerIndex::Index index(catalog);
    for (const auto& [group, floor] : Floors::Shipped()) {
        INFO(group << " on " << floor.group << "/" << floor.floor);
        if (!floor.any && floor.group)
            CHECK(index.HasFloor(floor.group, floor.floor));
        Floors::Of of = Floors::OfGroup(group, index);
        CHECK(of.known);
        CHECK(same(of.floor, floor));
    }
}

TEST_CASE("A group the build doesn't ship isn't known by itself", "[floors]") {
    MarkerIndex::Index index(cut());
    CHECK(!Floors::OfGroup(0, index).known);
    CHECK(!Floors::OfGroup(133999999, index).known);
}

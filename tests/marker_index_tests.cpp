#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "files.hpp"
#include "item_files.hpp"
#include "marker_index.hpp"
#include "packets/learner.hpp"
#include "packets/scene.hpp"
#include "packets/world.hpp"
#include "recorded_messages.hpp"

// HoYoLAB's chest types (Chests and Puzzle Chest) and the game's chests (SceneObj_Chest_* in the gadget table)
static const std::unordered_set<int> CHEST_TYPES = { 17, 44, 45, 46, 269, 69, 75, 879 };

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

    bool isChest(uint32_t gadget) { return gadget / 1000 == 70211 || gadget == 70210063; }

    // the pack the overlay in build\Clean uses, only on the PC it was downloaded on
    std::shared_ptr<const ItemCatalog::Catalog> installedPack() {
        std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path() / "build" / "Clean" / "items" / "items.pack";
        std::string text = Files::readText(file);
        size_t line = text.find('\n');
        if (line == std::string::npos)
            return nullptr;
        nlohmann::json meta = nlohmann::json::parse(text.substr(0, line), nullptr, false);
        std::string problem;
        return ItemFiles::open(text.substr(line + 1), meta, nullptr, problem);
    }
}

TEST_CASE("Every point of the cut is the nearest at its own place", "[marker_index]") {
    MarkerIndex::Index index(cut());
    REQUIRE(cut()->pointCount() == 74);
    for (const ItemCatalog::Point& p : cut()->allPoints()) {
        INFO("point " << p.id);
        MarkerIndex::Near found = index.UniqueNear({ p.type }, p.map, p.x, p.y, 1);
        REQUIRE(found.nearest);
        CHECK(found.nearest->id == p.id);
        CHECK(found.distance == 0);
        // the same place on another map has nothing
        CHECK(!index.UniqueNear({ p.type }, p.map + 1, p.x, p.y, 1).nearest);
    }
}

TEST_CASE("The chests opened on the puzzles cut are each the only chest marker near", "[marker_index]") {
    MarkerIndex::Index index(cut());
    Learner::Known known;
    known[""] = Learner::Shipped().at("7.1.0");
    Learner::Learner learner(known);
    World::World world;
    std::vector<Learner::Event> in;
    std::vector<World::Event> out;
    std::vector<int32_t> found;
    for (const Messages::Message& m : RecordedMessages::read(DATA / "learner" / "7.1-puzzles.bin")) {
        learner.Feed(m, in);
        for (const Learner::Event& e : in)
            world.Feed(e, out);
        in.clear();
        for (const World::Event& e : out) {
            if (e.what != World::What::Opened || !e.known)
                continue;
            INFO("group " << e.object.group << " config " << e.object.config);
            Scene::MapPos at;
            REQUIRE(Scene::ToMap(e.object.pos, 2, at));
            MarkerIndex::Near near = index.UniqueNear(CHEST_TYPES, 2, at.x, at.y, 10, 5, MarkerIndex::Floor::Over());
            REQUIRE(near.nearest);
            CHECK(near.unique);
            found.push_back(near.nearest->id);
        }
        out.clear();
    }
    CHECK(found.size() == 3);
    std::sort(found.begin(), found.end());
    CHECK(std::adjacent_find(found.begin(), found.end()) == found.end());
}

TEST_CASE("An underground chest under an over ground one counts only on its floor", "[marker_index]") {
    // 58577 is on floor 78 of group 90, 1.25 from 55917 over ground
    MarkerIndex::Index index(cut());
    const ItemCatalog::Point* under = cut()->point(58577);
    const ItemCatalog::Point* over = cut()->point(55917);
    REQUIRE(under);
    REQUIRE(over);
    CHECK(under->group == 90);
    CHECK(under->floor == 78);
    MarkerIndex::Near any = index.UniqueNear(CHEST_TYPES, 2, over->x, over->y, 10);
    CHECK(any.nearest->id == over->id);
    CHECK(!any.unique);
    MarkerIndex::Near up = index.UniqueNear(CHEST_TYPES, 2, under->x, under->y, 10, 5, MarkerIndex::Floor::Over());
    CHECK(up.nearest->id == over->id);
    MarkerIndex::Near down = index.UniqueNear(CHEST_TYPES, 2, over->x, over->y, 10, 5, MarkerIndex::Floor::Under(90, 78));
    CHECK(down.nearest->id == under->id);
    CHECK(!index.UniqueNear(CHEST_TYPES, 2, over->x, over->y, 10, 5, MarkerIndex::Floor::Under(90, 79)).nearest);
}

TEST_CASE("A taken point is passed over for the next one", "[marker_index]") {
    MarkerIndex::Index index(cut());
    const ItemCatalog::Point* over = cut()->point(55917);
    REQUIRE(over);
    std::unordered_set<int32_t> taken = { over->id };
    MarkerIndex::Near next = index.UniqueNear(CHEST_TYPES, 2, over->x, over->y, 10, 5, MarkerIndex::Floor::Any(), &taken);
    REQUIRE(next.nearest);
    CHECK(next.nearest->id == 58577);
}

TEST_CASE("Chests the world saw on every recording are at HoYoLAB's chest markers", "[marker_index]") {
    std::vector<std::filesystem::path> recordings = RecordedMessages::recordings();
    auto catalog = installedPack();
    if (recordings.empty() || !catalog)
        SKIP("no recordings or no items.pack on this PC");
    MarkerIndex::Index index(catalog);
    size_t chests = 0, near = 0, unique = 0;
    std::vector<double> distances;
    for (const std::filesystem::path& recording : recordings) {
        Learner::Learner learner(Learner::Shipped());
        World::World world;
        std::vector<Learner::Event> in;
        std::vector<World::Event> out;
        std::unordered_set<uint64_t> seen;
        for (const Messages::Message& m : RecordedMessages::read(recording / "messages.bin")) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in)
                world.Feed(e, out);
            in.clear();
            for (const World::Event& e : out) {
                if (e.what != World::What::Appeared || !isChest(e.object.gadget) || !e.object.hasPos || !seen.insert(e.object.entity).second)
                    continue;
                Scene::MapPos at;
                REQUIRE(Scene::ToMap(e.object.pos, 2, at));
                chests++;
                MarkerIndex::Near found = index.UniqueNear(CHEST_TYPES, 2, at.x, at.y, 10);
                if (found.nearest) {
                    near++;
                    distances.push_back(found.distance);
                }
                if (found.unique)
                    unique++;
            }
            out.clear();
        }
    }
    std::sort(distances.begin(), distances.end());
    double median = distances.empty() ? 0 : distances[distances.size() / 2];
    WARN("chests " << chests << ", a marker within 10: " << near << ", the only one: " << unique << ", median " << median);
    CHECK(chests > 300);
    CHECK(near * 10 >= chests * 6);
    CHECK(unique * 10 >= near * 9);
}


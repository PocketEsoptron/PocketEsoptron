#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "files.hpp"
#include "game_tables.hpp"
#include "installed_pack.hpp"
#include "item_files.hpp"
#include "packets/finds.hpp"
#include "packets/learner.hpp"
#include "packets/links.hpp"
#include "packets/world.hpp"
#include "settings.hpp"
#include "recorded_messages.hpp"

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data" / "finds";

    // tests/data/finds/items: HoYoLAB's points around the cuts' objects and the game's rows for them
    std::shared_ptr<const ItemCatalog::Catalog> cut() {
        static std::shared_ptr<const ItemCatalog::Catalog> loaded = [] {
            std::string raw;
            if (!ItemFiles::pack(DATA / "items", raw))
                return std::shared_ptr<ItemCatalog::Catalog>();
            return ItemCatalog::load(raw);
        }();
        REQUIRE(loaded);
        return loaded;
    }

    // the cuts have no login, so 7.1's ids are known from the start
    Learner::Known known71() {
        Learner::Known known;
        known[""] = Learner::Shipped().at("7.1.0");
        return known;
    }

    struct Played {
        std::vector<Finds::Found> found;
        std::vector<World::Event> events;
    };

    // every find of the messages played from their start
    Played play(const std::filesystem::path& messages, std::shared_ptr<const ItemCatalog::Catalog> catalog,
        const Learner::Known& known, std::unordered_set<int32_t> done = {}) {
        Learner::Learner learner(known);
        World::World world;
        Links::Links links(Finds::KnownLinks(catalog.get()));
        Finds::Finder finder(links);
        finder.SetCatalog(catalog);
        finder.SetDone(std::move(done));
        Played played;
        std::vector<Learner::Event> in;
        std::vector<World::Event> events;
        for (const Messages::Message& m : RecordedMessages::read(messages)) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in)
                world.Feed(e, events);
            in.clear();
            for (const World::Event& e : events) {
                finder.Feed(e, world, played.found);
                played.events.push_back(e);
            }
            events.clear();
        }
        finder.Finish(world, played.found);
        return played;
    }

    std::vector<int32_t> points(const std::vector<Finds::Found>& found) {
        std::vector<int32_t> out;
        for (const Finds::Found& f : found)
            out.push_back(f.point);
        return out;
    }
}

TEST_CASE("The Hydroculus and the Dendroculus picked up on the oculi cut are found", "[finds]") {
    std::vector<Finds::Found> found = play(DATA / "7.1-oculi.bin", cut(), known71()).found;
    REQUIRE(found.size() == 2);
    CHECK(found[0].how == Finds::How::PickedUp);
    CHECK(found[0].point == 53912);
    CHECK(found[0].type == 508);
    CHECK(found[0].gadget == 70590074);
    CHECK(found[0].group == 133409366);
    CHECK(found[0].config == 366001);
    CHECK(found[0].candidates.empty());
    CHECK(found[1].how == Finds::How::PickedUp);
    CHECK(found[1].point == 29159);
    CHECK(found[1].type == 403);
    CHECK(found[1].gadget == 70590063);
}

TEST_CASE("The mora chest opened on the stone pile cut is found, the Dendro Rock broken over it isn't", "[finds]") {
    std::vector<Finds::Found> found = play(DATA / "7.1-stonepile.bin", cut(), known71()).found;
    REQUIRE(found.size() == 1);
    CHECK(found[0].how == Finds::How::Opened);
    CHECK(found[0].point == 32859);
    CHECK(found[0].type == 359);
    CHECK(found[0].gadget == 70217020);
}

TEST_CASE("A Hydroculus that was there before listening isn't found when picked up", "[finds]") {
    Played played = play(DATA / "7.1-oculi-late.bin", cut(), known71());
    // its item still comes in the cut
    bool gotIt = false;
    for (const World::Event& e : played.events)
        if (e.what == World::What::GotItem)
            for (const auto& [item, count] : e.items)
                gotIt = gotIt || item == 107023;
    CHECK(gotIt);
    CHECK(played.found.empty());
}

TEST_CASE("Map items done before are passed over", "[finds]") {
    std::vector<Finds::Found> found = play(DATA / "7.1-oculi.bin", cut(), known71(), { 53912 }).found;
    CHECK(points(found) == std::vector<int32_t>{ 29159 });
}

TEST_CASE("Nothing is found without an items pack", "[finds]") {
    CHECK(play(DATA / "7.1-oculi.bin", nullptr, known71()).found.empty());
}

TEST_CASE("The finds on every recording", "[finds]") {
    std::vector<std::filesystem::path> recordings = RecordedMessages::recordings();
    auto catalog = installedPack();
    if (recordings.empty() || !catalog || !catalog->game.gadgetCount())
        SKIP("no recordings or no items pack with the game's tables on this PC");
    // the old found list (a473787) and the puzzles, each checked on HoYoLAB, and the 2026-11-21 and
    // 2026-11-22 plays, each ticked on the map right after it was done. The 0 on 11-22 is a Wooden Mora
    // Chest with two next to it, both ticked. On 11-24 every find was ticked by Markers itself and checked
    // by the player in game, the two 0s are the Wooden Mora Chests 65410 and 65411 opened one after the other
    const std::map<std::string, std::vector<int32_t>> right = {
        { "2026-10-14_21-47", { 4831 } },
        { "2026-10-17_18-39", { 1305 } },
        { "2026-10-18_pickups2-check", { 53912, 29159 } },
        { "2026-10-18_puzzles", { 29061, 29109, 29065, 29064, 29111, 29009, 29010, 29008, 31126, 33000, 30581, 31326,
                                  29126, 36953, 42155 } },
        { "2026-10-19_puzzles", { 2828, 2829, 2795, 2816, 4739, 29189, 32803, 29190, 29342, 81509, 29142 } },
        { "2026-10-19_puzzles-live", { 72367 } },
        { "2026-10-19_puzzles-live-2", { 72279, 73946 } },
        { "2026-10-19_stonepile", { 32859 } },
        { "2026-11-21_a-s4-puzzles", { 65434, 64877, 64874, 65081, 64804, 65046, 65047, 64994, 64949, 64971, 65069 } },
        { "2026-11-22_play", { 0, 65365, 64803, 64802, 64856, 64916, 64875, 65022, 64798, 65450, 64799, 64876, 64800, 64921,
                               65435, 64920, 65502, 64868, 65436, 64969, 65048, 64898, 64805, 64970, 64806, 65367, 65368, 104473,
                               104610, 101909, 100876, 100812, 104451, 101840, 101839, 101838, 101911, 101910 } },
        { "2026-11-24_g-s2-live", { 86295, 85997, 86071, 0, 0, 64852, 64813, 100836 } },
    };
    for (const std::filesystem::path& recording : recordings) {
        auto it = right.find(recording.filename().string());
        std::vector<int32_t> expected = it == right.end() ? std::vector<int32_t>() : it->second;
        INFO(recording.filename().string());
        CHECK(points(play(recording / "messages.bin", catalog, Learner::Shipped()).found) == expected);
    }
}

TEST_CASE("A Wooden Mora Chest opened next to another gives both map items and the other chest as its rival", "[finds]") {
    std::filesystem::path recording;
    for (const std::filesystem::path& each : RecordedMessages::recordings())
        if (each.filename() == "2026-11-22_play")
            recording = each;
    auto catalog = installedPack();
    if (recording.empty() || !catalog || !catalog->game.gadgetCount())
        SKIP("no 2026-11-22_play recording or no items pack with the game's tables on this PC");
    std::vector<Finds::Found> found = play(recording / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() >= 2);
    // 67006 was opened first while 67005 stood about as near to 65365
    CHECK(found[0].point == 0);
    CHECK(found[0].type == 129);
    CHECK(found[0].group == 133114067);
    CHECK(found[0].config == 67006);
    CHECK(found[0].candidates == std::vector<int32_t>{ 65365, 65366 });
    CHECK(found[0].rivals == std::vector<std::pair<uint32_t, uint32_t>>{ { 133114067, 67005 } });
    CHECK(found[1].point == 65365);
    CHECK(found[1].config == 67005);
    CHECK(found[1].rivals.empty());
}

namespace {
    std::filesystem::path tempFolder(const std::string& name) {
        std::filesystem::path folder = std::filesystem::temp_directory_path()
            / (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        return folder;
    }

    // the finds cut with game/links.json holding text
    std::shared_ptr<const ItemCatalog::Catalog> withLinks(const std::string& text) {
        std::filesystem::path folder = tempFolder("finds-links-test");
        std::filesystem::copy(DATA / "items", folder, std::filesystem::copy_options::recursive);
        REQUIRE(Files::writeFile(folder / "game" / "links.json", text));
        std::string raw;
        bool packed = ItemFiles::pack(folder, raw);
        std::filesystem::remove_all(folder);
        REQUIRE(packed);
        auto catalog = ItemCatalog::load(raw);
        REQUIRE(catalog);
        return catalog;
    }
}

TEST_CASE("The types found by themselves are the links' types, the puzzle chests and the puzzles", "[finds]") {
    CHECK(Finds::AutoTypes({}) == std::set<int>{ 18, 64, 69, 70, 71, 75, 407, 879 });
    CHECK(Finds::AutoTypes({ { 70211001, 17 }, { 70211002, 17 }, { 73094157, 833 } })
        == std::set<int>{ 17, 18, 64, 69, 70, 71, 75, 407, 833, 879 });
    auto pack = installedPack();
    REQUIRE(pack);
    if (!ItemCatalog::current() || !ItemCatalog::current()->game.gadgetCount())
        ItemCatalog::publish(std::make_shared<ItemCatalog::Catalog>(*pack));
    REQUIRE(ItemCatalog::current()->game.gadgetCount());
    Settings::readGameTraffic = false;
    CHECK(Finds::AutoTypesNow().empty());
    uint64_t off = Finds::AutoTypesVersion();
    Settings::readGameTraffic = true;
    std::set<int> now = Finds::AutoTypesNow();
    CHECK(Finds::AutoTypesVersion() != off);
    // the shipped ones are in, Dendro Rocks and what grows back aren't
    for (int type : { 17, 44, 45, 269, 129, 359, 838, 356, 128, 5, 6, 194, 403, 508, 626, 694, 833, 141, 472 }) {
        INFO(type);
        CHECK(now.count(type));
    }
    CHECK(!now.count(415));
    uint64_t version = Finds::AutoTypesVersion();
    CHECK(Finds::AutoTypesVersion() == version);
    Settings::readGameTraffic = false;
}

TEST_CASE("The pack's links come before the shipped ones, then the chests by their names", "[finds]") {
    Links::Known known = Finds::KnownLinks(withLinks("[{\"gadget\": 70590074, \"type\": 17}, {\"gadget\": 1, \"type\": 44}]").get());
    CHECK(known.at(70590074) == 17);
    CHECK(known.at(1) == 44);
    CHECK(known.at(70590063) == 403);
    Links::Known byName = Links::ChestsByName(cut()->game);
    Links::Known shipped = Links::Shipped();
    shipped.insert(byName.begin(), byName.end());
    CHECK(known.size() == shipped.size() + 1);
    CHECK(Finds::KnownLinks(withLinks("not json").get()) == shipped);
    CHECK(Finds::KnownLinks(cut().get()) == shipped);
    CHECK(Finds::KnownLinks(nullptr) == Links::Shipped());
}

TEST_CASE("Every chest of the game's tables gets its type by name, the shipped ones agree", "[finds]") {
    auto catalog = installedPack();
    if (!catalog || !catalog->game.gadgetCount())
        SKIP("no items pack with the game's tables on this PC");
    Links::Known byName = Links::ChestsByName(catalog->game);
    // 7.2 has 118 such chests, a later version more
    CHECK(byName.size() >= 118);
    // named Lv4, they sit on Remarkable Chest markers
    const std::set<uint32_t> remarkable = { 70211150, 70211151 };
    for (auto& [gadget, type] : Links::Shipped()) {
        auto named = byName.find(gadget);
        if (named == byName.end())
            continue;
        INFO("gadget " << gadget);
        CHECK((remarkable.count(gadget) ? type == 269 : named->second == type));
    }
    // Snezhnaya's, the ones under water and on land and water, and the frozen ones are in
    CHECK(byName.at(73174100) == 44);
    CHECK(byName.at(70211199) == 46);
    CHECK(byName.at(70211189) == 45);
    CHECK(byName.at(70211104) == 75);
}

TEST_CASE("The learned file's votes come back, and one that doesn't read is kept aside", "[finds]") {
    std::filesystem::path folder = tempFolder("finds-votes-test");
    std::filesystem::path file = folder / "learned-links.json";
    Links::Links saved;
    saved.Cast(70590074, 133409366, 366001, { 508, 53912, 1.5 });
    saved.Cast(70590074, 133409180, 180003, { 508, 53900, 2.0 });
    REQUIRE(saved.TypeOf(70590074) == 508);
    REQUIRE(Files::writeFile(file, saved.ToJson()));

    Links::Links loaded;
    Finds::LoadVotes(file, loaded);
    CHECK(loaded.TypeOf(70590074) == 508);
    CHECK(loaded.ToJson() == saved.ToJson());

    Links::Links none;
    Finds::LoadVotes(folder / "missing.json", none);
    CHECK(none.Learned().empty());

    REQUIRE(Files::writeFile(file, "not json"));
    Links::Links fresh;
    Finds::LoadVotes(file, fresh);
    CHECK(fresh.Learned().empty());
    CHECK(!std::filesystem::exists(file));
    CHECK(Files::readText(folder / "learned-links.json.broken") == "not json");
    std::filesystem::remove_all(folder);
}

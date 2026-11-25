#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "files.hpp"
#include "game_server.hpp"
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
    Played play(const std::vector<Messages::Message>& messages, std::shared_ptr<const ItemCatalog::Catalog> catalog,
        const Learner::Known& known, std::unordered_set<int32_t> done = {}, const GameServer::Server* server = nullptr,
        std::vector<Finds::Growing> growing = {}) {
        Learner::Learner learner(known);
        World::World world;
        Links::Links links(Finds::KnownLinks(catalog.get()));
        Finds::Finder finder(links);
        finder.SetCatalog(catalog);
        finder.SetDone(std::move(done), std::move(growing));
        finder.SetServer(server);
        Played played;
        std::vector<Learner::Event> in;
        std::vector<World::Event> events;
        for (const Messages::Message& m : messages) {
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

    Played play(const std::filesystem::path& messages, std::shared_ptr<const ItemCatalog::Catalog> catalog,
        const Learner::Known& known, std::unordered_set<int32_t> done = {}, const GameServer::Server* server = nullptr,
        std::vector<Finds::Growing> growing = {}) {
        return play(RecordedMessages::read(messages), catalog, known, std::move(done), server, std::move(growing));
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
    // by the player in game, the two 0s are the Wooden Mora Chests 65410 and 65411 opened one after the other.
    // The finds of things that grow back were checked by where they were picked against HoYoLAB's points,
    // not in game: the Starconch 4620 (10-14), the Violetgrass 65266 (11-21), the Kalpalata Lotus 30299 and
    // two ores (10-18 pickups2), the Qingxin 945 (10-19 puzzles, 11.5 units off, the only one within 40),
    // the Rimecurrants, Snowball Flowers and Flockingweed on 11-22. Each ore is a rock among more of its
    // kind, so it waits (0); the third rock mined there wasn't picked up before the end. The plants on
    // 11-22 grow a few to a map item, so most of them wait too. On g-s3-live every find was ticked by
    // Markers and checked by the player in game: a Frostfairy Flower, three ores, then a cluster of four
    // Crystal Chunk rocks under four map items (each 0 alone, Markers ticks all four) and a Cor Lapis.
    // On g-s4-live the player mined an Iron Chunk and a Crystal Chunk rock in the Wenut Tunnels (the
    // Iron Chunk grows back by the server's clock, not found without one; the Crystal Chunk's floor
    // isn't known without the minimap, so 0), then three Condessence rocks in Fontaine under three map
    // items: their rows say Crystal Chunk, the drops gave Condessence Crystal, each 0 alone. On
    // g-s4-unmark the player mined two Condessence rocks under two map items (0 each, both ticked), then
    // looked from the Fontaine viewpoint 56253, ticked on the map by hand. On g-s5-live the player opened
    // a Wooden Mora Chest floating on the water, its map item 56167 filed on HoYoLAB's underwater floor
    // but taken as on top (ticked by hand), then looked from the viewpoint 56252
    const std::map<std::string, std::vector<int32_t>> right = {
        { "2026-10-14_21-47", { 4831, 4620 } },
        { "2026-10-17_18-39", { 1305 } },
        { "2026-10-18_pickups2-check", { 53912, 29159, 30299, 0, 0 } },
        { "2026-10-18_puzzles", { 29061, 29109, 29065, 29064, 29111, 29009, 29010, 29008, 31126, 33000, 30581, 31326,
                                  29126, 36953, 42155 } },
        { "2026-10-19_puzzles", { 2828, 2829, 945, 2795, 2816, 4739, 29189, 32803, 29190, 29342, 81509, 29142 } },
        { "2026-10-19_puzzles-live", { 72367 } },
        { "2026-10-19_puzzles-live-2", { 72279, 73946 } },
        { "2026-10-19_stonepile", { 32859 } },
        { "2026-11-21_a-s4-puzzles", { 65434, 64877, 64874, 65081, 64804, 65046, 65047, 64994, 64949, 64971, 65266, 65069 } },
        { "2026-11-22_play", { 0, 65365, 64803, 64802, 64856, 64916, 64875, 65022, 64798, 65450, 64799, 64876, 64800, 64921,
                               65435, 64920, 65502, 64868, 65436, 64969, 65048, 64898, 64805, 64970, 64806, 65367, 65368, 104473,
                               0, 0, 0, 0, 0, 0, 104610, 101909, 100876, 101692, 101693, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100812,
                               0, 0, 0, 0, 0, 104451, 0, 0, 101840, 0, 0, 101839, 101712, 101838, 0, 0, 0, 0, 0, 0, 101911, 0, 0,
                               0, 0, 101910, 0, 0, 0, 0 } },
        { "2026-11-24_g-s2-live", { 86295, 85997, 86071, 0, 0, 64852, 64813, 100836 } },
        { "2026-11-24_g-s3-live", { 101668, 5225, 1225, 664, 0, 0, 679, 0, 0 } },
        { "2026-11-25_g-s4-live", { 0, 0, 0, 0 } },
        { "2026-11-25_g-s4-unmark", { 0, 0, 56253 } },
        { "2026-11-25_g-s5-live", { 56167, 56252 } },
    };
    for (const std::filesystem::path& recording : recordings) {
        auto it = right.find(recording.filename().string());
        std::vector<int32_t> expected = it == right.end() ? std::vector<int32_t>() : it->second;
        INFO(recording.filename().string());
        CHECK(points(play(recording / "messages.bin", catalog, Learner::Shipped()).found) == expected);
    }
}

TEST_CASE("A rock whose row says Crystal Chunk is found by what its drop gave, Condessence Crystal", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-11-25_g-s4-live") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-11-25_g-s4-live recording or no items pack with ranks on this PC");
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() == 4);
    // the three rocks of group 133412143 by the lake, each with the three map items there
    for (size_t i = 1; i < 4; i++) {
        INFO(i);
        CHECK(found[i].gadget == 70520042);
        CHECK(found[i].item == 101009);
        CHECK(found[i].type == 507);
        std::vector<int32_t> candidates = found[i].candidates;
        std::sort(candidates.begin(), candidates.end());
        CHECK(candidates == std::vector<int32_t>{ 58399, 58400, 58401 });
    }
    // the Wenut Tunnels one is a plain Crystal Chunk
    CHECK(found[0].gadget == 70520003);
    CHECK(found[0].type == 16);
}

TEST_CASE("A viewpoint looked from is found as it goes, its map item the Viewpoint the player ticked", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-11-25_g-s4-unmark") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.gadgetCount())
        SKIP("no 2026-11-25_g-s4-unmark recording or no items pack with the game's tables on this PC");
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() == 3);
    // the game's ViewCodex row 70500008 is this object, in Fontaine
    const Finds::Found& viewpoint = found[2];
    CHECK(viewpoint.how == Finds::How::Used);
    CHECK(viewpoint.gadget == 70800028);
    CHECK(viewpoint.group == 133402856);
    CHECK(viewpoint.config == 856001);
    CHECK(viewpoint.type == Links::VIEWPOINT);
    CHECK(viewpoint.point == 56253);
    CHECK(viewpoint.candidates.empty());
    CHECK(viewpoint.backAt == 0);
    // done before, it's passed over
    CHECK(points(play(folder / "messages.bin", catalog, Learner::Shipped(), { 56253 }).found) == std::vector<int32_t>{ 0, 0 });
    // gone with reason 2 without the player's interact, it isn't found
    std::vector<Messages::Message> messages = RecordedMessages::read(folder / "messages.bin");
    messages.erase(std::remove_if(messages.begin(), messages.end(),
        [](const Messages::Message& m) { return m.id == 1891 && m.direction == Frames::Direction::Sent; }), messages.end());
    CHECK(points(play(messages, catalog, Learner::Shipped()).found) == std::vector<int32_t>{ 0, 0 });
}

TEST_CASE("Every ViewPoint gadget of the game's tables is used and linked to the Viewpoint", "[finds]") {
    auto catalog = installedPack();
    if (!catalog || !catalog->game.gadgetCount())
        SKIP("no items pack with the game's tables on this PC");
    Links::Known viewpoints = Links::Viewpoints(*catalog);
    // all 233 viewpoints of 7.2 use this one
    CHECK(viewpoints.count(70800028));
    for (auto& [gadget, type] : viewpoints)
        CHECK(type == Links::VIEWPOINT);
    CHECK(Links::RouteOf(70800028, catalog->game) == Links::Route::Used);
    // named like it, but a plain Gadget
    CHECK(Links::RouteOf(70800029, catalog->game) == Links::Route::None);
    CHECK(Finds::KnownLinks(catalog.get()).at(70800028) == Links::VIEWPOINT);
    CHECK(Links::TypesOf(*catalog).used == std::unordered_set<int>{ Links::VIEWPOINT });
}

TEST_CASE("Pickups that grow back sit further than RADIUS from their map item, within PICKUP_RADIUS", "[finds]") {
    auto catalog = installedPack();
    if (!catalog || !catalog->game.knowsRanks()) SKIP("no items pack with ranks on this PC");
    // a picked plant (item 100031) 11.5 from its map item, a Crystal Chunk rock 10.2 from its map item
    const std::map<std::string, std::pair<int32_t, double>> far = {
        { "2026-10-19_puzzles", { 945, 11.5 } },
        { "2026-11-24_g-s3-live", { 5225, 10.2 } },
    };
    int seen = 0;
    for (const std::filesystem::path& recording : RecordedMessages::recordings()) {
        auto it = far.find(recording.filename().string());
        if (it == far.end()) continue;
        INFO(recording.filename().string());
        bool there = false;
        for (const Finds::Found& f : play(recording / "messages.bin", catalog, Learner::Shipped()).found) {
            if (f.point != it->second.first) continue;
            const ItemCatalog::Point* p = catalog->point(f.point);
            REQUIRE(p);
            double d = std::hypot(p->x - f.x, p->y - f.y);
            CHECK(f.backAt > 0);
            CHECK(d > Links::RADIUS);
            CHECK(d < Finds::PICKUP_RADIUS);
            CHECK(std::abs(d - it->second.second) < 0.1);
            there = true;
        }
        CHECK(there);
        seen++;
    }
    if (!seen) SKIP("none of the recordings on this PC");
}

TEST_CASE("Every drop on the recordings lies well within PICKUP_RADIUS of the gather point it came from", "[finds]") {
    // the drop (an object with no group) and its gone gather point within SAME_MOMENT: on the recordings
    // of 2026-10 to 2026-11 the furthest is 7.9 from its rock (an ore chunk on 2026-11-22_play)
    double furthest = 0;
    int drops = 0;
    for (const std::filesystem::path& recording : RecordedMessages::recordings()) {
        Learner::Learner learner(Learner::Shipped());
        World::World world;
        std::vector<Learner::Event> in;
        std::vector<World::Event> events, gone;
        for (const Messages::Message& m : RecordedMessages::read(recording / "messages.bin")) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in) world.Feed(e, events);
            in.clear();
            for (const World::Event& e : events) {
                if (e.what == World::What::Left && e.reason == 2 && e.object.group && e.object.hasPos) gone.push_back(e);
                if (e.what != World::What::Appeared || e.object.group || !e.object.hasPos) continue;
                double best = -1;
                for (const World::Event& g : gone) {
                    if ((g.ms > e.ms ? g.ms - e.ms : e.ms - g.ms) > Finds::SAME_MOMENT) continue;
                    double d = std::hypot((double)g.object.pos.x - e.object.pos.x, (double)g.object.pos.z - e.object.pos.z);
                    if (best < 0 || d < best) best = d;
                }
                if (best < 0) continue;
                furthest = std::max(furthest, best);
                drops++;
            }
            events.clear();
        }
    }
    if (!drops) SKIP("no recordings with drops on this PC");
    CHECK(drops >= 44);
    CHECK(furthest < 8);
    CHECK(furthest < Finds::PICKUP_RADIUS / 1.5);
}

TEST_CASE("A local specialty picked on a recording is back 48 h after the pickup", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-10-14_21-47") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-10-14_21-47 recording or no items pack with ranks on this PC");
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() == 2);
    // the Starconch, picked at 1792007349675 ms
    CHECK(found[1].point == 4620);
    CHECK(found[1].item == 100033);
    CHECK(found[1].backAt == 1792007349 + 48 * 3600);
    // the chest doesn't grow back
    CHECK(found[0].backAt == 0);
}

namespace {
    // the 2026-10-14_21-47 recording, with its Starconch's object
    std::filesystem::path starconchRecording() {
        for (const std::filesystem::path& recording : RecordedMessages::recordings())
            if (recording.filename() == "2026-10-14_21-47") return recording / "messages.bin";
        return {};
    }

    // well after the recording
    constexpr int64_t LATER = 1792007349 + 48 * 3600;
}

TEST_CASE("A grow-back object given as waiting that the recording shows is back, and found again when picked", "[finds]") {
    std::filesystem::path messages = starconchRecording();
    auto catalog = installedPack();
    if (messages.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-10-14_21-47 recording or no items pack with ranks on this PC");
    Finds::Found starconch = play(messages, catalog, Learner::Shipped()).found.at(1);
    REQUIRE(starconch.point == 4620);
    // as if it was picked two days before and its time wasn't up yet: the game sent it, so it's back
    std::vector<Finds::Found> found = play(messages, catalog, Learner::Shipped(), { 4620 }, nullptr,
        { { starconch.group, starconch.config, { 4620 }, LATER } }).found;
    REQUIRE(found.size() == 3);
    CHECK(found[0].how == Finds::How::Back);
    CHECK(found[0].group == starconch.group);
    CHECK(found[0].config == starconch.config);
    CHECK(found[0].point == 0);
    CHECK(found[0].ms < starconch.ms);
    // the pickup after it is found again
    CHECK(found[2].point == 4620);
}

TEST_CASE("A hand tick doesn't hide a grow-back pickup, a tick waiting to grow back does", "[finds]") {
    std::filesystem::path messages = starconchRecording();
    auto catalog = installedPack();
    if (messages.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-10-14_21-47 recording or no items pack with ranks on this PC");
    // ticked with no back time: the object being there shows the tick is out of date
    std::vector<Finds::Found> found = play(messages, catalog, Learner::Shipped(), { 4620 }).found;
    REQUIRE(found.size() == 2);
    CHECK(found[1].point == 4620);
    // waiting to grow back, by its point only (an older tick with no object): passed over
    found = play(messages, catalog, Learner::Shipped(), { 4620 }, nullptr, { { 0, 0, { 4620 }, LATER } }).found;
    for (const Finds::Found& f : found)
        CHECK(f.point != 4620);
}

TEST_CASE("Cooking ingredients picked on a recording are found only with a server, back at 0:00 on its clock", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-10-18_puzzles") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-10-18_puzzles recording or no items pack with ranks on this PC");
    auto grows = [](const std::vector<Finds::Found>& found) {
        std::vector<int32_t> out;
        for (const Finds::Found& f : found)
            if (f.backAt) out.push_back(f.point);
        return out;
    };
    CHECK(grows(play(folder / "messages.bin", catalog, Learner::Shipped()).found).empty());
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped(), {}, GameServer::find("europe")).found;
    // the Starshroom, the Mint, three Sumeru Roses, two Harra Fruits and four Zaytun Peaches, each 2 to 9
    // map units from the map item, checked on HoYoLAB's points. The fruits of a kind hang under one map
    // item, each but the last can't tell it from the next one (0), the last takes it
    CHECK(grows(found) == std::vector<int32_t>{ 40321, 40272, 31662, 31661, 31660, 0, 35765, 0, 0, 0, 35769 });
    // all picked on 2026-10-18 before 23:00 UTC, back at 0:00 in Europe (UTC+1)
    for (const Finds::Found& f : found)
        if (f.backAt) CHECK(f.backAt == 1792364400);
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
    // the shipped ones and the Viewpoint are in, Dendro Rocks and what grows back aren't
    for (int type : { 17, 44, 45, 269, 129, 359, 838, 356, 128, 5, 6, 194, 403, 508, 626, 694, 833, 141, 472, 85 }) {
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

TEST_CASE("The overlay's own record says a picked Starconch grew back only once it has, and lets it go when unmarked", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-10-14_21-47") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-10-14_21-47 recording or no items pack with ranks on this PC");
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() == 2);
    const int64_t picked = 1792007349, back = picked + 48 * 3600;

    Finds::GrowingRecord record;
    for (const Finds::Found& f : found) record.Add(f, picked);
    // the chest doesn't grow back, the Starconch does
    CHECK(record.Size() == 1);
    CHECK_FALSE(record.AllBack({ 4620 }, back - 1));
    CHECK(record.AllBack({ 4620 }, back));
    // a point it never found, or the chest, isn't vouched for, and neither is a list holding one
    CHECK_FALSE(record.AllBack({ 4831 }, back));
    CHECK_FALSE(record.AllBack({ 4620, 4831 }, back));
    CHECK_FALSE(record.AllBack({}, back));

    // saved and read back the same, and let go a month after it was back
    Finds::GrowingRecord saved;
    saved.FromJson(record.ToJson(), back);
    CHECK(saved.AllBack({ 4620 }, back));
    saved.FromJson(record.ToJson(), back + Finds::GrowingRecord::FORGET + 1);
    CHECK(saved.Size() == 0);
    saved.FromJson("not json", back);
    CHECK(saved.Size() == 0);

    // its object seen again: back at once
    Finds::Found again = found[1];
    again.how = Finds::How::Back;
    again.point = 0;
    CHECK(record.Add(again, picked + 3600));
    CHECK(record.AllBack({ 4620 }, picked + 3600));

    // unmarked: nothing to vouch for any more
    CHECK(record.Unmarked({ 4620 }));
    CHECK_FALSE(record.AllBack({ 4620 }, back));
    CHECK_FALSE(record.Unmarked({ 4620 }));
}

TEST_CASE("Three rocks on the same three map items: any of them seen again makes the map items back", "[finds]") {
    std::filesystem::path folder;
    for (const std::filesystem::path& recording : RecordedMessages::recordings())
        if (recording.filename() == "2026-11-25_g-s4-live") folder = recording;
    auto catalog = installedPack();
    if (folder.empty() || !catalog || !catalog->game.knowsRanks())
        SKIP("no 2026-11-25_g-s4-live recording or no items pack with ranks on this PC");
    std::vector<Finds::Found> found = play(folder / "messages.bin", catalog, Learner::Shipped()).found;
    REQUIRE(found.size() == 4);
    for (int first = 1; first < 4; first++) {
        INFO(first);
        Finds::GrowingRecord record;
        for (size_t i = 1; i < 4; i++) record.Add(found[i], 1);
        REQUIRE(record.Size() == 3);
        const int64_t soon = 3600;
        CHECK_FALSE(record.AllBack({ 58399, 58400, 58401 }, soon));
        Finds::Found again = found[first];
        again.how = Finds::How::Back;
        again.candidates.clear();
        record.Add(again, soon);
        CHECK(record.AllBack({ 58399, 58400, 58401 }, soon));
        // and saved and read back, it still knows every rock
        Finds::GrowingRecord saved;
        for (size_t i = 1; i < 4; i++) saved.Add(found[i], 1);
        Finds::GrowingRecord read;
        read.FromJson(saved.ToJson(), 1);
        read.Add(again, soon);
        CHECK(read.AllBack({ 58399, 58400, 58401 }, soon));
    }
}

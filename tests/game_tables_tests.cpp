#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "files.hpp"
#include "game_tables.hpp"
#include "item_catalog.hpp"
#include "item_files.hpp"
#include "packets/learner.hpp"
#include "packets/learning.hpp"
#include "packets/links.hpp"
#include "packets/scene.hpp"
#include "recorded_messages.hpp"

// tests/data/game_tables: whole rows of the game's 7.2 tables, see its answers.md

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data";

    std::string table(const char* name) {
        std::string text = Files::readText(DATA / "game_tables" / name);
        REQUIRE(!text.empty());
        return text;
    }

    std::filesystem::path tempFolder(const std::string& name) {
        std::filesystem::path folder = std::filesystem::temp_directory_path()
            / (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::remove_all(folder);
        return folder;
    }

    // the marker index cut, with game/ as the builder writes it when with is true
    std::shared_ptr<ItemCatalog::Catalog> cutPack(bool with) {
        std::filesystem::path folder = tempFolder("game-tables-test");
        std::filesystem::copy(DATA / "marker_index", folder, std::filesystem::copy_options::recursive);
        if (with) {
            REQUIRE(Files::writeFile(folder / "game" / "gadgets.json", ItemFiles::trimGameTable(GameTables::GADGETS, table("GadgetExcelConfigData.json"))));
            REQUIRE(Files::writeFile(folder / "game" / "gathers.json", ItemFiles::trimGameTable(GameTables::GATHERS, table("GatherExcelConfigData.json"))));
            REQUIRE(Files::writeFile(folder / "game" / "materials.json", ItemFiles::trimGameTable(GameTables::MATERIALS, table("MaterialExcelConfigData.json"))));
            REQUIRE(Files::writeFile(folder / "game" / "learned-messages.json", Learner::ToJson(Learner::Shipped())));
            REQUIRE(Files::writeFile(folder / "game" / "scenes.json", Scene::ToJson(Scene::Shipped())));
            REQUIRE(Files::writeFile(folder / "game" / "links.json", Links::KnownToJson(Links::Shipped())));
        }
        std::string raw;
        bool packed = ItemFiles::pack(folder, raw);
        std::filesystem::remove_all(folder);
        REQUIRE(packed);
        auto catalog = ItemCatalog::load(raw);
        REQUIRE(catalog);
        return catalog;
    }
}

TEST_CASE("The game's tables are cut to the fields game/ keeps", "[game_tables]") {
    nlohmann::json gadgets = nlohmann::json::parse(ItemFiles::trimGameTable(GameTables::GADGETS, table("GadgetExcelConfigData.json")));
    REQUIRE(gadgets.size() == 3);
    CHECK(gadgets[0] == nlohmann::json({ { "id", 40200002 }, { "type", "AirflowField" } }));
    CHECK(gadgets[1] == nlohmann::json({ { "id", 70211101 }, { "type", "Chest" }, { "jsonName", "SceneObj_Chest_Default_Lv1" } }));
    nlohmann::json gathers = nlohmann::json::parse(ItemFiles::trimGameTable(GameTables::GATHERS, table("GatherExcelConfigData.json")));
    REQUIRE(gathers.size() == 6);
    CHECK(gathers[0] == nlohmann::json({ { "gadgetId", 70540003 }, { "itemId", 100001 }, { "pointType", 2006 }, { "refreshId", 999900 } }));
    CHECK(gathers[3] == nlohmann::json({ { "gadgetId", 70590074 }, { "itemId", 107023 }, { "pointType", 4016 } }));
    nlohmann::json materials = nlohmann::json::parse(ItemFiles::trimGameTable(GameTables::MATERIALS, table("MaterialExcelConfigData.json")));
    REQUIRE(materials.size() == 7);
    CHECK(materials[0] == nlohmann::json({ { "id", 100001 }, { "itemType", "ITEM_MATERIAL" }, { "materialType", "MATERIAL_NOTICE_ADD_HP" }, { "rank", 90000 } }));
    CHECK(ItemFiles::trimGameTable(GameTables::GADGETS, "not json").empty());
    CHECK(ItemFiles::trimGameTable("game/other.json", table("GadgetExcelConfigData.json")).empty());
}

TEST_CASE("The items gather points give and the ones that grow back get their names from the text maps", "[game_tables]") {
    std::string materials = table("MaterialExcelConfigData.json");
    std::string trimmed = ItemFiles::trimGameTable(GameTables::MATERIALS, materials);
    std::vector<std::string> maps = { table("TextMapEN.json"), table("TextMap_MediumEN.json") };
    nlohmann::json named = nlohmann::json::parse(ItemFiles::nameGrowingItems(trimmed, materials, table("GatherExcelConfigData.json"), maps));
    REQUIRE(named.size() == 7);
    // no gather point gives the Magical Crystal Chunk (the outcrops do) or the Condessence Crystal
    // (Fontaine's rocks are rows of Crystal Chunk, their drop gives it), both grow back
    const char* names[] = { "Apple", "Iron Chunk", "Crystal Chunk", "Quenepa Berry", "Hydroculus", "Magical Crystal Chunk",
                            "Condessence Crystal" };
    for (size_t i = 0; i < 7; i++)
        CHECK(named[i].value("name", "") == names[i]);
    CHECK(named[6]["id"] == 101009);
    // a text map that doesn't read leaves the table as it was
    CHECK(ItemFiles::nameGrowingItems(trimmed, materials, table("GatherExcelConfigData.json"), { "not json" }) == trimmed);
}

TEST_CASE("A pack with game/ gives the tables, the ids and the formulas", "[game_tables]") {
    auto catalog = cutPack(true);
    CHECK(catalog->pointCount() == 74);
    const GameTables::Tables& game = catalog->game;
    CHECK(game.gadgetCount() == 3);
    GameTables::Gadget gadget;
    REQUIRE(game.gadget(70211101, gadget));
    CHECK(gadget.type == "Chest");
    CHECK(gadget.name == "SceneObj_Chest_Default_Lv1");
    REQUIRE(game.gadget(40200002, gadget));
    CHECK(gadget.type == "AirflowField");
    CHECK(gadget.name.empty());
    CHECK(!game.gadget(70211102, gadget));

    std::vector<GameTables::Gather> gathers = game.gathers(70540003);
    REQUIRE(gathers.size() == 2);
    CHECK(gathers[0].item == 100001);
    CHECK(gathers[0].pointType == 2006);
    CHECK(gathers[1].pointType == 3001);
    CHECK(gathers[0].refresh == 999900);
    CHECK(game.gathers(70211101).empty());
    CHECK(game.knowsRanks());
    // a Hydroculus is picked up once
    std::vector<GameTables::Gather> hydroculus = game.gathers(70590074);
    REQUIRE(hydroculus.size() == 1);
    CHECK(hydroculus[0].item == 107023);
    CHECK(hydroculus[0].refresh == 0);

    GameTables::Material material;
    REQUIRE(game.material(gathers[0].item, material));
    CHECK(material.itemType == "ITEM_MATERIAL");
    CHECK(material.materialType == "MATERIAL_NOTICE_ADD_HP");
    CHECK(material.rank == 90000);
    REQUIRE(game.material(101001, material));
    CHECK(material.materialType == "MATERIAL_EXCHANGE");

    Learner::Known learned;
    REQUIRE(Learner::FromJson(game.learned, learned));
    CHECK(learned == Learner::Shipped());
    CHECK(Learning::Released(catalog.get()) == Learner::Shipped());
    std::vector<Scene::Formula> formulas;
    REQUIRE(Scene::FromJson(game.scenes, formulas));
    REQUIRE(formulas.size() == 1);
    CHECK(formulas[0].map == 2);
    CHECK(formulas[0].x0 == Scene::Shipped()[0].x0);
    CHECK(formulas[0].y0 == Scene::Shipped()[0].y0);
    Links::Known links;
    REQUIRE(Links::KnownFromJson(game.links, links));
    CHECK(links == Links::Shipped());
    WARN("game tables in memory: " << game.bytes() << " bytes");
}

TEST_CASE("A pack without game/ loads with empty tables and no released ids", "[game_tables]") {
    auto catalog = cutPack(false);
    CHECK(catalog->pointCount() == 74);
    CHECK(catalog->game.gadgetCount() == 0);
    CHECK(catalog->game.scenes.empty());
    CHECK(catalog->game.learned.empty());
    CHECK(catalog->game.links.empty());
    CHECK(Learning::Released(catalog.get()).empty());
    CHECK(Learning::Released(nullptr).empty());
}

TEST_CASE("Released ids win over the shipped ones, and both over the learned file", "[game_tables]") {
    std::filesystem::path folder = tempFolder("released-test");
    std::filesystem::path file = folder / "learned-messages.json";
    Learner::Known saved, released;
    saved["7.2.0"][Learner::Kind::ObjectsNear] = { 100, { 2, 2, 4, 13, 1, 2, 3 } };
    saved["7.3.0"][Learner::Kind::ObjectsNear] = { 101, { 2, 2, 4, 13, 1, 2, 3 } };
    saved["7.3.0"][Learner::Kind::PlayerMoved] = { 102, { 1, 2 } };
    released["7.3.0"][Learner::Kind::ObjectsNear] = { 103, { 2, 2, 4, 13, 1, 2, 3 } };
    released["7.2.0"][Learner::Kind::PlayerMoved] = { 104, { 1, 2 } };
    REQUIRE(Learning::Save(file, saved));
    Learner::Known loaded = Learning::Load(file, released);
    std::filesystem::remove_all(folder);
    CHECK(loaded.at("7.1.0") == Learner::Shipped().at("7.1.0"));
    CHECK(loaded.at("7.2.0").at(Learner::Kind::ObjectsNear) == Learner::Shipped().at("7.2.0").at(Learner::Kind::ObjectsNear));
    CHECK(loaded.at("7.2.0").at(Learner::Kind::PlayerMoved).id == 104);
    CHECK(loaded.at("7.3.0").at(Learner::Kind::ObjectsNear).id == 103);
    CHECK(loaded.at("7.3.0").at(Learner::Kind::PlayerMoved).id == 102);
}

TEST_CASE("A learner given the released ids while running reads as one that had them", "[game_tables]") {
    std::vector<Messages::Message> login = RecordedMessages::read(DATA / "learner" / "7.2-login.bin");
    std::vector<Messages::Message> walk = RecordedMessages::read(DATA / "learner" / "7.2-walk.bin");
    REQUIRE(!login.empty());
    REQUIRE(!walk.empty());
    auto run = [&](Learner::Learner& learner, bool releaseAfterLogin) {
        std::vector<Learner::Event> events;
        for (const Messages::Message& m : login)
            learner.Feed(m, events);
        if (releaseAfterLogin) {
            CHECK(!learner.IsLearned(Learner::Kind::ObjectsNear));
            learner.Release(Learner::Shipped(), events);
            CHECK(learner.TakeChanged());
        }
        for (const Messages::Message& m : walk)
            learner.Feed(m, events);
        return events;
    };
    Learner::Learner had(Learner::Shipped()), given, before;
    std::vector<Learner::Event> none;
    before.Release(Learner::Shipped(), none);
    CHECK(none.empty());
    std::vector<Learner::Event> expected = run(had, false);
    for (Learner::Learner* learner : { &given, &before }) {
        std::vector<Learner::Event> events = run(*learner, learner == &given);
        CHECK(learner->LearnedNow() == Learner::Shipped().at("7.2.0"));
        REQUIRE(events.size() == expected.size());
        for (size_t i = 0; i < events.size(); i++) {
            CHECK(events[i].kind == expected[i].kind);
            CHECK(events[i].ms == expected[i].ms);
            CHECK(events[i].replayed == expected[i].replayed);
        }
    }
}

TEST_CASE("The map formulas read back as written", "[game_tables]") {
    std::vector<Scene::Formula> back;
    REQUIRE(Scene::FromJson(Scene::ToJson(Scene::Shipped()), back));
    REQUIRE(back.size() == Scene::Shipped().size());
    CHECK(back[0].xz == -1);
    CHECK(back[0].yx == -1);
    CHECK(!Scene::FromJson("[{\"map\": 2}]", back));
    CHECK(!Scene::FromJson("{}", back));
    CHECK(back.size() == Scene::Shipped().size());
}

TEST_CASE("A released id takes its id from the kind that had it, and isn't saved", "[game_tables]") {
    Learner::Known known, released;
    known["7.3.0"][Learner::Kind::ItemGot] = { 200, { 5, 9, 1, 2 } };
    known["7.3.0"][Learner::Kind::ObjectGone] = { 201, { 10, 13 } };
    known["7.3.0"][Learner::Kind::Interact] = { 202, { 11, 7 } };
    released["7.3.0"][Learner::Kind::SceneChanged] = { 200, { 14 } };
    released["7.3.0"][Learner::Kind::PlayerMoved] = { 201, { 1, 2 } };
    Learner::Learner learner(known);
    std::vector<Learner::Event> events;
    learner.Release(released, events);
    const Learner::LearnedSet& now = learner.All().at("7.3.0");
    CHECK(!now.count(Learner::Kind::ItemGot));
    CHECK(now.at(Learner::Kind::SceneChanged).id == 200);
    // sent and received ids are apart
    CHECK(now.at(Learner::Kind::ObjectGone).id == 201);
    CHECK(now.at(Learner::Kind::PlayerMoved).id == 201);
    CHECK(now.at(Learner::Kind::Interact).id == 202);

    std::filesystem::path folder = tempFolder("released-save-test");
    std::filesystem::path file = folder / "learned-messages.json";
    REQUIRE(Learning::Save(file, learner.All(), released));
    Learner::Known saved;
    REQUIRE(Learner::FromJson(Files::readText(file), saved));
    std::filesystem::remove_all(folder);
    CHECK(saved.at("7.3.0").size() == 2);
    CHECK(saved.at("7.3.0").count(Learner::Kind::ObjectGone));
    CHECK(saved.at("7.3.0").count(Learner::Kind::Interact));
}

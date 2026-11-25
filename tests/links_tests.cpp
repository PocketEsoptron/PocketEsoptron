#include <filesystem>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "game_tables.hpp"
#include "item_catalog.hpp"
#include "installed_pack.hpp"
#include "marker_index.hpp"
#include "packets/learner.hpp"
#include "packets/links.hpp"
#include "packets/world.hpp"
#include "recorded_messages.hpp"

namespace {
    // votes of one gadget as counted on the 7.1 recordings: type (or NONE / UNCLEAR) and how many
    // objects, each on a map item of its own
    void cast(Links::Links& links, uint32_t gadget, const std::map<int, int>& votes) {
        static int32_t point = 1;
        static uint32_t config = 1;
        for (auto& [vote, n] : votes)
            for (int i = 0; i < n; i++)
                links.Cast(gadget, 133000001, config++, { vote, vote > 0 ? point++ : 0, 1 });
    }
}

TEST_CASE("A gadget is the type its objects agree on", "[links]") {
    Links::Links links;
    // SceneObj_Chest_Locked_Lv1: Common Chest
    cast(links, 70211002, { { 17, 27 }, { Links::NONE, 5 }, { Links::UNCLEAR, 2 } });
    // SceneObj_Chest_Default_Lv1: Common Chest, one Buried Chest near
    cast(links, 70211101, { { 17, 14 }, { Links::NONE, 11 }, { Links::UNCLEAR, 5 }, { 69, 1 } });
    // SceneObj_Gather_GrassCrystal_Gather: Dendroculus
    cast(links, 70590063, { { 403, 13 }, { Links::NONE, 4 }, { Links::UNCLEAR, 4 } });
    // SceneObj_Gear_Operator_GrassTablet: mostly far from any Dendro Rock
    cast(links, 70900050, { { 415, 4 }, { Links::NONE, 20 }, { Links::UNCLEAR, 1 } });
    // SearchPoint: a few next to chests, most of them not
    cast(links, 70210101, { { 17, 10 }, { 44, 5 }, { Links::UNCLEAR, 3 }, { Links::NONE, 164 } });
    CHECK(links.TypeOf(70211002) == 17);
    CHECK(links.TypeOf(70211101) == 17);
    CHECK(links.TypeOf(70590063) == 403);
    CHECK(links.TypeOf(70900050) == 0);
    CHECK(links.TypeOf(70210101) == 0);
    CHECK(links.Learned() == Links::Known{ { 70211002, 17 }, { 70211101, 17 }, { 70590063, 403 } });
    CHECK(links.TakeChanged());
    CHECK(!links.TakeChanged());
}

TEST_CASE("One object gets one vote and two are needed", "[links]") {
    Links::Links links;
    links.Cast(70590086, 133000001, 5, { 626, 9001, 0.87 });
    links.Cast(70590086, 133000001, 5, { 626, 9001, 0.87 });
    CHECK(links.TypeOf(70590086) == 0);
    links.Cast(70590086, 133000001, 6, { 626, 9002, 1.78 });
    CHECK(links.TypeOf(70590086) == 626);
}

TEST_CASE("A map item counts for the nearest object only", "[links]") {
    Links::Links links;
    // SceneObj_Area_Dq_Property_Prop_BookPage_01 4.28 and 4.69 from two Remarkable Chests, then the
    // chests' own SceneObj_Chest_Default_Lv4 objects 1.25 and 2.21 from them
    links.Cast(70900386, 133000001, 1, { 269, 501, 4.28 });
    links.Cast(70900386, 133000001, 2, { 269, 502, 4.69 });
    CHECK(links.TypeOf(70900386) == 269);
    links.Cast(70211150, 133000002, 1, { 269, 501, 1.25 });
    links.Cast(70211150, 133000002, 2, { 269, 502, 2.21 });
    CHECK(links.TypeOf(70900386) == 0);
    CHECK(links.TypeOf(70211150) == 269);
    // a page seen after the chest gets no vote from it
    links.Cast(70900386, 133000001, 3, { 269, 501, 4.0 });
    CHECK(links.TypeOf(70900386) == 0);
    CHECK(links.Learned() == Links::Known{ { 70211150, 269 } });

    Links::Links back;
    REQUIRE(back.FromJson(links.ToJson()));
    CHECK(back.Learned() == Links::Known{ { 70211150, 269 } });
}

TEST_CASE("Known links hold over votes", "[links]") {
    Links::Links links({ { 70217020, 359 } });
    cast(links, 70217020, { { 17, 5 } });
    CHECK(links.TypeOf(70217020) == 359);
    CHECK(links.Learned() == Links::Known{ { 70217020, 17 } });
    links.SetKnown({});
    CHECK(links.TypeOf(70217020) == 17);
}

TEST_CASE("Votes and known links read back", "[links]") {
    Links::Links links;
    cast(links, 70211012, { { 44, 20 }, { Links::NONE, 8 }, { Links::UNCLEAR, 2 } });
    cast(links, 70210101, { { 17, 1 } });
    Links::Links back;
    REQUIRE(back.FromJson(links.ToJson()));
    CHECK(back.ToJson() == links.ToJson());
    CHECK(back.Learned() == Links::Known{ { 70211012, 44 } });
    CHECK(!back.FromJson("{\"votes\":{\"x\":[]}}"));
    CHECK(!back.FromJson("{\"votes\":{\"5\":[[1,2]]}}"));
    CHECK(!back.FromJson("{\"votes\":{\"4294967297\":[]}}"));
    CHECK(back.Learned() == Links::Known{ { 70211012, 44 } });

    Links::Known known;
    REQUIRE(Links::KnownFromJson(Links::KnownToJson({ { 70217020, 359 }, { 70590063, 403 } }), known));
    CHECK(known == Links::Known{ { 70217020, 359 }, { 70590063, 403 } });
    CHECK(!Links::KnownFromJson("[{\"gadget\":70217020,\"type\":0}]", known));
    CHECK(!Links::KnownFromJson("[{\"gadget\":4294967297,\"type\":17}]", known));
    CHECK(known.size() == 2);
}

TEST_CASE("A gadget whose votes can't make a link any more is dropped", "[links]") {
    Links::Links links;
    // a plain Gadget far from every Dendro Rock: after 43 of 64 objects a third can't agree any more
    cast(links, 70900050, { { Links::NONE, 42 } });
    CHECK(links.ToJson().find("\"70900050\"") != std::string::npos);
    cast(links, 70900050, { { Links::NONE, 1 } });
    CHECK(links.ToJson().find("\"70900050\"") == std::string::npos);
    cast(links, 70900050, { { 415, 30 } });
    CHECK(links.TypeOf(70900050) == 0);
    // 42 far ones and then 22 on Dendro Rocks still make it
    cast(links, 70330218, { { Links::NONE, 42 } });
    cast(links, 70330218, { { 415, 22 } });
    CHECK(links.TypeOf(70330218) == 415);
    // one whose last chance is a map item a nearer object takes
    cast(links, 70900051, { { Links::NONE, 42 } });
    links.Cast(70900051, 133000003, 1, { 415, 900001, 4 });
    CHECK(links.ToJson().find("\"70900051\"") != std::string::npos);
    links.Cast(70330219, 133000003, 2, { 415, 900001, 1 });
    CHECK(links.ToJson().find("\"70900051\"") == std::string::npos);
    CHECK(links.ToJson().find("\"70330219\"") != std::string::npos);

    Links::Links back;
    REQUIRE(back.FromJson(links.ToJson()));
    CHECK(back.ToJson() == links.ToJson());
    cast(back, 70900050, { { 415, 30 } });
    CHECK(back.TypeOf(70900050) == 0);
    CHECK(!back.FromJson("{\"votes\":{},\"never\":[-1]}"));
}

TEST_CASE("Teyvat's groups are on map 2", "[links]") {
    CHECK(Links::MapOf(133004001) == 2);
    CHECK(Links::MapOf(131001001) == 2);
    CHECK(Links::MapOf(134001001) == 0);
    CHECK(Links::MapOf(0) == 0);
}

// gadget names from the game's gadget table, types from HoYoLAB
TEST_CASE("A chest's name gives its type", "[links]") {
    CHECK(Links::ChestByName("SceneObj_Chest_Default_Lv1") == 17);
    CHECK(Links::ChestByName("SceneObj_Chest_Locked_Lv2") == 44);
    CHECK(Links::ChestByName("SceneObj_EssenceChest_Locked_Lv4") == 45);
    CHECK(Links::ChestByName("SceneObj_Chest_Locked_Lv5_UnderWater") == 46);
    CHECK(Links::ChestByName("SceneObj_Chest_Default_Lv2_V70") == 44);
    CHECK(Links::ChestByName("SceneObj_Chest_Locked_Lv4_FollowMove") == 45);
    CHECK(Links::ChestByName("SceneObj_Chest_Bramble_Lv1") == 75);
    CHECK(Links::ChestByName("SceneObj_Chest_Frozen_Lv4") == 75);
    CHECK(Links::ChestByName("SceneObj_Chest_Rock_Lv1") == 75);
    CHECK(Links::ChestByName("SceneObj_Chest_Default_Lv3") == 0);
    CHECK(Links::ChestByName("SceneObj_Chest_Default_Fake_Lv2") == 0);
    CHECK(Links::ChestByName("SceneObj_Drop_Chest_Lv1") == 0);
    CHECK(Links::ChestByName("SceneObj_Ticket_Chest_Locked_Lv1") == 0);
    CHECK(Links::ChestByName("SceneObj_Chest_NormalChest") == 0);
    CHECK(Links::ChestByName("SceneObj_Area_Dq_Property_Ani_Prop_JunkChest_01") == 0);
}

TEST_CASE("The recordings link each gadget to its map item type", "[links]") {
    std::vector<std::filesystem::path> recordings = RecordedMessages::recordings();
    auto catalog = installedPack();
    if (recordings.empty() || !catalog)
        SKIP("no recordings or no items.pack on this PC");
    MarkerIndex::Index index(catalog);
    Links::Types types = Links::TypesOf(*catalog);
    Links::Links links;
    for (const std::filesystem::path& recording : recordings) {
        Learner::Learner learner(Learner::Shipped());
        World::World world;
        std::vector<Learner::Event> in;
        std::vector<World::Event> out;
        for (const Messages::Message& m : RecordedMessages::read(recording / "messages.bin")) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in)
                world.Feed(e, out);
            in.clear();
            for (const World::Event& e : out)
                if (e.what == World::What::Appeared)
                    links.See(e.object, index, catalog->game, types);
            out.clear();
        }
    }
    Links::Known learned = links.Learned();
    std::string all;
    for (auto& [gadget, type] : learned)
        all += std::to_string(gadget) + "=" + std::to_string(type) + " ";
    WARN("links " << all);

    const Links::Known right = {
        { 70211001, 17 }, { 70211002, 17 }, { 70211101, 17 }, { 70211102, 17 },  // SceneObj_Chest_Default_Lv1, Locked_Lv1: Common Chest
        { 73174098, 17 }, { 73174099, 17 },                                       // Default_Lv1_V70, Locked_Lv1_V70: Common Chest
        { 70211012, 44 },                                                         // Locked_Lv2: Exquisite Chest
        { 70211022, 45 }, { 70211121, 45 },                                       // Locked_Lv4, Default_Lv4: Precious Chest
        { 70211150, 269 }, { 70211151, 269 },                                     // Default_Lv4, Locked_Lv4: Remarkable Chest
        { 70210063, 75 }, { 70211103, 75 },                                       // Rock_Lv1, Bramble_Lv1: Sealed Chest
        { 70210105, 129 },                                                        // SearchPoint_OnWater: Wooden Mora Chest
        { 70217014, 359 }, { 70217020, 359 }, { 70330342, 359 },                  // JunkChest, SumeruCrudeChest: Stone Pile Mora Chest
        { 70217027, 366 }, { 73003003, 596 },                                     // FontaineCrudeChest, NataCrudeChest: their mora chests
        { 70217013, 356 },                                                        // BGM: Radiant Spincrystal
        { 70210107, 128 }, { 70210108, 128 },                                     // SearchPoint_Rock: Geo Sigil
        { 70590063, 403 }, { 70590074, 508 }, { 70590086, 626 },                  // Grass, Water, Nata Crystal: Dendro, Hydro, Pyroculus
        { 70211168, 471 },                                                        // SmallStoneFlowerCore: Udumbara
        { 70330366, 429 },                                                        // DeshretRune: Sacred Seal
        { 70590084, 554 },                                                        // CygMemories: Spirit Carp
        // things that grow back, each the type named as the item its gather row gives
        { 70330221, 355 }, { 70330449, 62 }, { 70510005, 63 }, { 70510012, 40 }, // Starshroom, Flaming Flower Stamen, Electro Crystal, Cor Lapis
        { 70520005, 160 }, { 70520009, 161 }, { 70520024, 161 },                  // Sweet Flower, Mint, Mint
        { 70520013, 124 }, { 70540015, 36 }, { 70540027, 93 },                    // Matsutake, Silk Flower, Horsetail
        { 70540028, 78 }, { 70540034, 402 }, { 70540036, 386 },                   // Starconch, Rukkhashava Mushrooms, Harra Fruit
        { 70540042, 384 }, { 70540043, 400 }, { 70540051, 522 },                  // Sumeru Rose, Nilotpala Lotus, Pluie Lotus
        { 70540053, 492 }, { 70540061, 545 }, { 70540081, 620 },                  // Marcotte, Lumitoile, Embercore Flower
        { 70540055, 501 },                                                        // Romaritime Flower
        { 73176001, 854 },                                                        // Lumifluorite
    };
    // shipped without being learned here, by what the game's names say they are
    const Links::Known byHand = {
        { 70590004, 5 }, { 70590021, 6 }, { 70590041, 194 },                      // Wind, Rock, Electric Crystal: Anemo, Geo, Electroculus
        { 70590088, 694 }, { 73094157, 833 },                                     // Moon, Zhidong Crystal: Luno, Cryoculus
        { 70590031, 141 }, { 70590042, 472 },                                     // Ljxs Essence, MoonlitSigil: Crimson Agate, Khvarena
        { 73176013, 838 },                                                        // ZDCrudeChest: Snezhnaya Mora Chest
        // chests of the same levels as the learned ones: Lv2 Exquisite, Lv4 Precious, Lv5 Luxurious
        { 70211011, 44 }, { 70211111, 44 }, { 70211122, 45 },                     // Default_Lv2, Locked_Lv4
        { 70211031, 46 }, { 70211032, 46 }, { 70211131, 46 }, { 70211132, 46 },  // Default_Lv5, Locked_Lv5
        // the regions' mora chests by their model names, Dq JunkChest_01 by the Stone Pile markers it sits on in 3.7
        { 70217026, 366 },                                                        // FontaineCrudeChest_01
        { 70217012, 359 }, { 70217046, 723 }, { 73138056, 819 },                  // Dq JunkChest_01, NodKraiCrudeChest, MoonCrudeChest
    };
    for (auto& [gadget, type] : learned) {
        INFO("gadget " << gadget << " type " << type);
        const Links::Known& list = right.count(gadget) ? right : byHand;
        auto link = list.find(gadget);
        REQUIRE(link != list.end());
        CHECK(link->second == type);
    }
    CHECK(learned.size() >= right.size() - 2);
    // a book page next to Remarkable Chests, an Aranara paint decal next to Dendro Rocks, SearchPoint
    // and wooden objects next to chests now and then
    for (uint32_t gadget : { 70900386, 70230110, 70210101, 70220013, 70220014 }) {
        INFO("gadget " << gadget);
        CHECK(links.TypeOf(gadget) == 0);
    }
    Links::Known shipped = right;
    shipped.insert(byHand.begin(), byHand.end());
    CHECK(Links::Shipped() == shipped);
}

TEST_CASE("Every gather gadget that grows back gets its type by its item's name, the shipped ones agree", "[links]") {
    auto catalog = installedPack();
    if (!catalog || !catalog->game.knowsRanks())
        SKIP("no items pack with the game's ranks and names on this PC");
    Links::Known byName = Links::GathersByName(catalog->game, *catalog);
    Links::Types types = Links::TypesOf(*catalog);
    for (auto& [gadget, type] : Links::Shipped()) {
        if (!types.regrows.count(type)) continue;
        INFO("gadget " << gadget);
        CHECK(byName.count(gadget));
        if (byName.count(gadget)) CHECK(byName.at(gadget) == type);
    }
}

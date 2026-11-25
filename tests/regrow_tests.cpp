#include <filesystem>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "files.hpp"
#include "game_server.hpp"
#include "game_tables.hpp"
#include "item_files.hpp"
#include "regrow.hpp"

// When a picked up thing grows back, on whole rows of the game's 7.2 tables
// (tests/data/game_tables, see its answers.md). The times are worked out by
// hand from the fandom wiki's Reset page.

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data" / "game_tables";

    GameTables::Tables tables(bool ranks = true) {
        GameTables::Tables out;
        std::string gathers = Files::readText(DATA / "GatherExcelConfigData.json");
        std::string materials = Files::readText(DATA / "MaterialExcelConfigData.json");
        REQUIRE(!gathers.empty());
        REQUIRE(!materials.empty());
        REQUIRE(out.read(GameTables::GATHERS, ItemFiles::trimGameTable(GameTables::GATHERS, gathers)));
        std::string trimmed = ItemFiles::trimGameTable(GameTables::MATERIALS, materials);
        if (!ranks) {
            // a pack made before the ranks were kept
            nlohmann::json rows = nlohmann::json::parse(trimmed);
            for (auto& row : rows) row.erase("rank");
            trimmed = rows.dump();
        }
        REQUIRE(out.read(GameTables::MATERIALS, trimmed));
        return out;
    }

    const GameTables::Tables& game() {
        static const GameTables::Tables t = tables();
        return t;
    }

    const GameServer::Server* server(const char* key) {
        const GameServer::Server* s = GameServer::find(key);
        REQUIRE(s != nullptr);
        return s;
    }

    // 2026-11-24 12:00 UTC
    const int64_t NOON = 1795521600;
    const int64_t HOUR = 3600;
}

TEST_CASE("A local specialty grows back 48 h after the pickup, also one the gather table gives no refreshId", "[regrow]") {
    // Quenepa Berry, Natlan, rank 316
    Regrow::Rule rule = Regrow::Of(101249, game());
    CHECK(game().refreshOf(101249) == 0);
    CHECK(rule.clock == Regrow::Clock::AfterPickup);
    CHECK(!rule.needsServer());
    CHECK(Regrow::BackAt(rule, NOON, nullptr) == NOON + 48 * HOUR);
}

TEST_CASE("Crystal Chunk grows back 72 h after the pickup", "[regrow]") {
    Regrow::Rule rule = Regrow::Of(101003, game());
    CHECK(game().refreshOf(101003) == 999932);
    // 2026-11-27 12:00 UTC
    CHECK(Regrow::BackAt(rule, NOON, server("europe")) == 1795780800);
}

TEST_CASE("Iron Chunk and an Apple are back at the next 0:00 on the server's clock", "[regrow]") {
    Regrow::Rule iron = Regrow::Of(101001, game());
    REQUIRE(iron.clock == Regrow::Clock::ServerHour);
    CHECK(iron.needsServer());
    // Europe is UTC+1: picked 22:30 UTC (23:30 there), back 23:00 UTC the same day
    CHECK(Regrow::BackAt(iron, 1795559400, server("europe")) == 1795561200);
    // picked 23:30 UTC (0:30 there the next day), back at the 0:00 after, 23:00 UTC on the 25th
    CHECK(Regrow::BackAt(iron, 1795563000, server("europe")) == 1795647600);
    // Asia is UTC+8: picked at noon UTC (20:00 there), back 16:00 UTC
    Regrow::Rule apple = Regrow::Of(100001, game());
    CHECK(apple.clock == Regrow::Clock::ServerHour);
    CHECK(Regrow::BackAt(apple, NOON, server("asia")) == 1795536000);
}

TEST_CASE("A kind on the server's clock has no time while no server is picked", "[regrow]") {
    CHECK(Regrow::BackAt(Regrow::Of(101001, game()), NOON, nullptr) == 0);
}

TEST_CASE("Magical Crystal is back when the mining outcrops move, 6:00 on the server's clock", "[regrow]") {
    Regrow::Rule rule = Regrow::Of(101004, game());
    REQUIRE(rule.clock == Regrow::Clock::ServerHour);
    // America is UTC-5: picked at noon UTC (7:00 there), back 11:00 UTC the next day
    CHECK(Regrow::BackAt(rule, NOON, server("america")) == 1795604400);
}

TEST_CASE("An Oculus and an item the tables don't have never grow back", "[regrow]") {
    CHECK(!Regrow::Of(107023, game()).grows());
    CHECK(Regrow::BackAt(Regrow::Of(107023, game()), NOON, server("europe")) == 0);
    CHECK(!Regrow::Of(999999, game()).grows());
}

TEST_CASE("The tables know the ranks only when the pack kept them", "[regrow]") {
    CHECK(game().knowsRanks());
    CHECK(!tables(false).knowsRanks());
}

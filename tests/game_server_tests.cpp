#include <filesystem>
#include <fstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "asks.hpp"
#include "extensions.hpp"
#include "game_server.hpp"
#include "settings.hpp"
#include "test_extension.hpp"
#include "triggers.hpp"

// The game server the player picks: its daily reset, what Lua gets, the
// question for it and its trigger.

using Permission = Extensions::ExtensionPermissionTypes;

namespace {
    // no server picked and no questions, before and after
    struct NoServer {
        NoServer() {
            std::filesystem::remove(Settings::file);
            Settings::gameServer.clear();
            Asks::clear();
        }
        ~NoServer() {
            Settings::gameServer.clear();
            Asks::clear();
            TestExtension::unloadAll();
        }
    };

    int64_t resetAfter(const char* key, int64_t now) {
        const GameServer::Server* server = GameServer::find(key);
        REQUIRE(server != nullptr);
        return GameServer::nextReset(*server, now);
    }
}

TEST_CASE("every server has its own key, an unknown key is none", "[game_server]") {
    REQUIRE(GameServer::list().size() == 5);
    for (const GameServer::Server& server : GameServer::list()) CHECK(GameServer::find(server.key) == &server);
    CHECK(GameServer::find("") == nullptr);
    CHECK(GameServer::find("Europe") == nullptr);
}

TEST_CASE("the next reset is 4:00 on the server's clock", "[game_server]") {
    // 2026-11-24 2:30 UTC is 3:30 in Europe: 4:00 there is 3:00 UTC the same day
    CHECK(resetAfter("europe", 1795487400) == 1795489200);
    // at the reset itself the next one is a day later
    CHECK(resetAfter("europe", 1795489200) == 1795489200 + 86400);
    // 2026-11-24 10:00 UTC is 5:00 in America, past its reset: 9:00 UTC the next day
    CHECK(resetAfter("america", 1795514400) == 1795597200);
    // 2026-11-23 20:00 UTC is already 4:00 the 24th in Asia: the next is the 25th, 20:00 UTC the 24th
    CHECK(resetAfter("asia", 1795464000) == 1795550400);
    CHECK(resetAfter("tw_hk_mo", 1795464000) == 1795550400);
    CHECK(resetAfter("china", 1795464000) == 1795550400);
}

TEST_CASE("the picked server is saved, a key the overlay doesn't know loads as none", "[game_server]") {
    NoServer none;
    Settings::gameServer = "asia";
    Settings::Save();
    Settings::gameServer.clear();
    Settings::Load();
    CHECK(Settings::gameServer == "asia");
    CHECK(GameServer::picked() == GameServer::find("asia"));

    Settings::gameServer = "moon";
    Settings::Save();
    Settings::Load();
    CHECK(Settings::gameServer.empty());
    CHECK(GameServer::picked() == nullptr);

    // not a string at all
    std::ofstream(Settings::file, std::ios::trunc) << R"({ "gameServer": null })";
    Settings::gameServer = "asia";
    Settings::Load();
    CHECK(Settings::gameServer.empty());
    std::ofstream(Settings::file, std::ios::trunc) << R"({ "gameServer": 2 })";
    Settings::Load();
    CHECK(Settings::gameServer.empty());
}

TEST_CASE("Lua: GetGameServer needs Game server, gives nil until one is picked", "[game_server]") {
    NoServer none;
    Extensions::Extension* ext = TestExtension::load("game_server_get", "");
    REQUIRE(ext != nullptr);
    sol::state& lua = *ext->luaState;

    // not allowed: nil, and the call asks for it
    Settings::gameServer = "europe";
    REQUIRE(TestExtension::run(ext, "_G.got = GetGameServer()"));
    CHECK(!lua["got"].valid());
    REQUIRE(Asks::queue().size() == 1);
    CHECK(Asks::queue()[0].permissions == std::vector<Permission>{ Permission::GameServer });
    Asks::clear();

    Extensions::allow(*ext, Permission::GameServer);
    Settings::gameServer.clear();
    REQUIRE(TestExtension::run(ext, "_G.got = GetGameServer()"));
    CHECK(!lua["got"].valid());

    Settings::gameServer = "tw_hk_mo";
    REQUIRE(TestExtension::run(ext, "_G.got = GetGameServer()"));
    sol::table got = lua["got"];
    CHECK(got["server"].get<std::string>() == "tw_hk_mo");
    CHECK(got["name"].get<std::string>() == "TW, HK, MO");
    CHECK(got["utcOffset"].get<int>() == 8);
    int64_t now = (int64_t)std::time(nullptr);
    int64_t next = got["nextReset"].get<int64_t>();
    CHECK(next > now);
    CHECK(next <= now + 86400);
    CHECK((next + 8 * 3600) % 86400 == 4 * 3600);
}

TEST_CASE("asked with none picked the question has the choice, allowed it asks only for a pick", "[game_server]") {
    NoServer none;
    Extensions::Extension* ext = TestExtension::load("game_server_ask", "");
    REQUIRE(ext != nullptr);

    REQUIRE(TestExtension::run(ext, "AskForPermission('game_server')"));
    REQUIRE(Asks::queue().size() == 1);
    CHECK(Asks::queue()[0].kind == Asks::Kind::Permissions);
    Asks::answer(Asks::Answer::Allow);
    CHECK(ext->extensionPermissions.count(Permission::GameServer) == 1);

    // allowed without a pick: the pick is asked next
    REQUIRE(Asks::queue().size() == 1);
    CHECK(Asks::queue()[0].kind == Asks::Kind::Server);
    // a second ask joins it
    REQUIRE(TestExtension::run(ext, "AskForPermission('game_server')"));
    CHECK(Asks::queue().size() == 1);

    // Not now holds for this start
    Asks::answer(Asks::Answer::NotNow);
    CHECK(Asks::queue().empty());
    REQUIRE(TestExtension::run(ext, "AskForPermission('game_server')"));
    CHECK(Asks::queue().empty());

    // with one picked there's nothing to ask
    ext->serverPickRefused = false;
    Settings::gameServer = "china";
    REQUIRE(TestExtension::run(ext, "AskForPermission('game_server')"));
    CHECK(Asks::queue().empty());
}

TEST_CASE("the GameServer trigger runs when the player picks another and at each reset, only with the permission", "[game_server][triggers]") {
    NoServer none;
    Extensions::Extension* ext = TestExtension::load("game_server_trigger", R"(
        _G.runs = 0
        function Init()
            AddTrigger("GameServer", function(server, what)
                _G.runs = _G.runs + 1
                _G.given = server
                _G.what = what
            end)
        end
    )");
    REQUIRE(ext != nullptr);
    Extensions::allow(*ext, Permission::GameServer);
    Extensions::runInit(*ext);
    sol::state& lua = *ext->luaState;

    Triggers::State state;
    Triggers::check(state);
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 0);

    state.gameServer = "america";
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 1);
    CHECK(lua["what"].get<std::string>() == "server");
    CHECK(lua["given"]["server"].get<std::string>() == "america");
    CHECK(lua["given"]["utcOffset"].get<int>() == -5);

    // back to none: it runs with nil
    state.gameServer.clear();
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 2);
    CHECK(!lua["given"].valid());

    // the game's next day: it runs at the reset with the next one
    state.gameServer = "europe";
    state.time = 1795487400;
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 3);
    CHECK(lua["what"].get<std::string>() == "server");
    CHECK(lua["given"]["nextReset"].get<int64_t>() == 1795489200);
    state.time = 1795489199;
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 3);
    state.time = 1795489200;
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 4);
    CHECK(lua["what"].get<std::string>() == "reset");
    CHECK(lua["given"]["nextReset"].get<int64_t>() == 1795489200 + 86400);

    // switched off it doesn't run, and starts again from what it sees
    Extensions::switchPermission(*ext, Permission::GameServer, false);
    state.gameServer = "asia";
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 4);
    Extensions::switchPermission(*ext, Permission::GameServer, true);
    Triggers::check(state);
    CHECK(lua["runs"].get<int>() == 4);
}

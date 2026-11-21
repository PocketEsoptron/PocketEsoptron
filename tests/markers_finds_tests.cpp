#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>
#include <sol/sol.hpp>

// The Markers extension's finds (extensions/markers/finds.lua): what the
// overlay finds done is ticked in collected. The finds are the ones the
// overlay gives on 2026-11-22_play: two Wooden Mora Chests side by side in
// group 133114067, 67006 opened first while 67005 stood as near to 65365,
// then 67005 on 65365 (finds_tests). The player ticked 65365 and 65366.

namespace fs = std::filesystem;

namespace {
    const fs::path MARKERS = fs::path(__FILE__).parent_path().parent_path() / "extensions" / "markers";

    struct Markers {
        sol::state lua;
        Markers() {
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::package, sol::lib::math);
            lua["package"]["path"] = (MARKERS / "?.lua").string();
            lua.script(R"(
                files = {}
                local function copy(value)
                    if type(value) ~= "table" then return value end
                    local out = {}
                    for k, v in pairs(value) do out[k] = copy(v) end
                    return out
                end
                FileExists = function(name) return files[name] ~= nil end
                ReadFile = function(name) return files[name] end
                WriteFile = function(name, data) files[name] = data return true end
                JSON_Encode = copy
                JSON_Decode = copy
                GetOSTimeMS = function() return 1795365183885 end
                -- what the overlay found, GetFinds gives the ones after an id
                given = {}
                GetFinds = function(after)
                    local out = {}
                    for _, f in ipairs(given) do if f.id > after then out[#out + 1] = f end end
                    return out
                end
                first = { id = 1795365183884001, how = "opened", type = 129, object = "133114067/67006",
                    maybe = { 65365, 65366 }, rivals = { "133114067/67005" } }
                second = { id = 1795365183884002, how = "opened", type = 129, object = "133114067/67005", point = 65365 }
                function start()
                    for _, name in ipairs({ "collected", "finds" }) do package.loaded[name] = nil end
                    collected = require("collected")
                    finds = require("finds")
                    collected.load()
                    finds.load()
                end
                function found(f)
                    given[#given + 1] = f
                    finds.update()
                end
                start()
            )");
        }
        void run(const std::string& code) { lua.script(code); }
        std::set<int> collected() {
            std::set<int> out;
            sol::table list = lua.script("return collected.list()");
            for (auto& [key, value] : list) out.insert(value.as<int>());
            return out;
        }
        bool maybe(int id) { return lua.script("return finds.maybe[" + std::to_string(id) + "] == true"); }
    };
}

TEST_CASE("A find the overlay is sure of is ticked", "[markers]") {
    Markers m;
    m.run("found(second)");
    CHECK(m.collected() == std::set<int>{ 65365 });
}

TEST_CASE("A find with a rival waits, and is ticked once the rival is found on the other map item", "[markers]") {
    Markers m;
    m.run("found(first)");
    CHECK(m.collected().empty());
    CHECK(m.maybe(65365));
    CHECK(m.maybe(65366));
    m.run("found(second)");
    CHECK(m.collected() == std::set<int>{ 65365, 65366 });
    CHECK_FALSE(m.maybe(65366));
}

TEST_CASE("Two waiting finds on the same two map items tick both", "[markers]") {
    Markers m;
    m.run(R"(
        found({ id = 1795365183884001, object = "133114067/67006", maybe = { 65365, 65366 }, rivals = {} })
    )");
    CHECK(m.collected().empty());
    m.run(R"(
        found({ id = 1795365183884002, object = "133114067/67005", maybe = { 65365, 65366 }, rivals = {} })
    )");
    CHECK(m.collected() == std::set<int>{ 65365, 65366 });
}

TEST_CASE("The player's tick of a maybe takes the find, the other maybe isn't ticked", "[markers]") {
    Markers m;
    m.run(R"(
        found({ id = 1795365183884001, object = "133114067/67006", maybe = { 65365, 65366 }, rivals = {} })
        collected.set(65366, true, "tap")
    )");
    CHECK(m.collected() == std::set<int>{ 65366 });
    CHECK_FALSE(m.maybe(65365));
    // a later find on the pair is the other one
    m.run(R"(
        found({ id = 1795365183884002, object = "133114067/67005", maybe = { 65365, 65366 }, rivals = {} })
    )");
    CHECK(m.collected() == std::set<int>{ 65365, 65366 });
}

TEST_CASE("A sync's tick of a maybe takes no find, the other maybe is ticked once settled", "[markers]") {
    Markers m;
    m.run(R"(
        found({ id = 1795365183884001, object = "133114067/67006", maybe = { 65365, 65366 }, rivals = {} })
        collected.change({ 65365 }, {}, "hoyolab_map")
    )");
    CHECK(m.collected() == std::set<int>{ 65365 });
    m.run("finds.tick()");
    CHECK(m.collected() == std::set<int>{ 65365, 65366 });
    CHECK_FALSE(m.maybe(65366));
}

TEST_CASE("A find is taken after a run whose clock was ahead", "[markers]") {
    Markers m;
    m.run(R"(
        files["finds.json"] = { last = 1795368783884001, waiting = {} }
        start()
        found(second)
    )");
    CHECK(m.collected() == std::set<int>{ 65365 });
}

TEST_CASE("A waiting find and the last one taken are kept over a restart", "[markers]") {
    Markers m;
    m.run("found(first)");
    m.run("start()");
    CHECK(m.maybe(65366));
    // the overlay gives the same find again: it isn't taken twice
    m.run("finds.update()");
    m.run("found(second)");
    CHECK(m.collected() == std::set<int>{ 65365, 65366 });
    // an unticked one stays unticked when the finds are read again
    m.run(R"(
        collected.set(65366, false, "tap")
        start()
        finds.update()
    )");
    CHECK(m.collected() == std::set<int>{ 65365 });
}

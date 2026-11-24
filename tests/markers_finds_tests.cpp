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
// starconch is the Starconch picked on 2026-10-14_21-47, back 48 h later.

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
                clock = 1795365183885
                GetOSTimeMS = function() return clock end
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
                -- what Markers gave the overlay last
                SetFindsDone = function(done, growing) givenDone, givenGrowing = done, growing end
                -- the Starconch's object seen again a day later
                starconchBack = { id = 1792093749676001, how = "back", type = 78, object = "133105804/154004" }
                starconch = { id = 1792007349676001, how = "picked_up", type = 78, object = "133105804/154004",
                    point = 4620, item = 100033, back_at = 1792180149 }
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

TEST_CASE("A find of a thing that grows back is ticked until it's back", "[markers]") {
    Markers m;
    m.run("clock = 1792007350000 found(starconch) finds.tick()");
    CHECK(m.collected() == std::set<int>{ 4620 });
    m.run("clock = 1792180148999 finds.tick()");
    CHECK(m.collected() == std::set<int>{ 4620 });
    m.run("clock = 1792180149000 finds.tick()");
    CHECK(m.collected().empty());
    CHECK(m.lua.script("return finds.backAt(4620) == nil").get<bool>());
}

TEST_CASE("When a thing grows back is kept over a restart", "[markers]") {
    Markers m;
    m.run("clock = 1792007350000 found(starconch) finds.tick()");
    m.run("clock = 1792200000000 start() finds.tick()");
    CHECK(m.collected().empty());
}

TEST_CASE("A grow-back tick taken off and put back by hand stays", "[markers]") {
    Markers m;
    m.run("clock = 1792007350000 found(starconch) collected.set(4620, false, 'tap') collected.set(4620, true, 'tap') finds.tick()");
    m.run("clock = 1792200000000 finds.tick()");
    CHECK(m.collected() == std::set<int>{ 4620 });
}

TEST_CASE("A grow-back tick comes off when the overlay sees its object again", "[markers]") {
    Markers m;
    m.run("clock = 1792007350000 found(starconch) finds.tick()");
    CHECK(m.collected() == std::set<int>{ 4620 });
    m.run("clock = 1792093750000 found(starconchBack) finds.tick()");
    CHECK(m.collected().empty());
    CHECK(m.lua.script("return finds.backAt(4620) == nil").get<bool>());
    // and stays off over a restart and past its old time
    m.run("start() clock = 1792200000000 finds.tick()");
    CHECK(m.collected().empty());
}

TEST_CASE("A back find of an object Markers doesn't wait on changes nothing", "[markers]") {
    Markers m;
    m.run("collected.set(4620, true, 'tap') clock = 1792093750000 found(starconchBack) finds.tick()");
    CHECK(m.collected() == std::set<int>{ 4620 });
}

TEST_CASE("The ticks and the object waiting to grow back go to the overlay, also after a restart", "[markers]") {
    Markers m;
    m.run("collected.set(65365, true, 'tap') clock = 1792007350000 found(starconch) finds.tick()");
    auto check = [&] {
        CHECK(m.lua.script("return #givenDone == 2 and givenDone[1] == 4620 and givenDone[2] == 65365").get<bool>());
        CHECK(m.lua.script(R"(
            local g = givenGrowing
            return #g == 1 and g[1].object == "133105804/154004" and #g[1].points == 1 and g[1].points[1] == 4620
                and g[1].back_at == 1792180149
        )").get<bool>());
    };
    check();
    m.run("givenDone, givenGrowing = nil, nil start() finds.tick()");
    check();
}

TEST_CASE("With auto off a find ticks nothing, also once it's on again", "[markers]") {
    Markers m;
    m.run("finds.setAuto(false) clock = 1792007350000 found(starconch) finds.tick()");
    CHECK(m.collected().empty());
    m.run("start()");
    CHECK_FALSE(m.lua.script("return finds.auto").get<bool>());
    m.run("finds.setAuto(true) finds.update() finds.tick()");
    CHECK(m.collected().empty());
}

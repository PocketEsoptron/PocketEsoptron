#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "packets/learner.hpp"
#include "packets/world.hpp"
#include "recorded_messages.hpp"

// The World on the learner's events from the cuts in tests/data/learner
// (answers.md there says what happens in each).

using Learner::Kind;
using World::What;

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data" / "learner";

    Learner::Known known71() {
        Learner::Known known;
        known[""] = Learner::Shipped().at("7.1.0");
        return known;
    }

    struct Run {
        World::World world;
        std::vector<World::Event> events;
        size_t biggest = 0;
        size_t mostBytes = 0;
    };

    void play(Run& run, Learner::Learner& learner, const std::vector<Messages::Message>& messages) {
        std::vector<Learner::Event> in;
        for (const Messages::Message& m : messages) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in)
                run.world.Feed(e, run.events);
            in.clear();
            run.biggest = std::max(run.biggest, run.world.Size());
            run.mostBytes = std::max(run.mostBytes, run.world.Bytes());
        }
    }

    Run play(const std::string& name, Learner::Known known) {
        Run run;
        Learner::Learner learner(std::move(known));
        play(run, learner, RecordedMessages::read(DATA / name));
        return run;
    }

    size_t count(const Run& run, What what) {
        return std::count_if(run.events.begin(), run.events.end(), [&](const World::Event& e) { return e.what == what; });
    }
}

TEST_CASE("Every opening of an object that appeared while listening is known", "[world]") {
    Run run = play("7.1-puzzles.bin", known71());
    std::set<std::pair<uint32_t, uint32_t>> appeared;
    size_t known = 0;
    for (const World::Event& e : run.events) {
        if (e.what == What::Appeared)
            appeared.insert({ e.object.group, e.object.config });
        if (e.what != What::Opened)
            continue;
        INFO("group " << e.object.group << " config " << e.object.config);
        // 133104010 10048 is opened 27 s in, near since before the cut began
        CHECK(e.known == (appeared.count({ e.object.group, e.object.config }) > 0));
        if (!e.known)
            continue;
        known++;
        CHECK(e.object.gadget >= 70000000);
        CHECK(e.object.gadget < 80000000);
        CHECK(e.object.hasPos);
    }
    CHECK(known == 3);
    CHECK(count(run, What::Appeared) > 0);
    CHECK(count(run, What::Left) > 0);
    CHECK(count(run, What::GotItem) > 0);
    CHECK(count(run, What::PlayerMoved) > 0);
}

TEST_CASE("After a scene entry nothing seen before it is left", "[world]") {
    for (const char* name : { "7.1-pickups.bin", "7.1-puzzles.bin" }) {
        INFO(name);
        Run run;
        Learner::Learner learner(known71());
        std::vector<Messages::Message> messages = RecordedMessages::read(DATA / name);
        std::vector<Learner::Event> in;
        size_t scenes = 0;
        for (const Messages::Message& m : messages) {
            learner.Feed(m, in);
            for (const Learner::Event& e : in) {
                run.world.Feed(e, run.events);
                if (e.kind != Kind::SceneChanged)
                    continue;
                scenes++;
                for (auto& [entity, o] : run.world.Objects())
                    CHECK(o.ms >= e.ms);
                CHECK(run.world.Player().x == e.pos.x);
            }
            in.clear();
        }
        // both cuts hold a teleport
        CHECK(scenes > 0);
    }
}

TEST_CASE("Learning from nothing ends with the same world as knowing the ids", "[world]") {
    // from nothing the first messages wait and come back as replays, older than
    // what came after them: the world must still end the same
    for (const char* name : { "7.1-pickups.bin", "7.1-puzzles.bin" }) {
        INFO(name);
        Run knowing = play(name, known71());
        Run learning = play(name, {});
        CHECK(learning.events.end() != std::find_if(learning.events.begin(), learning.events.end(),
                                                    [](const World::Event& e) { return e.replayed; }));
        std::set<uint64_t> a, b;
        for (auto& [entity, o] : knowing.world.Objects())
            a.insert(entity);
        for (auto& [entity, o] : learning.world.Objects())
            b.insert(entity);
        CHECK(a == b);
        for (auto& [entity, o] : knowing.world.Objects()) {
            const World::Object* other = learning.world.Find(entity);
            if (!other)
                continue;
            INFO("entity " << entity);
            CHECK(other->gadget == o.gadget);
            CHECK(other->state == o.state);
        }
        // the same openings of what the world saw, whenever they were heard
        // (learning from nothing, one of what it never saw can't be told from other messages)
        std::multiset<std::pair<uint32_t, uint32_t>> openedA, openedB;
        for (const World::Event& e : knowing.events)
            if (e.what == What::Opened && e.known)
                openedA.insert({ e.object.group, e.object.config });
        for (const World::Event& e : learning.events)
            if (e.what == What::Opened && e.known)
                openedB.insert({ e.object.group, e.object.config });
        CHECK(openedA == openedB);
    }
}

TEST_CASE("The world stays small and quick on every recording", "[world]") {
    std::vector<std::filesystem::path> recordings = RecordedMessages::recordings();
    if (recordings.empty())
        SKIP("no recordings on this PC");
    size_t biggest = 0, mostBytes = 0;
    std::string where;
    double busy = 0, played = 0;
    for (const std::filesystem::path& recording : recordings) {
        Run run;
        Learner::Learner learner(Learner::Shipped());
        std::vector<Messages::Message> messages = RecordedMessages::read(recording / "messages.bin");
        if (messages.empty())
            continue;
        auto start = std::chrono::steady_clock::now();
        play(run, learner, messages);
        busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        played += (messages.back().ms - messages.front().ms) / 1000.0;
        if (run.mostBytes > mostBytes)
            where = recording.filename().string();
        biggest = std::max(biggest, run.biggest);
        mostBytes = std::max(mostBytes, run.mostBytes);
    }
    INFO("most objects " << biggest << ", most bytes " << mostBytes << " on " << where);
    CHECK(biggest <= World::World::CAP);
    CHECK(mostBytes < 4 * 1024 * 1024);
    WARN("World: most objects " << biggest << ", about " << mostBytes / 1024 << " KB, on " << where);
    // the learner and the world on one thread, against the time the game took to send it all
    WARN("Learner and World: " << busy << " s for " << played << " s of messages, " << 100 * busy / played << "% of one thread");
    CHECK(busy < played / 100);
}


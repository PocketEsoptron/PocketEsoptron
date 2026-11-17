#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "packets/learner.hpp"

// tests/data/learner holds cuts of real sessions as messages, see its
// answers.md for where they come from and what each one has to teach.

using Learner::Kind;
using Learner::Learned;

namespace {
    const std::filesystem::path DATA = std::filesystem::path(__FILE__).parent_path() / "data" / "learner";
    // the whole recordings as messages, only on the PC they were made on
    const std::filesystem::path RECORDINGS = "D:\\dev\\genshin-overlay\\packets";

    // per message: u64 unix ms, u8 side (0 received, 1 sent), u16 id, u32 size, the body
    std::vector<Messages::Message> readMessages(const std::filesystem::path& path) {
        std::vector<Messages::Message> out;
        std::ifstream file(path, std::ios::binary);
        uint8_t head[15];
        while (file.read((char*)head, sizeof(head))) {
            Messages::Message m;
            for (int b = 0; b < 8; b++)
                m.ms |= uint64_t(head[b]) << (8 * b);
            m.direction = head[8] ? Frames::Direction::Sent : Frames::Direction::Received;
            m.id = uint16_t(head[9] | head[10] << 8);
            uint32_t size = 0;
            for (int b = 0; b < 4; b++)
                size |= uint32_t(head[11 + b]) << (8 * b);
            m.body.resize(size);
            if (!file.read(m.body.data(), size))
                break;
            out.push_back(std::move(m));
        }
        return out;
    }

    std::vector<Learner::Event> feed(Learner::Learner& learner, const std::string& name) {
        std::vector<Messages::Message> messages = readMessages(DATA / name);
        REQUIRE(!messages.empty());
        std::vector<Learner::Event> events;
        for (const Messages::Message& m : messages)
            learner.Feed(m, events);
        return events;
    }

    // what answers.md gives for 7.1, by hand from the recordings
    std::map<Kind, Learned> answers71() {
        return {
            { Kind::ObjectsNear, { 27685, { 8, 2, 4, 13, 1, 2, 3 } } },
            { Kind::ItemGot, { 652, { 11, 13, 1, 2 } } },
            { Kind::StateChanged, { 22292, { 3, 7 } } },
            { Kind::ObjectGone, { 24935, { 5, 11 } } },
            { Kind::Interact, { 5765, { 11, 4 } } },
            { Kind::InteractAnswer, { 881, { 15, 3 } } },
            { Kind::ChestOpened, { 28616, { 12, 11 } } },
            { Kind::PlayerMoved, { 2246, { 8, 6, 9, 15, 1, 2 } } },
            { Kind::SceneChanged, { 9582, { 8 } } },
        };
    }

    void requireLearned(const Learner::Learner& learner, const std::map<Kind, Learned>& answers, const std::set<Kind>& kinds) {
        for (int k = 0; k < Learner::KINDS; k++) {
            Kind kind = Kind(k);
            INFO(Learner::KindCode(kind));
            if (!kinds.count(kind)) {
                CHECK(!learner.IsLearned(kind));
                continue;
            }
            REQUIRE(learner.IsLearned(kind));
            CHECK(learner.LearnedNow().at(kind) == answers.at(kind));
        }
    }

    size_t countOf(const std::vector<Learner::Event>& events, Kind kind, bool replayed) {
        size_t n = 0;
        for (const Learner::Event& e : events)
            n += e.kind == kind && e.replayed == replayed;
        return n;
    }
}

TEST_CASE("The learner finds 7.1's kinds on a pickups walk, with nothing known", "[learner]") {
    Learner::Learner learner;
    feed(learner, "7.1-pickups.bin");
    // no chest is opened in it
    requireLearned(learner, answers71(), { Kind::ObjectsNear, Kind::ItemGot, Kind::StateChanged, Kind::ObjectGone, Kind::Interact,
                                           Kind::InteractAnswer, Kind::PlayerMoved, Kind::SceneChanged });
}

TEST_CASE("The learner finds all of 7.1's kinds on the puzzles, with nothing known", "[learner]") {
    Learner::Learner learner;
    feed(learner, "7.1-puzzles.bin");
    requireLearned(learner, answers71(), { Kind::ObjectsNear, Kind::ItemGot, Kind::StateChanged, Kind::ObjectGone, Kind::Interact,
                                           Kind::InteractAnswer, Kind::ChestOpened, Kind::PlayerMoved, Kind::SceneChanged });
}

TEST_CASE("The learner reads 7.2's version from the login and finds its objects and movement", "[learner]") {
    Learner::Learner learner;
    feed(learner, "7.2-login.bin");
    CHECK(learner.Version() == "7.2.0");
    feed(learner, "7.2-walk.bin");
    CHECK(learner.Version() == "7.2.0");
    std::map<Kind, Learned> answers = {
        { Kind::ObjectsNear, { 4469, { 2, 2, 4, 13, 1, 2, 3 } } },
        { Kind::PlayerMoved, { 24007, { 12, 14, 14, 8, 1, 2 } } },
    };
    requireLearned(learner, answers, { Kind::ObjectsNear, Kind::PlayerMoved });
    // what the build ships for 7.2 is what this finds
    CHECK(Learner::Shipped().at("7.2.0") == learner.LearnedNow());
}

TEST_CASE("Messages before a kind is learned are read once it is, and nothing comes out before", "[learner]") {
    // the same messages with the ids known from the start give every event
    Learner::Known known;
    known[""] = answers71();
    Learner::Learner told(known);
    std::vector<Learner::Event> all = feed(told, "7.1-puzzles.bin");
    Learner::Learner blind;
    std::vector<Learner::Event> found = feed(blind, "7.1-puzzles.bin");

    for (Kind kind : { Kind::ItemGot, Kind::ObjectGone, Kind::Interact, Kind::ChestOpened, Kind::StateChanged }) {
        INFO(Learner::KindCode(kind));
        CHECK(countOf(all, kind, false) > 0);
        REQUIRE(countOf(found, kind, true) > 0);
        // the message that made it learned is the last one read again
        uint64_t learnedAt = 0;
        for (const Learner::Event& e : found)
            if (e.kind == kind && e.replayed)
                learnedAt = std::max(learnedAt, e.ms);
        // what came up to 10 minutes before, and nothing older
        std::vector<uint64_t> expected, got;
        for (const Learner::Event& e : all)
            if (e.kind == kind && e.ms + 10 * 60 * 1000 >= learnedAt)
                expected.push_back(e.ms);
        for (const Learner::Event& e : found)
            if (e.kind == kind)
                got.push_back(e.ms);
        std::sort(got.begin(), got.end());
        CHECK(got == expected);
    }
}

TEST_CASE("A learned id that doesn't fit is dropped and learned again", "[learner]") {
    Learner::Known known;
    known[""] = answers71();
    // objects near's id as item got: none of its messages has an item part
    known[""][Kind::ItemGot] = { 27685, { 11, 13, 1, 2 } };
    Learner::Learner learner(known);
    feed(learner, "7.1-puzzles.bin");
    REQUIRE(learner.IsLearned(Kind::ItemGot));
    CHECK(learner.LearnedNow().at(Kind::ItemGot) == answers71().at(Kind::ItemGot));
    CHECK(learner.LearnedNow().at(Kind::ObjectsNear) == answers71().at(Kind::ObjectsNear));
}

TEST_CASE("The learned file keeps each version", "[learner]") {
    Learner::Known known = Learner::Shipped();
    REQUIRE(known.at("7.1.0") == answers71());
    std::string text = Learner::ToJson(known);
    Learner::Known back;
    REQUIRE(Learner::FromJson(text, back));
    CHECK(back == known);
    CHECK(!Learner::FromJson("not json", back));
    CHECK(!Learner::FromJson("{\"7.2.0\": {\"objects_near\": {\"id\": 70000, \"fields\": [1]}}}", back));
}

TEST_CASE("A login of a known version starts with its ids", "[learner]") {
    Learner::Learner learner(Learner::Shipped());
    std::vector<Learner::Event> events = feed(learner, "7.2-login.bin");
    CHECK(learner.Version() == "7.2.0");
    CHECK(learner.IsLearned(Kind::ObjectsNear));
    CHECK(learner.TakeChanged() == false);
    events = feed(learner, "7.2-walk.bin");
    CHECK(countOf(events, Kind::ObjectsNear, false) > 0);
    CHECK(countOf(events, Kind::PlayerMoved, false) > 0);
    CHECK(countOf(events, Kind::ObjectsNear, true) == 0);
}

TEST_CASE("The learner finds nothing wrong on any recording, with nothing known", "[learner]") {
    std::vector<std::filesystem::path> recordings;
    std::error_code error;
    for (auto& entry : std::filesystem::directory_iterator(RECORDINGS, error))
        if (std::filesystem::exists(entry.path() / "messages.bin"))
            recordings.push_back(entry.path());
    if (recordings.empty())
        SKIP("no recordings on this PC");
    std::sort(recordings.begin(), recordings.end());
    std::map<Kind, Learned> answers72 = {
        { Kind::ObjectsNear, { 4469, { 2, 2, 4, 13, 1, 2, 3 } } },
        { Kind::ItemGot, { 5662, { 5, 9, 1, 2 } } },
        { Kind::StateChanged, { 4303, { 2, 3 } } },
        { Kind::ObjectGone, { 7450, { 10, 13 } } },
        { Kind::Interact, { 1891, { 11, 7 } } },
        { Kind::InteractAnswer, { 20297, { 8, 2 } } },
        { Kind::ChestOpened, { 22094, { 1, 7 } } },
        { Kind::PlayerMoved, { 24007, { 12, 14, 14, 8, 1, 2 } } },
        { Kind::SceneChanged, { 27748, { 14 } } },
    };
    // one learner over all of 7.1 in a row, as on a PC that plays on
    Learner::Learner carried;
    std::vector<Learner::Event> events;
    for (const std::filesystem::path& recording : recordings) {
        INFO(recording.filename().string());
        std::vector<Messages::Message> messages = readMessages(recording / "messages.bin");
        Learner::Learner learner;
        for (const Messages::Message& m : messages)
            learner.Feed(m, events);
        if (learner.Version() == "7.2.0") {
            for (auto& [kind, learned] : learner.LearnedNow()) {
                INFO(Learner::KindCode(kind));
                REQUIRE(answers72.count(kind));
                CHECK(learned == answers72.at(kind));
            }
            CHECK(learner.IsLearned(Kind::ObjectsNear));
            // the player stands still all through 7.2-cpu
            if (recording.filename() != "2026-11-06_7.2-cpu")
                CHECK(learner.IsLearned(Kind::PlayerMoved));
            events.clear();
            continue;
        }
        CHECK((learner.Version() == "7.1.0" || learner.Version().empty()));
        for (auto& [kind, learned] : learner.LearnedNow()) {
            INFO(Learner::KindCode(kind));
            CHECK(learned == answers71().at(kind));
        }
        for (const Messages::Message& m : messages)
            carried.Feed(m, events);
        events.clear();
    }
    CHECK(carried.LearnedNow() == answers71());
}

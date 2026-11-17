#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "messages.hpp"

// Finds which message id is which kind in the game version that runs, by what
// the messages hold: gadget, group and config ids, item ids, the player's
// positions. Every update renumbers the messages and their fields, what is in
// them stays. A kind is learned after 3 hits that agree, and dropped when a
// message of its id stops fitting. Events only come from learned kinds; the
// messages before that wait up to 10 minutes and are read once it is learned.

namespace Learner {
    enum class Kind { ObjectsNear, ItemGot, StateChanged, ObjectGone, Interact, InteractAnswer, ChestOpened, PlayerMoved, SceneChanged, Count };
    inline constexpr int KINDS = int(Kind::Count);

    // objects_near and so on, the names in the learned file
    extern const char* KindCode(Kind kind);
    // Count for an unknown code
    extern Kind KindFromCode(std::string_view code);

    struct Vec3 {
        float x = 0, y = 0, z = 0;
    };

    // a learned kind: its id, and the field numbers its reader uses
    // (player moved: the path to the part with the player, then its two fields)
    struct Learned {
        uint16_t id = 0;
        std::vector<uint32_t> fields;
        bool operator==(const Learned& other) const { return id == other.id && fields == other.fields; }
    };
    using LearnedSet = std::map<Kind, Learned>;
    // per game version, "7.2.0"
    using Known = std::map<std::string, LearnedSet>;

    struct Object {
        uint64_t entity = 0;
        // 0 for entries that aren't gadgets (avatars, monsters)
        uint32_t gadget = 0;
        uint32_t group = 0, config = 0;
        bool hasPos = false;
        Vec3 pos;
        // the gadget state, 0 until the state field is learned
        uint32_t state = 0;
    };

    struct Event {
        Kind kind = Kind::Count;
        uint64_t ms = 0;
        // read from a message that waited for its kind to be learned
        bool replayed = false;
        // objects near
        std::vector<Object> objects;
        // state changed, object gone, interact, its answer, player moved
        uint64_t entity = 0;
        // interact and its answer
        uint32_t gadget = 0;
        // state changed: the new state; object gone: the reason
        uint32_t value = 0;
        // chest opened
        uint32_t group = 0, config = 0;
        // item got: where from, and (item, count)
        uint32_t source = 0;
        std::vector<std::pair<uint32_t, uint32_t>> items;
        // player moved, scene changed
        Vec3 pos;
    };

    // the ids the build ships, found on the test recordings
    extern Known Shipped();
    // the learned file's text and back, false when it doesn't read
    extern std::string ToJson(const Known& known);
    extern bool FromJson(const std::string& text, Known& out);

    class Learner {
    public:
        // known: what was learned before, per version ("" until a login says which)
        explicit Learner(Known known = {});

        // every message in order, its events go to out
        void Feed(const Messages::Message& message, std::vector<Event>& out);

        // from the login, empty until one was seen
        const std::string& Version() const { return m_version; }
        bool IsLearned(Kind kind) const { return m_learned.count(kind) > 0; }
        const LearnedSet& LearnedNow() const { return m_learned; }
        // everything learned so far, the current version's included
        Known All() const;
        // true once after something was learned or dropped
        bool TakeChanged();

    private:
        using Fields = std::vector<Messages::Field>;
        using Sig = std::vector<uint32_t>;
        struct Partner {
            bool any = false;
            Kind kind = Kind::Count;
            bool hasId = false;
            uint16_t id = 0;
        };
        struct Candidate {
            std::vector<uint64_t> hits;
            std::vector<Partner> partners;
        };
        struct WorldObject {
            uint32_t gadget = 0, group = 0, config = 0;
            bool hasPos = false;
            Vec3 pos;
            uint32_t state = 0;
            uint64_t stateMs = 0;
            bool changed = false;
        };
        struct Held {
            uint64_t ms;
            Frames::Direction direction;
            uint16_t id;
            std::string body;
        };
        struct Recent {
            uint64_t ms;
            Kind kind;
            bool hasId;
            uint16_t id;
            Sig sig;
        };
        struct PendingScene {
            uint64_t ms;
            uint32_t field;
            Vec3 pos;
            uint16_t id;
        };
        // a read: Contradiction when the message doesn't fit, Empty when it fits but says nothing
        enum class Read { Event, Empty, Contradiction };
        struct Found {
            Sig sig;
            uint64_t entity;
            Vec3 pos;
        };

        void newSession(const std::string& version);
        bool find(Kind kind, uint64_t ms, const Fields& fs, Sig& sig);
        Read read(Kind kind, uint64_t ms, const Fields& fs, const Sig& sig, Event& ev, std::vector<Fields>& raws);
        bool findObjects(const Fields& fs, Sig& sig);
        bool entrySig(const Fields& e, bool hasEntity, uint32_t entity, Sig& out);
        bool findInteract(uint64_t ms, const Fields& fs, bool answer, Sig& sig);
        void playerParts(const Fields& fs, bool learned, std::vector<Found>& out);
        void nodes(const Fields& fs, Sig& path, int depth, bool learned, std::vector<Found>& out);
        void hit(uint64_t ms, Kind kind, uint16_t id, const Sig& sig, Partner partner, std::vector<Event>& out);
        void hold(const Messages::Message& message);
        bool pair(uint64_t ms, Kind kind, bool hasId, uint16_t id, const Sig& sig, const Fields& fs, std::vector<Event>& out);
        void moved(uint64_t ms, const Vec3& pos, std::vector<Event>& out);
        bool isAvatar(uint64_t entity);
        void tallyStateField(const Object& o, const Fields& raw);
        bool lead(Kind kind, uint16_t id, const Sig& sig);
        size_t count(Kind kind, uint16_t id, const Sig& sig);
        void goneCheck(uint64_t ms, uint16_t id, const Fields& fs);
        void apply(Event& ev, const std::vector<Fields>& raws, std::vector<Event>& out);

        Known m_known;
        std::string m_version;
        LearnedSet m_learned;
        bool m_changed = false;
        std::map<std::pair<uint16_t, Sig>, Candidate> m_candidates[KINDS];
        std::map<std::pair<Kind, uint16_t>, int> m_contradicted;
        std::map<std::pair<uint16_t, uint64_t>, uint64_t> m_goneSeen;
        // entity: when it left, so an older message doesn't bring it back
        std::unordered_map<uint64_t, uint64_t> m_goneAt;
        uint64_t m_sceneMs = 0;
        std::deque<bool> m_strikes[KINDS];
        bool m_miss = false;
        // objects near: how many entries the last find saw
        size_t m_entries = 1;
        // object gone: the last find's entity had just been opened
        bool m_direct = false;

        std::unordered_map<uint64_t, WorldObject> m_world;
        std::unordered_set<uint64_t> m_others;
        bool m_entitiesChanged = true;
        uint64_t m_minEntity = 0;
        int m_shift = 0;
        uint32_t m_stateField = 0;
        std::vector<std::pair<uint32_t, int>> m_stateVotes;

        std::deque<Held> m_held;
        size_t m_heldBytes = 0;
        std::deque<Recent> m_recent;
        uint64_t m_loadingUntil = 0;
        bool m_started = false;
        std::pair<uint64_t, uint64_t> m_lastInteract{ 0, 0 }, m_interactLike{ 0, 0 };
        bool m_hasLastInteract = false, m_hasInteractLike = false;
        bool m_hasPlayer = false;
        Vec3 m_player;
        std::vector<PendingScene> m_pendingScene;
        uint16_t m_id = 0;
        std::map<std::pair<Sig, uint64_t>, std::pair<uint64_t, Vec3>> m_lastMoved;
    };
}

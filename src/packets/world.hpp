#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include "learner.hpp"

// What the game told us is around the player, from Learning's events only:
// the objects near by entity and by their placed (group, config), and where
// the player is. Nothing here reads messages or writes to disk.
//
// One reset rule: a scene entry (or a teleport's jump) drops everything heard
// before it. One cap: past CAP objects the farthest from the player go first.

namespace World {
    using Learner::Vec3;

    struct Object {
        uint64_t entity = 0;
        // 0 for entries that aren't gadgets
        uint32_t gadget = 0;
        // where it was placed, 0 and 0 for things that weren't (drops, monsters)
        uint32_t group = 0, config = 0;
        bool hasPos = false;
        Vec3 pos;
        // false until a state was heard (the learner gives none before it knows the state field)
        bool hasState = false;
        uint32_t state = 0;
        // the newest news about it
        uint64_t ms = 0;
    };

    enum class What { Appeared, StateChanged, Opened, Left, GotItem, PlayerMoved, SceneChanged, Interacted };

    struct Event {
        What what = What::Appeared;
        uint64_t ms = 0;
        // came from a message that waited for its kind to be learned
        bool replayed = false;
        // Appeared, StateChanged (with the new state), Opened, Left, Interacted (the
        // player interacted with it)
        Object object;
        // Opened: false when the world never saw it, then only group and config are set
        bool known = true;
        // StateChanged
        uint32_t oldState = 0;
        // Left: the game's reason
        uint32_t reason = 0;
        // GotItem: where from, and (item, count)
        uint32_t source = 0;
        std::vector<std::pair<uint32_t, uint32_t>> items;
        // PlayerMoved, SceneChanged: the player's game position
        Vec3 pos;
    };

    class World {
    public:
        static constexpr size_t CAP = 20000;

        void Feed(const Learner::Event& in, std::vector<Event>& out);

        // nullptr when it isn't around
        const Object* Find(uint64_t entity) const;
        const Object* FindPlaced(uint32_t group, uint32_t config) const;
        size_t Size() const { return m_objects.size(); }
        const std::unordered_map<uint64_t, Object>& Objects() const { return m_objects; }

        bool HasPlayer() const { return m_hasPlayer; }
        const Vec3& Player() const { return m_player; }
        // the last scene entry, 0 before the first
        uint64_t SceneMs() const { return m_sceneMs; }
        // about what it holds in memory
        size_t Bytes() const;

    private:
        void appear(const Learner::Object& o, uint64_t ms, bool replayed, std::vector<Event>& out);
        void leave(uint64_t entity, uint64_t ms);
        void unplace(const Object& o);
        void cap();
        void reset(uint64_t ms);
        void keep(const Object& o, uint64_t ms);
        bool stale(uint64_t entity, uint64_t ms) const;

        std::unordered_map<uint64_t, Object> m_objects;
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> m_placed;
        // entity: when it left, so an older message doesn't bring it back
        std::unordered_map<uint64_t, uint64_t> m_goneAt;
        // what went away lately by (group, config) and when, for an opening heard after it
        std::map<std::pair<uint32_t, uint32_t>, std::pair<Object, uint64_t>> m_past;
        bool m_hasPlayer = false;
        Vec3 m_player;
        uint64_t m_sceneMs = 0;
        uint64_t m_movedMs = 0;
    };

    // The one World for the run, fed by Learning. Start after Learning::Start.
    extern void Start();
    extern void Stop();

    struct Status {
        size_t objects = 0;
        bool hasPlayer = false;
        Vec3 player;
        uint64_t sceneMs = 0;
    };
    extern Status GetStatus();
    // runs see on the World under its lock, not at all while it isn't started; keep it short
    extern void Visit(const std::function<void(const World&)>& see);

    // Called on GameTraffic's worker thread for every event, keep it short.
    // Gives an id for RemoveListener.
    using Listener = std::function<void(const Event&)>;
    extern int AddListener(Listener listener);
    extern void RemoveListener(int id);
}

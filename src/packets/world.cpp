#include "world.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include "learning.hpp"

namespace World {
    using Learner::Kind;

    // lists of left entities are cleared at this size, they only guard against old messages
    static constexpr size_t GONE_CAP = 50000;
    static constexpr size_t PAST_CAP = 5000;
    // as long as the learner holds a message for its kind to be learned
    static constexpr uint64_t PAST_MS = 10 * 60 * 1000;
    // metres between two moves that only a teleport covers (moves come several times a second)
    static constexpr float JUMP = 100;

    static float dist2(const Vec3& a, const Vec3& b) {
        float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return x * x + y * y + z * z;
    }

    const Object* World::Find(uint64_t entity) const {
        auto o = m_objects.find(entity);
        return o == m_objects.end() ? nullptr : &o->second;
    }

    const Object* World::FindPlaced(uint32_t group, uint32_t config) const {
        auto p = m_placed.find({ group, config });
        return p == m_placed.end() ? nullptr : Find(p->second);
    }

    size_t World::Bytes() const {
        // the nodes and the buckets, roughly what the standard maps cost on MSVC
        return m_objects.size() * (sizeof(Object) + 40) + m_objects.bucket_count() * 8
             + m_placed.size() * 48 + m_goneAt.size() * 32 + m_goneAt.bucket_count() * 8
             + m_past.size() * (sizeof(Object) + 48);
    }

    // news older than what we know of the entity, or than its leaving or the scene entry
    bool World::stale(uint64_t entity, uint64_t ms) const {
        if (ms < m_sceneMs)
            return true;
        auto gone = m_goneAt.find(entity);
        if (gone != m_goneAt.end() && gone->second >= ms)
            return true;
        const Object* o = Find(entity);
        return o && o->ms > ms;
    }

    // drops o's (group, config) unless a newer entity holds it
    void World::unplace(const Object& o) {
        if (!o.group && !o.config)
            return;
        auto placed = m_placed.find({ o.group, o.config });
        if (placed != m_placed.end() && placed->second == o.entity)
            m_placed.erase(placed);
    }

    void World::appear(const Learner::Object& in, uint64_t ms, bool replayed, std::vector<Event>& out) {
        // avatars and monsters have neither, nothing marks them
        if (!in.gadget && !in.group)
            return;
        if (stale(in.entity, ms))
            return;
        auto had = m_objects.find(in.entity);
        bool fresh = had == m_objects.end();
        Object old = fresh ? Object() : had->second;
        Object& o = m_objects[in.entity];
        if (!fresh && (o.group != in.group || o.config != in.config))
            unplace(o);
        o = { in.entity, in.gadget, in.group, in.config, in.hasPos, in.pos, in.hasState, in.state, ms };
        // an entry without a position or a state keeps the ones heard before
        if (!o.hasPos && old.hasPos) {
            o.hasPos = true;
            o.pos = old.pos;
        }
        if (!o.hasState && old.hasState) {
            o.hasState = true;
            o.state = old.state;
        }
        if (o.group || o.config)
            m_placed[{ o.group, o.config }] = o.entity;
        Event ev;
        ev.ms = ms;
        ev.replayed = replayed;
        ev.object = o;
        if (fresh) {
            ev.what = What::Appeared;
            out.push_back(ev);
        } else if (old.hasState && in.hasState && old.state != o.state) {
            ev.what = What::StateChanged;
            ev.oldState = old.state;
            out.push_back(ev);
        }
    }

    void World::leave(uint64_t entity, uint64_t ms) {
        auto o = m_objects.find(entity);
        if (o != m_objects.end()) {
            const Object& left = o->second;
            unplace(left);
            keep(left, ms);
            m_objects.erase(o);
        }
        if (m_goneAt.size() >= GONE_CAP)
            m_goneAt.clear();
        uint64_t& at = m_goneAt[entity];
        at = std::max(at, ms);
    }

    // Past the cap the farthest from the player go, down to nine tenths so it
    // doesn't run on every new one. Without a player, the oldest news go.
    void World::cap() {
        if (m_objects.size() <= CAP)
            return;
        std::vector<std::pair<float, uint64_t>> order;
        order.reserve(m_objects.size());
        // smallest first goes: the farthest, the ones without a place before them
        for (auto& [entity, o] : m_objects) {
            float key;
            if (!m_hasPlayer)
                key = float(o.ms - m_sceneMs);
            else
                key = o.hasPos ? -dist2(o.pos, m_player) : -3e38f;
            order.push_back({ key, entity });
        }
        size_t drop = m_objects.size() - CAP * 9 / 10;
        std::nth_element(order.begin(), order.begin() + drop, order.end());
        for (size_t i = 0; i < drop; i++) {
            auto o = m_objects.find(order[i].second);
            unplace(o->second);
            m_objects.erase(o);
        }
    }

    // what went away by (group, config), for an opening heard late (a replay)
    void World::keep(const Object& o, uint64_t ms) {
        if (!o.group && !o.config)
            return;
        if (m_past.size() >= PAST_CAP)
            for (auto p = m_past.begin(); p != m_past.end();)
                p = p->second.second + PAST_MS < ms ? m_past.erase(p) : std::next(p);
        if (m_past.size() >= PAST_CAP)
            m_past.clear();
        m_past[{ o.group, o.config }] = { o, ms };
    }

    // the one reset rule: what was heard before a scene entry is gone
    void World::reset(uint64_t ms) {
        m_sceneMs = ms;
        for (auto o = m_objects.begin(); o != m_objects.end();) {
            if (o->second.ms >= ms) {
                ++o;
                continue;
            }
            keep(o->second, ms);
            o = m_objects.erase(o);
        }
        m_placed.clear();
        for (auto& [entity, o] : m_objects)
            if (o.group || o.config)
                m_placed[{ o.group, o.config }] = entity;
        for (auto g = m_goneAt.begin(); g != m_goneAt.end();)
            g = g->second < ms ? m_goneAt.erase(g) : std::next(g);
    }

    void World::Feed(const Learner::Event& in, std::vector<Event>& out) {
        Event ev;
        ev.ms = in.ms;
        ev.replayed = in.replayed;
        switch (in.kind) {
        case Kind::ObjectsNear:
            for (const Learner::Object& o : in.objects)
                appear(o, in.ms, in.replayed, out);
            cap();
            break;
        case Kind::StateChanged: {
            auto o = m_objects.find(in.entity);
            if (o == m_objects.end() || o->second.ms > in.ms || (o->second.hasState && o->second.state == in.value))
                break;
            ev.what = What::StateChanged;
            ev.oldState = o->second.state;
            o->second.hasState = true;
            o->second.state = in.value;
            o->second.ms = in.ms;
            ev.object = o->second;
            out.push_back(ev);
            break;
        }
        case Kind::ChestOpened: {
            ev.what = What::Opened;
            const Object* o = FindPlaced(in.group, in.config);
            auto past = m_past.find({ in.group, in.config });
            if (o)
                ev.object = *o;
            else if (past != m_past.end() && past->second.second + PAST_MS >= in.ms)
                ev.object = past->second.first;
            else {
                ev.known = false;
                ev.object.group = in.group;
                ev.object.config = in.config;
            }
            out.push_back(ev);
            break;
        }
        case Kind::ObjectGone: {
            const Object* o = Find(in.entity);
            if (o && o->ms > in.ms)
                break;
            if (o) {
                ev.what = What::Left;
                ev.object = *o;
                ev.reason = in.value;
                out.push_back(ev);
            }
            leave(in.entity, in.ms);
            break;
        }
        case Kind::Interact:
            if (const Object* o = Find(in.entity)) {
                ev.what = What::Interacted;
                ev.object = *o;
                out.push_back(ev);
            }
            break;
        case Kind::ItemGot:
            ev.what = What::GotItem;
            ev.source = in.source;
            ev.items = in.items;
            out.push_back(ev);
            break;
        case Kind::PlayerMoved:
            // a late one from before the last scene entry is somewhere else, one
            // from before the last move is where the player was
            if (in.ms < m_sceneMs || (m_hasPlayer && in.ms < m_movedMs))
                break;
            // a jump is a teleport, also before the scene kind is learned: the new
            // place loads between the last move and this one
            if (m_hasPlayer && dist2(in.pos, m_player) > JUMP * JUMP && m_movedMs > m_sceneMs)
                reset(m_movedMs + 1);
            m_hasPlayer = true;
            m_player = in.pos;
            m_movedMs = in.ms;
            ev.what = What::PlayerMoved;
            ev.pos = in.pos;
            out.push_back(ev);
            break;
        case Kind::SceneChanged: {
            if (in.ms < m_sceneMs)
                break;
            reset(in.ms);
            if (!m_hasPlayer || in.ms >= m_movedMs) {
                m_hasPlayer = true;
                m_player = in.pos;
            }
            ev.what = What::SceneChanged;
            ev.pos = in.pos;
            out.push_back(ev);
            break;
        }
        default:
            break;
        }
    }

    static std::mutex mutex;
    static std::shared_ptr<World> world;
    static int learningListener = 0;

    static std::mutex listenersMutex;
    static std::vector<std::pair<int, Listener>> listeners;
    static int nextListener = 1;

    void Start() {
        std::lock_guard lock(mutex);
        if (learningListener)
            return;
        world = std::make_shared<World>();
        auto events = std::make_shared<std::vector<Event>>();
        learningListener = Learning::AddListener([events](const Learner::Event& in) {
            {
                std::lock_guard lock(mutex);
                if (!world)
                    return;
                world->Feed(in, *events);
            }
            if (events->empty())
                return;
            std::vector<std::pair<int, Listener>> now;
            {
                std::lock_guard lock(listenersMutex);
                now = listeners;
            }
            for (const Event& ev : *events)
                for (auto& [id, listener] : now)
                    listener(ev);
            events->clear();
        });
    }

    void Stop() {
        int id = 0;
        {
            std::lock_guard lock(mutex);
            id = learningListener;
            learningListener = 0;
            world.reset();
        }
        if (id)
            Learning::RemoveListener(id);
    }

    Status GetStatus() {
        std::lock_guard lock(mutex);
        Status s;
        if (!world)
            return s;
        s.objects = world->Size();
        s.hasPlayer = world->HasPlayer();
        s.player = world->Player();
        s.sceneMs = world->SceneMs();
        return s;
    }

    void Visit(const std::function<void(const World&)>& see) {
        std::lock_guard lock(mutex);
        if (world)
            see(*world);
    }

    int AddListener(Listener listener) {
        std::lock_guard lock(listenersMutex);
        int id = nextListener++;
        listeners.push_back({ id, std::move(listener) });
        return id;
    }

    void RemoveListener(int id) {
        std::lock_guard lock(listenersMutex);
        for (size_t i = 0; i < listeners.size(); i++)
            if (listeners[i].first == id) {
                listeners.erase(listeners.begin() + i);
                return;
            }
    }
}

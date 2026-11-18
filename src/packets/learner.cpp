#include "learner.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>

namespace Learner {
    static constexpr size_t NEED = 3;
    static constexpr int64_t HOLD_MS = 10 * 60 * 1000;
    static constexpr size_t HOLD_BYTES = 16 << 20;
    // the game's loading (the start, a scene entry) sends everything in one go
    static constexpr int64_t LOADING_MS = 3000;
    // an item and the object it came from arrive within this
    static constexpr int64_t PAIR_MS = 50;
    static constexpr size_t MAX_CANDIDATES = 20000, MAX_HITS = 1000;
    // a scene entry to the player's first move there, the loading screen in between
    static constexpr int64_t ARRIVE_MS = 15000;

    static const char* CODES[KINDS] = { "objects_near", "item_got", "state_changed", "object_gone", "interact", "interact_answer", "chest_opened", "player_moved", "scene_changed" };

    const char* KindCode(Kind kind) {
        return kind < Kind::Count ? CODES[int(kind)] : "";
    }

    Kind KindFromCode(std::string_view code) {
        for (int k = 0; k < KINDS; k++)
            if (code == CODES[k])
                return Kind(k);
        return Kind::Count;
    }

    static Frames::Direction directionOf(Kind kind) {
        return kind == Kind::Interact || kind == Kind::PlayerMoved ? Frames::Direction::Sent : Frames::Direction::Received;
    }

    static bool paired(Kind kind) { return kind == Kind::ItemGot || kind == Kind::ObjectGone; }

    // the game's own number ranges
    static bool isGadget(uint64_t v) { return v >= 70000000 && v <= 79999999; }
    static bool isItem(uint64_t v) { return v >= 100 && v <= 999999; }
    // GadgetState: default, chests, gears, crystals
    static bool isState(uint64_t v) {
        static const uint64_t states[] = { 0, 101, 102, 103, 104, 105, 106, 201, 202, 203, 204, 301, 302, 303 };
        return std::find(std::begin(states), std::end(states), v) != std::end(states);
    }
    // the entity's type sits in its top bits, which moved between versions
    static bool isEntity(uint64_t v) { return v >= (1ull << 20) && v < (1ull << 32); }

    // a config id starts with its group's last digits: group 133102803, config 803493
    static bool groupConfig(uint64_t group, uint64_t config) {
        uint64_t k = config / 1000;
        if (group < 100000000 || group >= 1000000000 || k == 0)
            return false;
        uint64_t ten = 1;
        while (ten <= k)
            ten *= 10;
        return group % ten == k;
    }

    using Fields = std::vector<Messages::Field>;

    static bool parse(std::string_view data, Fields& out) {
        out.clear();
        Messages::Field field;
        while (!data.empty()) {
            if (!Messages::NextField(data, field))
                return false;
            out.push_back(field);
        }
        return true;
    }

    // bytes as a message with at least one field
    static bool sub(std::string_view data, Fields& out) {
        return !data.empty() && parse(data, out) && !out.empty();
    }

    static const Messages::Field* one(const Fields& fs, uint32_t number, int wire = 0) {
        for (const Messages::Field& f : fs)
            if (f.number == number && f.wire == wire)
                return &f;
        return nullptr;
    }

    static uint64_t valueOr0(const Fields& fs, uint32_t number) {
        const Messages::Field* f = one(fs, number);
        return f ? f->value : 0;
    }

    // {1 x, 2 y, 3 z} as floats, a missing one is 0
    static bool vec3(std::string_view data, Vec3& out) {
        Fields fs;
        if (!sub(data, fs))
            return false;
        float xyz[3] = { 0, 0, 0 };
        for (const Messages::Field& f : fs) {
            if (f.number < 1 || f.number > 3 || f.wire != 5)
                return false;
            uint32_t bits = uint32_t(f.value);
            std::memcpy(&xyz[f.number - 1], &bits, 4);
        }
        for (float a : xyz)
            if (std::isnan(a) || std::fabs(a) > 1e5f)
                return false;
        out = { xyz[0], xyz[1], xyz[2] };
        return true;
    }

    static float dist(const Vec3& a, const Vec3& b) {
        float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    static int64_t since(uint64_t now, uint64_t then) { return int64_t(now) - int64_t(then); }

    // "OSRELWin7.2.0" in the login, the version without the rest
    static bool findVersion(std::string_view body, std::string& out) {
        for (size_t at = body.find("OS"); at != std::string_view::npos; at = body.find("OS", at + 1)) {
            // the letters between OS and Win, at least one
            size_t i = at + 3;
            while (i < body.size() && body[i - 1] >= 'A' && body[i - 1] <= 'Z' && body.compare(i, 3, "Win") != 0)
                i++;
            if (i >= body.size() || body[i - 1] < 'A' || body[i - 1] > 'Z' || body.compare(i, 3, "Win") != 0)
                continue;
            i += 3;
            size_t start = i;
            int parts = 0;
            while (parts < 3) {
                size_t digits = i;
                while (i < body.size() && body[i] >= '0' && body[i] <= '9')
                    i++;
                if (i == digits)
                    break;
                if (++parts < 3) {
                    if (i >= body.size() || body[i] != '.')
                        break;
                    i++;
                }
            }
            if (parts == 3) {
                out = std::string(body.substr(start, i - start));
                return true;
            }
        }
        return false;
    }

    Known Shipped() {
        Known known;
        LearnedSet& v71 = known["7.1.0"];
        v71[Kind::ObjectsNear] = { 27685, { 8, 2, 4, 13, 1, 2, 3 } };
        v71[Kind::ItemGot] = { 652, { 11, 13, 1, 2 } };
        v71[Kind::StateChanged] = { 22292, { 3, 7 } };
        v71[Kind::ObjectGone] = { 24935, { 5, 11 } };
        v71[Kind::Interact] = { 5765, { 11, 4 } };
        v71[Kind::InteractAnswer] = { 881, { 15, 3 } };
        v71[Kind::ChestOpened] = { 28616, { 12, 11 } };
        v71[Kind::PlayerMoved] = { 2246, { 8, 6, 9, 15, 1, 2 } };
        v71[Kind::SceneChanged] = { 9582, { 8 } };
        LearnedSet& v72 = known["7.2.0"];
        v72[Kind::ObjectsNear] = { 4469, { 2, 2, 4, 13, 1, 2, 3 } };
        v72[Kind::ItemGot] = { 5662, { 5, 9, 1, 2 } };
        v72[Kind::StateChanged] = { 4303, { 2, 3 } };
        v72[Kind::ObjectGone] = { 7450, { 10, 13 } };
        v72[Kind::Interact] = { 1891, { 11, 7 } };
        v72[Kind::InteractAnswer] = { 20297, { 8, 2 } };
        v72[Kind::ChestOpened] = { 22094, { 1, 7 } };
        v72[Kind::PlayerMoved] = { 24007, { 12, 14, 14, 8, 1, 2 } };
        v72[Kind::SceneChanged] = { 27748, { 14 } };
        return known;
    }

    std::string ToJson(const Known& known) {
        nlohmann::json all = nlohmann::json::object();
        for (const auto& [version, set] : known) {
            nlohmann::json kinds = nlohmann::json::object();
            for (const auto& [kind, learned] : set)
                kinds[KindCode(kind)] = { { "id", learned.id }, { "fields", learned.fields } };
            all[version] = kinds;
        }
        return all.dump(1);
    }

    bool FromJson(const std::string& text, Known& out) {
        nlohmann::json all = nlohmann::json::parse(text, nullptr, false);
        if (!all.is_object())
            return false;
        Known known;
        for (const auto& [version, kinds] : all.items()) {
            if (!kinds.is_object())
                return false;
            LearnedSet& set = known[version];
            for (const auto& [code, learned] : kinds.items()) {
                Kind kind = KindFromCode(code);
                if (kind == Kind::Count || !learned.is_object())
                    continue;
                const nlohmann::json& id = learned.value("id", nlohmann::json());
                const nlohmann::json& fields = learned.value("fields", nlohmann::json());
                if (!id.is_number_unsigned() || id.get<uint64_t>() > 0xffff || !fields.is_array())
                    return false;
                Learned l;
                l.id = id.get<uint16_t>();
                for (const nlohmann::json& f : fields) {
                    if (!f.is_number_unsigned())
                        return false;
                    l.fields.push_back(f.get<uint32_t>());
                }
                set[kind] = std::move(l);
            }
        }
        out = std::move(known);
        return true;
    }

    Learner::Learner(Known known) : m_known(std::move(known)) {
        auto found = m_known.find("");
        if (found != m_known.end())
            m_learned = found->second;
    }

    Known Learner::All() const {
        Known all = m_known;
        if (!m_version.empty())
            all[m_version] = m_learned;
        return all;
    }

    bool Learner::TakeChanged() {
        bool changed = m_changed;
        m_changed = false;
        return changed;
    }

    void Learner::newSession(const std::string& version) {
        if (version != m_version) {
            if (!m_version.empty())
                m_known[m_version] = m_learned;
            m_version = version;
            auto found = m_known.find(version);
            m_learned = found != m_known.end() ? found->second : LearnedSet();
            for (auto& candidates : m_candidates)
                candidates.clear();
            for (auto& strikes : m_strikes)
                strikes.clear();
            m_contradicted.clear();
            m_stateField = 0;
            m_stateVotes.clear();
        }
        m_goneSeen.clear();
        m_goneAt.clear();
        m_held.clear();
        m_heldBytes = 0;
        m_sceneMs = 0;
        m_world.clear();
        m_others.clear();
        m_entitiesChanged = true;
        m_recent.clear();
        m_pendingScene.clear();
        m_lastMoved.clear();
        m_hasLastInteract = m_hasInteractLike = m_hasPlayer = false;
        m_started = false;
    }

    void Learner::Feed(const Messages::Message& message, std::vector<Event>& out) {
        uint64_t t = message.ms;
        std::string version;
        if (message.direction == Frames::Direction::Sent && findVersion(message.body, version))
            newSession(version);
        Fields fs;
        if (!parse(message.body, fs))
            return;
        if (!m_started) {
            m_started = true;
            m_loadingUntil = t + LOADING_MS;
        }
        hold(message);
        m_id = message.id;
        if (message.direction == Frames::Direction::Received && !IsLearned(Kind::ObjectGone))
            goneCheck(t, message.id, fs);

        for (int k = 0; k < KINDS; k++) {
            Kind kind = Kind(k);
            if (directionOf(kind) != message.direction)
                continue;
            auto learned = m_learned.find(kind);
            if (learned != m_learned.end()) {
                if (learned->second.id != message.id)
                    continue;
                m_miss = false;
                Event ev;
                std::vector<Fields> raws;
                Read r = read(kind, t, fs, learned->second.fields, ev, raws);
                if (r == Read::Empty)
                    continue;
                if (r == Read::Event) {
                    // dropped when most of its last 20 hold numbers out of the game's ranges
                    std::deque<bool>& strikes = m_strikes[k];
                    strikes.push_back(m_miss);
                    if (strikes.size() > 20)
                        strikes.pop_front();
                    if (strikes.size() == 20 && std::count(strikes.begin(), strikes.end(), true) > 10)
                        r = Read::Contradiction;
                }
                if (r == Read::Contradiction) {
                    // learning the same id again takes new hits
                    for (auto c = m_candidates[k].begin(); c != m_candidates[k].end();)
                        c = c->first.first == message.id ? m_candidates[k].erase(c) : std::next(c);
                    m_learned.erase(learned);
                    m_strikes[k].clear();
                    m_changed = true;
                    continue;
                }
                apply(ev, raws, out);
                if (paired(kind))
                    pair(t, kind, false, 0, {}, fs, out);
                continue;
            }
            Sig sig;
            if (!find(kind, t, fs, sig))
                continue;
            if (paired(kind)) {
                // a just opened object's gone counts once, with its partner or on its own
                bool direct = kind == Kind::ObjectGone && m_direct;
                if (pair(t, kind, true, message.id, sig, fs, out) || !direct)
                    continue;
            }
            hit(t, kind, message.id, sig, {}, out);
        }
    }

    bool Learner::find(Kind kind, uint64_t t, const Fields& fs, Sig& sig) {
        switch (kind) {
        case Kind::ObjectsNear:
            return findObjects(fs, sig);
        case Kind::ItemGot: {
            // one small number (where from) and a part with an item and its count
            const Messages::Field* top = nullptr;
            int tops = 0;
            for (const Messages::Field& f : fs)
                if (f.wire == 0 && f.value < 5000) {
                    top = &f;
                    tops++;
                }
            if (tops != 1 || fs.size() > 6)
                return false;
            Fields s;
            for (const Messages::Field& f : fs) {
                if (f.wire != 2 || !sub(f.bytes, s))
                    continue;
                const Messages::Field* item = nullptr;
                for (const Messages::Field& x : s)
                    if (x.wire == 0 && isItem(x.value)) {
                        item = &x;
                        break;
                    }
                if (!item)
                    continue;
                for (const Messages::Field& x : s) {
                    if (x.wire != 0 || x.value == 0 || x.value > 100000)
                        continue;
                    bool itemField = false;
                    for (const Messages::Field& y : s)
                        if (y.wire == 0 && y.number == x.number && isItem(y.value))
                            itemField = true;
                    if (!itemField) {
                        sig = { top->number, f.number, item->number, x.number };
                        return true;
                    }
                }
            }
            return false;
        }
        case Kind::StateChanged: {
            // one entity the world has and one state it isn't in
            const Messages::Field* entity = nullptr;
            int entities = 0;
            for (const Messages::Field& f : fs)
                if (f.wire == 0 && m_world.count(f.value)) {
                    entity = &f;
                    entities++;
                }
            if (entities != 1)
                return false;
            auto found = m_world.find(entity->value);
            if (found == m_world.end())
                return false;
            const WorldObject& o = found->second;
            const Messages::Field* state = nullptr;
            int states = 0;
            for (const Messages::Field& f : fs)
                if (f.wire == 0 && f.number != entity->number && isState(f.value) && f.value != o.state) {
                    state = &f;
                    states++;
                }
            if (states != 1)
                return false;
            sig = { entity->number, state->number };
            return true;
        }
        case Kind::ObjectGone: {
            // a gadget the world has and a small reason
            const Messages::Field* entity = nullptr;
            int entities = 0;
            for (const Messages::Field& f : fs) {
                auto o = f.wire == 0 ? m_world.find(f.value) : m_world.end();
                if (o != m_world.end() && o->second.gadget) {
                    entity = &f;
                    entities++;
                }
            }
            if (entities != 1)
                return false;
            auto found = m_world.find(entity->value);
            if (found == m_world.end())
                return false;
            const WorldObject& o = found->second;
            // 102: opened, it leaves without an item of its own
            m_direct = o.state == 102 && since(t, o.stateMs) <= 2000;
            const Messages::Field* reason = nullptr;
            int reasons = 0;
            for (const Messages::Field& f : fs)
                if (f.wire == 0 && f.number != entity->number && f.value > 0 && f.value < 20) {
                    reason = &f;
                    reasons++;
                }
            if (reasons != 1)
                return false;
            sig = { entity->number, reason->number };
            return true;
        }
        case Kind::Interact:
            return findInteract(t, fs, false, sig);
        case Kind::InteractAnswer: {
            // answers what was just sent, learned or not yet
            std::pair<bool, std::pair<uint64_t, uint64_t>> asked[] = { { m_hasLastInteract, m_lastInteract }, { m_hasInteractLike, m_interactLike } };
            for (auto& [has, interact] : asked) {
                if (!has || since(t, interact.first) > 1000)
                    continue;
                if (findInteract(t, fs, true, sig) && one(fs, sig[0])->value == interact.second)
                    return true;
            }
            return false;
        }
        case Kind::ChestOpened: {
            // the group and config of what the player opened, right after
            if (!m_hasLastInteract || since(t, m_lastInteract.first) > 2000)
                return false;
            auto o = m_world.find(m_lastInteract.second);
            if (o == m_world.end() || !o->second.group)
                return false;
            const Messages::Field *group = nullptr, *config = nullptr;
            int groups = 0, configs = 0;
            for (const Messages::Field& f : fs) {
                if (f.wire != 0)
                    continue;
                if (f.value == o->second.group) {
                    group = &f;
                    groups++;
                }
                if (f.value == o->second.config) {
                    config = &f;
                    configs++;
                }
            }
            if (groups != 1 || configs != 1)
                return false;
            sig = { group->number, config->number };
            return true;
        }
        case Kind::PlayerMoved: {
            // the player's avatar moving a bit at a time
            std::vector<Found> found;
            playerParts(fs, false, found);
            for (const Found& f : found) {
                auto key = std::make_pair(f.sig, f.entity);
                auto last = m_lastMoved.find(key);
                bool smooth = false;
                if (last != m_lastMoved.end()) {
                    int64_t gap = since(t, last->second.first);
                    float d = dist(f.pos, last->second.second);
                    smooth = gap >= 50 && gap <= 1000 && d > 0.05f && d < 15;
                }
                if (m_lastMoved.size() > 10000)
                    m_lastMoved.clear();
                m_lastMoved[key] = { t, f.pos };
                if (smooth) {
                    sig = f.sig;
                    return true;
                }
            }
            return false;
        }
        case Kind::SceneChanged: {
            // where the player is and a place far away, each a field of its own (not a list):
            // counts once the player shows up there (moved)
            if (!m_hasPlayer)
                return false;
            std::vector<std::pair<uint32_t, Vec3>> places;
            std::map<uint32_t, int> times;
            Vec3 p;
            for (const Messages::Field& f : fs)
                if (f.wire == 2 && vec3(f.bytes, p)) {
                    places.push_back({ f.number, p });
                    times[f.number]++;
                }
            uint32_t here = 0;
            for (auto& [number, pos] : places)
                if (times[number] == 1 && dist(pos, m_player) < 20)
                    here = number;
            if (!here)
                return false;
            for (auto& [number, pos] : places)
                if (number != here && times[number] == 1 && dist(pos, m_player) > 50)
                    m_pendingScene.push_back({ t, number, pos, m_id });
            return false;
        }
        default:
            return false;
        }
    }

    bool Learner::findObjects(const Fields& fs, Sig& sig) {
        // the lists in the message, each entry a part of its own
        std::vector<std::pair<uint32_t, std::vector<Fields>>> lists;
        Fields e;
        for (const Messages::Field& f : fs) {
            if (f.wire != 2 || !sub(f.bytes, e))
                continue;
            auto list = std::find_if(lists.begin(), lists.end(), [&](auto& l) { return l.first == f.number; });
            if (list == lists.end()) {
                lists.push_back({ f.number, {} });
                list = lists.end() - 1;
            }
            list->second.push_back(e);
        }
        std::vector<std::pair<Sig, size_t>> sigs;
        for (auto& [number, entries] : lists) {
            // the entity field: big and different in every entry
            bool hasEntity = false;
            uint32_t entity = 0;
            if (entries.size() >= 2) {
                std::set<uint32_t> numbers;
                for (const Fields& entry : entries)
                    for (const Messages::Field& f : entry)
                        if (f.wire == 0)
                            numbers.insert(f.number);
                for (uint32_t n : numbers) {
                    std::unordered_set<uint64_t> seen;
                    bool all = true;
                    for (const Fields& entry : entries) {
                        const Messages::Field* f = one(entry, n);
                        if (!f || !isEntity(f->value) || !seen.insert(f->value).second) {
                            all = false;
                            break;
                        }
                    }
                    if (all) {
                        hasEntity = true;
                        entity = n;
                        break;
                    }
                }
            }
            for (const Fields& entry : entries) {
                Sig s;
                if (!entrySig(entry, hasEntity, entity, s))
                    continue;
                s.insert(s.begin(), number);
                auto known = std::find_if(sigs.begin(), sigs.end(), [&](auto& x) { return x.first == s; });
                if (known == sigs.end())
                    sigs.push_back({ s, 1 });
                else
                    known->second++;
            }
        }
        if (sigs.empty())
            return false;
        auto best = sigs.begin();
        for (auto i = sigs.begin(); i != sigs.end(); i++)
            if (i->second > best->second)
                best = i;
        sig = best->first;
        m_entries = best->second;
        return true;
    }

    // an entry: its entity, a part with its position, and a part with a gadget, group and config
    bool Learner::entrySig(const Fields& e, bool hasEntity, uint32_t entity, Sig& out) {
        if (!hasEntity) {
            int big = 0;
            for (const Messages::Field& f : e)
                if (f.wire == 0 && isEntity(f.value)) {
                    entity = f.number;
                    big++;
                }
            if (big != 1)
                return false;
        }
        Fields s;
        Vec3 p;
        bool hasPos = false;
        uint32_t pos = 0;
        for (const Messages::Field& f : e) {
            if (f.wire != 2 || !sub(f.bytes, s))
                continue;
            const Messages::Field* xyz = one(s, 1, 2);
            if (xyz && vec3(xyz->bytes, p)) {
                hasPos = true;
                pos = f.number;
                break;
            }
        }
        for (const Messages::Field& f : e) {
            if (f.wire != 2 || !sub(f.bytes, s))
                continue;
            const Messages::Field* gadget = nullptr;
            for (const Messages::Field& x : s)
                if (x.wire == 0 && isGadget(x.value)) {
                    gadget = &x;
                    break;
                }
            const Messages::Field *group = nullptr, *config = nullptr;
            for (const Messages::Field& a : s) {
                for (const Messages::Field& b : s)
                    if (a.wire == 0 && b.wire == 0 && a.number != b.number && groupConfig(a.value, b.value)) {
                        group = &a;
                        config = &b;
                        break;
                    }
                if (group)
                    break;
            }
            if (gadget && group && hasPos) {
                out = { entity, pos, f.number, gadget->number, group->number, config->number };
                return true;
            }
        }
        return false;
    }

    // an entity the world has and that object's gadget id
    bool Learner::findInteract(uint64_t t, const Fields& fs, bool answer, Sig& sig) {
        for (const Messages::Field& f : fs) {
            if (f.wire != 0)
                continue;
            auto o = m_world.find(f.value);
            if (o == m_world.end() || !o->second.gadget)
                continue;
            const Messages::Field* gadget = nullptr;
            int gadgets = 0;
            for (const Messages::Field& g : fs)
                if (g.wire == 0 && g.value == o->second.gadget) {
                    gadget = &g;
                    gadgets++;
                }
            if (gadgets == 1) {
                if (!answer) {
                    m_hasInteractLike = true;
                    m_interactLike = { t, f.value };
                }
                sig = { f.number, gadget->number };
                return true;
            }
        }
        return false;
    }

    // every part, 6 deep, with one of the player's avatars and a position
    void Learner::playerParts(const Fields& fs, bool learned, std::vector<Found>& out) {
        Sig path;
        nodes(fs, path, 0, learned, out);
    }

    void Learner::nodes(const Fields& fs, Sig& path, int depth, bool learned, std::vector<Found>& out) {
        const Messages::Field* entity = nullptr;
        int entities = 0;
        for (const Messages::Field& f : fs)
            if (f.wire == 0 && (learned || m_others.count(f.value)) && isEntity(f.value) && isAvatar(f.value)) {
                entity = &f;
                entities++;
            }
        if (entities == 1) {
            Fields s;
            Vec3 p;
            for (const Messages::Field& f : fs) {
                if (f.wire != 2 || !sub(f.bytes, s))
                    continue;
                const Messages::Field* xyz = one(s, 1, 2);
                if (!xyz || !vec3(xyz->bytes, p))
                    continue;
                if (std::fabs(p.x) > 1 || std::fabs(p.y) > 1 || std::fabs(p.z) > 1) {
                    Sig sig = path;
                    sig.push_back(entity->number);
                    sig.push_back(f.number);
                    out.push_back({ sig, entity->value, p });
                }
            }
        }
        if (depth >= 6)
            return;
        Fields s;
        for (const Messages::Field& f : fs) {
            if (f.wire != 2 || !sub(f.bytes, s))
                continue;
            path.push_back(f.number);
            nodes(s, path, depth + 1, learned, out);
            path.pop_back();
        }
    }

    Learner::Read Learner::read(Kind kind, uint64_t t, const Fields& fs, const Sig& sig, Event& ev, std::vector<Fields>& raws) {
        ev = Event();
        ev.kind = kind;
        ev.ms = t;
        switch (kind) {
        case Kind::ObjectsNear: {
            if (sig.size() != 7)
                return Read::Contradiction;
            int known = 0, unknown = 0;
            for (const Messages::Field& f : fs) {
                if (f.number != sig[0] || f.wire != 2)
                    continue;
                Fields e;
                if (!sub(f.bytes, e))
                    return Read::Contradiction;
                Object o;
                o.entity = valueOr0(e, sig[1]);
                if (!o.entity)
                    return Read::Contradiction;
                Fields p;
                const Messages::Field* part = one(e, sig[2], 2);
                if (part && sub(part->bytes, p)) {
                    const Messages::Field* xyz = one(p, 1, 2);
                    o.hasPos = xyz && vec3(xyz->bytes, o.pos);
                }
                Fields g;
                part = one(e, sig[3], 2);
                if (part && sub(part->bytes, g)) {
                    const Messages::Field* gadget = one(g, sig[4]);
                    o.group = uint32_t(valueOr0(g, sig[5]));
                    o.config = uint32_t(valueOr0(g, sig[6]));
                    if (gadget) {
                        o.gadget = uint32_t(gadget->value);
                        // some level gadgets are out of the usual range, their group and config still fit
                        if (isGadget(gadget->value) || groupConfig(o.group, o.config))
                            known++;
                        else
                            unknown++;
                    }
                    raws.push_back(std::move(g));
                } else {
                    raws.push_back({});
                    m_others.insert(o.entity);
                    m_entitiesChanged = true;
                }
                ev.objects.push_back(o);
            }
            if (unknown > known)
                m_miss = true;
            return ev.objects.empty() ? Read::Contradiction : Read::Event;
        }
        case Kind::ItemGot: {
            if (sig.size() != 4)
                return Read::Contradiction;
            Fields s;
            for (const Messages::Field& f : fs) {
                if (f.number != sig[1] || f.wire != 2)
                    continue;
                if (!sub(f.bytes, s))
                    s.clear();
                const Messages::Field* item = one(s, sig[2]);
                if (!item)
                    return Read::Contradiction;
                if (!isItem(item->value))
                    m_miss = true;
                ev.items.push_back({ uint32_t(item->value), uint32_t(valueOr0(s, sig[3])) });
            }
            ev.source = uint32_t(valueOr0(fs, sig[0]));
            return ev.items.empty() ? Read::Contradiction : Read::Event;
        }
        case Kind::StateChanged: {
            if (sig.size() != 2)
                return Read::Contradiction;
            const Messages::Field* entity = one(fs, sig[0]);
            if (!entity)
                return Read::Contradiction;
            ev.entity = entity->value;
            ev.value = uint32_t(valueOr0(fs, sig[1]));
            // states the list doesn't have exist too (901, 903), the entity is the check
            if (!m_world.count(ev.entity) && !m_others.count(ev.entity))
                m_miss = true;
            return Read::Event;
        }
        case Kind::ObjectGone:
        case Kind::Interact:
        case Kind::InteractAnswer: {
            if (sig.size() != 2)
                return Read::Contradiction;
            ev.entity = valueOr0(fs, sig[0]);
            if (!ev.entity)
                return Read::Contradiction;
            if (kind == Kind::ObjectGone)
                ev.value = uint32_t(valueOr0(fs, sig[1]));
            else
                ev.gadget = uint32_t(valueOr0(fs, sig[1]));
            return Read::Event;
        }
        case Kind::ChestOpened: {
            if (sig.size() != 2)
                return Read::Contradiction;
            const Messages::Field* config = one(fs, sig[1]);
            ev.group = uint32_t(valueOr0(fs, sig[0]));
            if (!ev.group || !config)
                return Read::Contradiction;
            ev.config = uint32_t(config->value);
            return Read::Event;
        }
        case Kind::PlayerMoved: {
            std::vector<Found> found;
            playerParts(fs, true, found);
            for (const Found& f : found)
                if (f.sig == sig) {
                    ev.entity = f.entity;
                    ev.pos = f.pos;
                    return Read::Event;
                }
            // most of these messages carry other things
            return Read::Empty;
        }
        case Kind::SceneChanged: {
            if (sig.size() != 1)
                return Read::Contradiction;
            const Messages::Field* pos = one(fs, sig[0], 2);
            if (!pos || !vec3(pos->bytes, ev.pos))
                return Read::Contradiction;
            return Read::Event;
        }
        default:
            return Read::Contradiction;
        }
    }

    void Learner::hit(uint64_t t, Kind kind, uint16_t id, const Sig& sig, Partner partner, std::vector<Event>& out) {
        if (IsLearned(kind))
            return;
        auto& candidates = m_candidates[int(kind)];
        if (candidates.size() > MAX_CANDIDATES)
            candidates.clear();
        Candidate& c = candidates[{ id, sig }];
        if (c.hits.size() >= MAX_HITS)
            return;
        // objects near: every entry counts, from 2 messages, or 10 in one (the login's list)
        size_t entries = kind == Kind::ObjectsNear ? m_entries : 1;
        for (size_t i = 0; i < entries; i++)
            c.hits.push_back(t);
        c.partners.push_back(partner);
        bool enough = c.hits.size() >= NEED;
        if (enough && kind == Kind::ObjectsNear && c.hits.size() < 10)
            enough = std::any_of(c.hits.begin(), c.hits.end(), [&](uint64_t h) { return h != c.hits.front(); });
        if (!enough || !lead(kind, id, sig))
            return;
        m_learned[kind] = { id, sig };
        m_changed = true;
        // what waited is read now, in order
        Fields hf;
        Event ev;
        std::vector<Fields> raws;
        for (const Held& h : m_held) {
            if (h.id != id || h.direction != directionOf(kind) || !parse(h.body, hf))
                continue;
            raws.clear();
            if (read(kind, h.ms, hf, sig, ev, raws) != Read::Event)
                continue;
            ev.replayed = true;
            apply(ev, raws, out);
        }
    }

    // messages wait up to 10 minutes for their kind
    void Learner::hold(const Messages::Message& message) {
        bool waiting = false;
        for (int k = 0; k < KINDS; k++)
            if (Kind(k) != Kind::SceneChanged && !m_learned.count(Kind(k)))
                waiting = true;
        if (!waiting) {
            m_held.clear();
            m_heldBytes = 0;
            return;
        }
        // a learned id is read as it comes
        for (auto& [kind, learned] : m_learned)
            if (learned.id == message.id && directionOf(kind) == message.direction)
                return;
        m_held.push_back({ message.ms, message.direction, message.id, message.body });
        m_heldBytes += message.body.size();
        while (!m_held.empty() && (since(message.ms, m_held.front().ms) > HOLD_MS || m_heldBytes > HOLD_BYTES)) {
            m_heldBytes -= m_held.front().body.size();
            m_held.pop_front();
        }
    }

    // an item and a gone object in the same tick count for each other, once the
    // player is in the world and not while it loads
    bool Learner::pair(uint64_t t, Kind kind, bool hasId, uint16_t id, const Sig& sig, const Fields& fs, std::vector<Event>& out) {
        if (!m_hasPlayer || int64_t(t) < int64_t(m_loadingUntil))
            return false;
        if (kind == Kind::ObjectGone && hasId) {
            if (m_goneSeen.size() > 50000)
                m_goneSeen.clear();
            m_goneSeen[{ id, valueOr0(fs, sig[0]) }] = t;
        }
        while (!m_recent.empty() && since(t, m_recent.front().ms) > PAIR_MS)
            m_recent.pop_front();
        for (auto r = m_recent.begin(); r != m_recent.end(); r++) {
            if (r->kind == kind)
                continue;
            Recent other = *r;
            m_recent.erase(r);
            // each hit remembers its partner: a partner contradicted takes the hit back
            if (other.hasId)
                hit(other.ms, other.kind, other.id, other.sig, { true, kind, hasId, id }, out);
            if (hasId)
                hit(t, kind, id, sig, { true, other.kind, other.hasId, other.id }, out);
            return true;
        }
        m_recent.push_back({ t, kind, hasId, id, sig });
        return false;
    }

    // a scene entry counts once the player turns up at its position
    void Learner::moved(uint64_t t, const Vec3& pos, std::vector<Event>& out) {
        m_player = pos;
        m_hasPlayer = true;
        if (IsLearned(Kind::SceneChanged))
            return;
        std::vector<PendingScene> pending = std::move(m_pendingScene);
        m_pendingScene.clear();
        for (const PendingScene& c : pending) {
            if (since(t, c.ms) > ARRIVE_MS)
                continue;
            if (dist(c.pos, pos) < 10)
                hit(c.ms, Kind::SceneChanged, c.id, { c.field }, {}, out);
            else
                m_pendingScene.push_back(c);
        }
    }

    // type 1 in the entity's top bits is an avatar. The smallest entity has the
    // lowest type, right once a list held one of the player's avatars.
    bool Learner::isAvatar(uint64_t entity) {
        if (m_world.size() + m_others.size() < 10)
            return true;
        if (m_entitiesChanged) {
            m_entitiesChanged = false;
            uint64_t smallest = UINT64_MAX;
            for (auto& [e, o] : m_world)
                smallest = std::min(smallest, e);
            for (uint64_t e : m_others)
                smallest = std::min(smallest, e);
            if (smallest != m_minEntity) {
                m_minEntity = smallest;
                m_shift = 0;
                while (smallest >> (m_shift + 1))
                    m_shift++;
            }
        }
        return entity >> m_shift == 1;
    }

    // the objects' own state field: the one holding what a state change said last
    void Learner::tallyStateField(const Object& o, const Fields& raw) {
        auto known = m_world.find(o.entity);
        if (known == m_world.end() || !known->second.changed)
            return;
        for (const Messages::Field& f : raw) {
            if (f.wire != 0 || f.value != known->second.state || !f.value || !isState(f.value))
                continue;
            auto vote = std::find_if(m_stateVotes.begin(), m_stateVotes.end(), [&](auto& v) { return v.first == f.number; });
            if (vote == m_stateVotes.end())
                m_stateVotes.push_back({ f.number, 1 });
            else
                vote->second++;
        }
        auto best = m_stateVotes.begin();
        for (auto v = m_stateVotes.begin(); v != m_stateVotes.end(); v++)
            if (v->second > best->second)
                best = v;
        if (best != m_stateVotes.end() && size_t(best->second) >= NEED)
            m_stateField = best->first;
    }

    // learned only with nothing against it and more hits than any other id
    bool Learner::lead(Kind kind, uint16_t id, const Sig& sig) {
        if (m_contradicted.count({ kind, id }))
            return false;
        // one id is one kind
        for (auto& [k, learned] : m_learned)
            if (learned.id == id && k != kind && directionOf(k) == directionOf(kind))
                return false;
        size_t n = count(kind, id, sig);
        if (n < NEED)
            return false;
        for (auto& [key, c] : m_candidates[int(kind)])
            if (!(key.first == id && key.second == sig) && count(kind, key.first, key.second) >= n)
                return false;
        return true;
    }

    size_t Learner::count(Kind kind, uint16_t id, const Sig& sig) {
        const Candidate& c = m_candidates[int(kind)][{ id, sig }];
        if (std::none_of(c.partners.begin(), c.partners.end(), [](const Partner& p) { return p.any; }))
            return c.hits.size();
        size_t n = 0;
        for (const Partner& p : c.partners)
            if (!p.any || !p.hasId || !m_contradicted.count({ p.kind, p.id }))
                n++;
        return n;
    }

    // a gone object is never named again by the same message
    void Learner::goneCheck(uint64_t t, uint16_t id, const Fields& fs) {
        if (m_goneSeen.empty())
            return;
        for (const Messages::Field& f : fs) {
            if (f.wire != 0)
                continue;
            auto seen = m_goneSeen.find({ id, f.value });
            if (seen != m_goneSeen.end() && since(t, seen->second) > 0)
                m_contradicted[{ Kind::ObjectGone, id }]++;
        }
    }

    void Learner::apply(Event& ev, const std::vector<Fields>& raws, std::vector<Event>& out) {
        switch (ev.kind) {
        case Kind::ObjectsNear:
            for (size_t i = 0; i < ev.objects.size(); i++) {
                Object& o = ev.objects[i];
                if (!o.gadget && !o.group)
                    continue;
                const Fields& raw = raws[i];
                if (!raw.empty() && m_stateField) {
                    o.hasState = true;
                    o.state = uint32_t(valueOr0(raw, m_stateField));
                }
                if (!raw.empty())
                    tallyStateField(o, raw);
                // an older message (a replay) doesn't bring back what left, or what had newer news
                auto gone = m_goneAt.find(o.entity);
                if (ev.ms < m_sceneMs || (gone != m_goneAt.end() && gone->second >= ev.ms))
                    continue;
                auto had = m_world.find(o.entity);
                if (had != m_world.end() && had->second.stateMs > ev.ms)
                    continue;
                WorldObject& w = m_world[o.entity];
                w = { o.gadget, o.group, o.config, o.hasPos, o.pos, o.state, ev.ms, false };
                m_entitiesChanged = true;
            }
            break;
        case Kind::StateChanged: {
            auto o = m_world.find(ev.entity);
            if (o != m_world.end() && o->second.stateMs <= ev.ms) {
                o->second.state = ev.value;
                o->second.stateMs = ev.ms;
                o->second.changed = true;
            }
            break;
        }
        case Kind::PlayerMoved:
            moved(ev.ms, ev.pos, out);
            break;
        case Kind::SceneChanged: {
            // what was seen before it is gone; the player's avatars stay, the game doesn't send them again
            for (auto o = m_world.begin(); o != m_world.end();)
                o = o->second.stateMs < ev.ms ? m_world.erase(o) : std::next(o);
            m_entitiesChanged = true;
            m_sceneMs = std::max(m_sceneMs, ev.ms);
            m_loadingUntil = std::max(m_loadingUntil, ev.ms + LOADING_MS);
            if (ev.replayed)
                break;
            std::unordered_set<uint64_t> avatars;
            for (uint64_t e : m_others)
                if (isAvatar(e))
                    avatars.insert(e);
            m_others = std::move(avatars);
            m_player = ev.pos;
            m_hasPlayer = true;
            break;
        }
        case Kind::Interact:
            m_hasLastInteract = true;
            m_lastInteract = { ev.ms, ev.entity };
            break;
        case Kind::ObjectGone:
            m_world.erase(ev.entity);
            if (m_goneAt.size() > 50000)
                m_goneAt.clear();
            m_goneAt[ev.entity] = std::max(m_goneAt[ev.entity], ev.ms);
            m_entitiesChanged = true;
            break;
        default:
            break;
        }
        out.push_back(ev);
    }
}

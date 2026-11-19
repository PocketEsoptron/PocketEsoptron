#include "links.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <set>
#include <vector>
#include <nlohmann/json.hpp>
#include "../game_tables.hpp"
#include "../item_catalog.hpp"
#include "../marker_index.hpp"
#include "../regrow.hpp"
#include "scene.hpp"

namespace Links {
    // HoYoLAB's groups and types
    static constexpr int CHESTS = 13, SPECIAL_ITEMS = 4, EXPERIENCE = 51, PUZZLE_CHEST = 186;
    static constexpr int GEO_SIGIL = 128, BURIED_CHEST = 69, SEALED_CHEST = 75, LIGHT_REVEALED_CHEST = 879;
    static constexpr int LOCAL_SPECIALTIES = 10, ORES = 11, MATERIALS = 60;
    static constexpr int TEYVAT = 2;
    // a link needs this many objects on its type
    static constexpr size_t AGREE = 2;

    Route RouteOf(uint32_t gadget, const GameTables::Tables& tables) {
        GameTables::Gadget g;
        if (!tables.gadget(gadget, g))
            return Route::None;
        if (g.type == "Chest")
            return Route::Opened;
        std::vector<GameTables::Gather> gathers = tables.gathers(gadget);
        if (!gathers.empty()) {
            if (!tables.knowsRanks())
                return Route::None;
            GameTables::Material material;
            for (const GameTables::Gather& gather : gathers) {
                // without its item's row it can't be told whether it grows back
                if (!gather.refresh && !tables.material(gather.item, material))
                    return Route::None;
                if (Regrow::Of(gather.item, tables).grows())
                    return Route::Regrows;
            }
            return Route::PickedUp;
        }
        return Route::None;
    }

    const std::unordered_set<int>& Types::of(Route route) const {
        static const std::unordered_set<int> none;
        switch (route) {
        case Route::Opened: return opened;
        case Route::PickedUp: return pickedUp;
        case Route::Regrows: return regrows;
        default: return none;
        }
    }

    static bool endsWith(const std::string& text, const std::string& end) {
        return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
    }

    Types TypesOf(const ItemCatalog::Catalog& catalog) {
        Types out;
        for (const ItemCatalog::TypeGroup& group : catalog.groups)
            for (const ItemCatalog::Type& type : group.types) {
                if (group.id == CHESTS || group.id == SPECIAL_ITEMS || group.id == EXPERIENCE || group.id == PUZZLE_CHEST)
                    out.places.insert(type.id);
                if (group.id == CHESTS || group.id == SPECIAL_ITEMS || (group.id == EXPERIENCE && endsWith(type.name, "Mora Chest")))
                    out.opened.insert(type.id);
                if (group.id == SPECIAL_ITEMS)
                    out.pickedUp.insert(type.id);
                if (group.id == LOCAL_SPECIALTIES || group.id == ORES || group.id == MATERIALS)
                    out.regrows.insert(type.id);
            }
        out.opened.insert({ GEO_SIGIL, BURIED_CHEST, SEALED_CHEST, LIGHT_REVEALED_CHEST });
        return out;
    }

    int MapOf(uint32_t group) {
        // Teyvat's groups start with 131 to 133
        uint32_t scene = group / 1000000;
        return scene >= 131 && scene <= 133 ? TEYVAT : 0;
    }

    Vote VoteAt(const MarkerIndex::Index& index, const Types& types, Route route, int map, double x, double y) {
        MarkerIndex::Near near = index.UniqueNear(types.of(route), map, x, y, RADIUS, MARGIN);
        if (!near.nearest)
            return {};
        MarkerIndex::Near any = index.UniqueNear(types.places, map, x, y, near.distance);
        if (any.nearest && any.distance < near.distance && !types.of(route).count(any.nearest->type))
            return {};
        if (!near.unique)
            return { UNCLEAR };
        return { near.nearest->type, near.nearest->id, near.distance };
    }

    void Links::redecide(Votes& votes) {
        int type = decide(votes);
        if (type != votes.type) {
            votes.type = type;
            m_changed = true;
        }
    }

    void Links::Cast(uint32_t gadget, uint32_t group, uint32_t config, Vote vote) {
        Object object = { group, config };
        if (m_never.count(gadget))
            return;
        auto known = m_votes.find(gadget);
        if (known != m_votes.end() && (known->second.objects.size() >= KEEP || known->second.objects.count(object)))
            return;
        if (vote.type > 0) {
            auto claim = m_claims.find(vote.point);
            if (claim != m_claims.end() && claim->second.distance <= vote.distance) {
                vote = {};
            } else {
                uint32_t was = claim != m_claims.end() ? claim->second.gadget : 0;
                if (claim != m_claims.end()) {
                    Votes& before = m_votes[was];
                    before.objects[claim->second.object] = {};
                    redecide(before);
                }
                m_claims[vote.point] = { gadget, object, vote.distance };
                if (was && was != gadget && hopeless(m_votes[was]))
                    drop(was);
            }
        }
        Votes& votes = m_votes[gadget];
        votes.objects.emplace(object, vote);
        m_unsaved = true;
        redecide(votes);
        if (hopeless(votes))
            drop(gadget);
    }

    // no link now, and none even if every object still to come agreed
    bool Links::hopeless(const Votes& votes) {
        if (votes.type)
            return false;
        if (votes.objects.size() >= KEEP)
            return true;
        std::map<int, size_t> count;
        size_t most = 0;
        for (auto& [object, vote] : votes.objects)
            if (vote.type > 0)
                most = std::max(most, ++count[vote.type]);
        return 3 * (most + KEEP - votes.objects.size()) < KEEP;
    }

    void Links::drop(uint32_t gadget) {
        auto votes = m_votes.find(gadget);
        if (votes == m_votes.end())
            return;
        for (auto& [object, vote] : votes->second.objects) {
            auto claim = m_claims.find(vote.point);
            if (vote.type > 0 && claim != m_claims.end() && claim->second.gadget == gadget)
                m_claims.erase(claim);
        }
        m_votes.erase(votes);
        m_never.insert(gadget);
    }

    void Links::See(const World::Object& object, const MarkerIndex::Index& index, const GameTables::Tables& tables, const Types& types) {
        if (!object.gadget || !object.hasPos || (!object.group && !object.config))
            return;
        int map = MapOf(object.group);
        Scene::MapPos at;
        if (!map || !Scene::ToMap(object.pos, map, at))
            return;
        auto known = m_votes.find(object.gadget);
        if (m_never.count(object.gadget) || (known != m_votes.end() && known->second.objects.count({ object.group, object.config })))
            return;
        Route route = RouteOf(object.gadget, tables);
        if (route == Route::None)
            return;
        Cast(object.gadget, object.group, object.config, VoteAt(index, types, route, map, at.x, at.y));
    }

    // The type most of its objects sit on: at least AGREE of them, twice as many
    // as all other types and unclear ones together, and a third of every object
    // seen, the ones far from any map item included
    int Links::decide(const Votes& votes) {
        std::map<int, size_t> count;
        for (auto& [object, vote] : votes.objects)
            count[vote.type]++;
        int best = 0;
        size_t most = 0, others = 0;
        for (auto& [vote, n] : count)
            if (vote > 0 && n > most) {
                best = vote;
                most = n;
            }
        for (auto& [vote, n] : count)
            if (vote != NONE && vote != best)
                others += n;
        if (!best || most < AGREE || most < 2 * others || 3 * most < votes.objects.size())
            return 0;
        return best;
    }

    int Links::TypeOf(uint32_t gadget) const {
        auto known = m_known.find(gadget);
        if (known != m_known.end())
            return known->second;
        auto votes = m_votes.find(gadget);
        return votes == m_votes.end() ? 0 : votes->second.type;
    }

    Known Links::Learned() const {
        Known out;
        for (auto& [gadget, votes] : m_votes)
            if (votes.type)
                out[gadget] = votes.type;
        return out;
    }

    bool Links::TakeChanged() {
        bool changed = m_changed;
        m_changed = false;
        return changed;
    }

    bool Links::TakeUnsaved() {
        bool unsaved = m_unsaved;
        m_unsaved = false;
        return unsaved;
    }

    // {"votes": {"<gadget>": [[group, config, type, point, distance], ...]}, "never": [gadget, ...]}
    std::string Links::ToJson() const {
        nlohmann::json votes = nlohmann::json::object();
        std::map<uint32_t, const Votes*> sorted;
        for (auto& [gadget, v] : m_votes)
            sorted[gadget] = &v;
        for (auto& [gadget, v] : sorted) {
            nlohmann::json objects = nlohmann::json::array();
            for (auto& [object, vote] : v->objects)
                objects.push_back({ object.first, object.second, vote.type, vote.point, vote.distance });
            votes[std::to_string(gadget)] = std::move(objects);
        }
        std::set<uint32_t> never(m_never.begin(), m_never.end());
        return nlohmann::json({ { "votes", std::move(votes) }, { "never", never } }).dump();
    }

    static bool readId(const std::string& text, uint32_t& out) {
        if (text.empty() || text[0] < '0' || text[0] > '9')
            return false;
        char* end = nullptr;
        errno = 0;
        unsigned long long id = std::strtoull(text.c_str(), &end, 10);
        if (*end || errno == ERANGE || !id || id > std::numeric_limits<uint32_t>::max())
            return false;
        out = uint32_t(id);
        return true;
    }

    // the votes as they were saved; a map item's claim goes to the nearest of them again
    bool Links::FromJson(const std::string& text) {
        nlohmann::json all = nlohmann::json::parse(text, nullptr, false);
        if (!all.is_object() || !all.contains("votes") || !all["votes"].is_object())
            return false;
        std::unordered_map<uint32_t, Votes> votes;
        for (auto& [key, objects] : all["votes"].items()) {
            uint32_t gadget = 0;
            if (!readId(key, gadget) || !objects.is_array())
                return false;
            Votes& v = votes[gadget];
            for (const nlohmann::json& o : objects) {
                if (!o.is_array() || o.size() != 5 || !o[0].is_number_unsigned() || !o[1].is_number_unsigned() || !o[2].is_number_integer()
                    || !o[3].is_number_integer() || !o[4].is_number())
                    return false;
                if (v.objects.size() < KEEP)
                    v.objects.emplace(Object{ o[0].get<uint32_t>(), o[1].get<uint32_t>() },
                        Vote{ o[2].get<int>(), o[3].get<int32_t>(), o[4].get<double>() });
            }
        }
        std::unordered_set<uint32_t> never;
        if (all.contains("never")) {
            if (!all["never"].is_array())
                return false;
            for (const nlohmann::json& g : all["never"]) {
                if (!g.is_number_unsigned() || g.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
                    return false;
                never.insert(g.get<uint32_t>());
            }
        }
        for (uint32_t gadget : never)
            votes.erase(gadget);

        std::unordered_map<int32_t, Claim> claims;
        for (auto& [gadget, v] : votes)
            for (auto& [object, vote] : v.objects) {
                if (vote.type <= 0)
                    continue;
                auto claim = claims.find(vote.point);
                if (claim == claims.end() || vote.distance < claim->second.distance)
                    claims[vote.point] = { gadget, object, vote.distance };
            }
        for (auto& [gadget, v] : votes) {
            for (auto& [object, vote] : v.objects)
                if (vote.type > 0) {
                    const Claim& claim = claims.at(vote.point);
                    if (claim.gadget != gadget || claim.object != object)
                        vote = {};
                }
            v.type = decide(v);
        }
        m_votes = std::move(votes);
        m_claims = std::move(claims);
        m_never = std::move(never);
        std::vector<uint32_t> gone;
        for (auto& [gadget, v] : m_votes)
            if (hopeless(v))
                gone.push_back(gadget);
        for (uint32_t gadget : gone)
            drop(gadget);
        m_changed = false;
        m_unsaved = false;
        return true;
    }

    std::string KnownToJson(const Known& known) {
        nlohmann::json all = nlohmann::json::array();
        for (auto& [gadget, type] : known)
            all.push_back({ { "gadget", gadget }, { "type", type } });
        return all.dump();
    }

    bool KnownFromJson(const std::string& text, Known& out) {
        nlohmann::json all = nlohmann::json::parse(text, nullptr, false);
        if (!all.is_array())
            return false;
        Known read;
        for (const nlohmann::json& link : all) {
            if (!link.is_object() || !link.contains("gadget") || !link["gadget"].is_number_unsigned()
                || link["gadget"].get<uint64_t>() > std::numeric_limits<uint32_t>::max() || !link.contains("type")
                || !link["type"].is_number_integer() || link["type"].get<int>() <= 0)
                return false;
            read[link["gadget"].get<uint32_t>()] = link["type"].get<int>();
        }
        out = std::move(read);
        return true;
    }

    Known Shipped() {
        return {
            { 70211001, 17 }, { 70211002, 17 }, { 70211101, 17 }, { 70211102, 17 },  // Common Chest
            { 73174098, 17 }, { 73174099, 17 },                                       // Snezhnaya's Common Chest
            { 70211011, 44 }, { 70211012, 44 }, { 70211111, 44 },                     // Exquisite Chest
            { 70211022, 45 }, { 70211121, 45 }, { 70211122, 45 },                     // Precious Chest
            { 70211031, 46 }, { 70211032, 46 }, { 70211131, 46 }, { 70211132, 46 },  // Luxurious Chest
            { 70211150, 269 }, { 70211151, 269 },                                     // Remarkable Chest
            { 70210063, 75 }, { 70211103, 75 },                                       // Sealed Chest
            { 70210105, 129 },                                                        // Wooden Mora Chest
            { 70217012, 359 }, { 70217014, 359 }, { 70217020, 359 }, { 70330342, 359 },  // Stone Pile Mora Chest
            { 70217026, 366 }, { 70217027, 366 }, { 73003003, 596 },                  // Fontaine and Natlan mora chests
            { 70217046, 723 }, { 73138056, 819 },                                     // Nod-Krai and Frost Moon mora chests
            { 73176013, 838 },                                                        // Snezhnaya Mora Chest
            { 70217013, 356 },                                                        // Radiant Spincrystal
            { 70210107, 128 }, { 70210108, 128 },                                     // Geo Sigil
            { 70590004, 5 }, { 70590021, 6 }, { 70590041, 194 },                      // Anemoculus, Geoculus, Electroculus
            { 70590063, 403 }, { 70590074, 508 }, { 70590086, 626 },                  // Dendroculus, Hydroculus, Pyroculus
            { 70590088, 694 }, { 73094157, 833 },                                     // Lunoculus, Cryoculus
            { 70590031, 141 }, { 70590042, 472 },                                     // Crimson Agate, Khvarena Inscription Fragment
            { 70211168, 471 }, { 70330366, 429 }, { 70590084, 554 },                  // Udumbara, Sacred Seal, Spirit Carp
            // things that grow back
            { 70330221, 355 }, { 70330449, 62 }, { 70510005, 63 }, { 70510012, 40 }, // Starshroom, Flaming Flower Stamen, Electro Crystal, Cor Lapis
            { 70520005, 160 }, { 70520009, 161 }, { 70520024, 161 },                  // Sweet Flower, Mint, Mint
            { 70520013, 124 }, { 70540015, 36 }, { 70540027, 93 },                    // Matsutake, Silk Flower, Horsetail
            { 70540028, 78 }, { 70540034, 402 }, { 70540036, 386 },                   // Starconch, Rukkhashava Mushrooms, Harra Fruit
            { 70540042, 384 }, { 70540043, 400 }, { 70540051, 522 },                  // Sumeru Rose, Nilotpala Lotus, Pluie Lotus
            { 70540053, 492 }, { 70540061, 545 }, { 70540081, 620 },                  // Marcotte, Lumitoile, Embercore Flower
            { 73176001, 854 },                                                        // Lumifluorite
        };
    }
}

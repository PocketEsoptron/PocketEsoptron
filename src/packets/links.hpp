#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "../marker_index.hpp"
#include "world.hpp"

namespace GameTables { class Tables; }
namespace ItemCatalog { class Catalog; }

// Which map item type each of the game's gadgets is. Every placed object the
// game shows near the player votes for the type of the one map item it sits
// on, and a gadget is that type once its objects agree. A map item counts for
// the nearest object only. A gadget whose votes can't make a link any more is
// dropped for good. Links that come with the build or the items pack hold
// without votes.

namespace Links {
    // how an object of a gadget is found
    enum class Route { None, Opened, PickedUp, Regrows, Used };

    // by the game's tables: a Chest is opened, a gather point is picked up, and
    // Regrows when its item grows back (Regrow). A ViewPoint is used: the game
    // takes it away once the player has looked, and never sends one done
    // before. None for the rest, and for a gather point when the tables have no
    // ranks to tell what grows back
    extern Route RouteOf(uint32_t gadget, const GameTables::Tables& tables);

    // the map item types each route can mark, and every type an object can sit on
    struct Types {
        std::unordered_set<int> opened, pickedUp, regrows, used, places;
        // the Chests group's types, and the Buried, Sealed and Light-Revealed chests
        std::unordered_set<int> chests, puzzleChests;
        const std::unordered_set<int>& of(Route route) const;
    };
    // opened: Chests, Special Items, the mora chests, Geo Sigil and the puzzle
    // chests; picked up: Special Items; regrows: Local Specialties, Ores and
    // Inventory / Materials; used: the Viewpoint when the pack has it; places:
    // the Chests, Special Items, Experience and Puzzle Chest groups
    extern Types TypesOf(const ItemCatalog::Catalog& catalog);

    // HoYoLAB's types of the chests a puzzle hides, the Geo Sigil and the Viewpoint
    inline constexpr int BURIED_CHEST = 69, SEALED_CHEST = 75, LIGHT_REVEALED_CHEST = 879, GEO_SIGIL = 128;
    inline constexpr int VIEWPOINT = 85;

    // the map an object is on by its group, 0 when it isn't one the overlay knows
    extern int MapOf(uint32_t group);

    // a vote that isn't a type
    inline constexpr int NONE = 0;      // no map item of the route's types within reach
    inline constexpr int UNCLEAR = -1;  // two too close to tell
    // how far an object may sit from its map item, and how much further the next has to be
    inline constexpr double RADIUS = 10;
    inline constexpr double MARGIN = 5;

    // a type, NONE or UNCLEAR, with the map item it was given for
    struct Vote {
        int type = NONE;
        int32_t point = 0;
        double distance = 0;
    };
    // the vote of an object at (map, x, y) on the floor: the one map item of the
    // route's types there, NONE when there's none or a place of another type is nearer
    extern Vote VoteAt(const MarkerIndex::Index& index, const Types& types, Route route, int map, double x, double y,
                       MarkerIndex::Floor floor);

    using Known = std::map<uint32_t, int>;

    class Links {
    public:
        // objects kept per gadget, enough to decide
        static constexpr size_t KEEP = 64;

        explicit Links(Known known = {}) : m_known(std::move(known)) {}

        // one vote per object, the first one counts; a map item another object
        // sits nearer to is a NONE vote
        void Cast(uint32_t gadget, uint32_t group, uint32_t config, Vote vote);
        // an object the World has, voted when its gadget has a route and it is on a known map
        void See(const World::Object& object, const MarkerIndex::Index& index, const GameTables::Tables& tables, const Types& types);

        // the type the gadget is, 0 while it isn't clear
        int TypeOf(uint32_t gadget) const;
        // the links made from votes
        Known Learned() const;
        // replaces the links that hold without votes
        void SetKnown(Known known) { m_known = std::move(known); }
        // true once after a link was made from votes
        bool TakeChanged();
        // true once after a vote was kept
        bool TakeUnsaved();

        // the votes, for the learned file
        std::string ToJson() const;
        // false when it doesn't read
        bool FromJson(const std::string& text);

    private:
        using Object = std::pair<uint32_t, uint32_t>;
        struct Votes {
            std::map<Object, Vote> objects;
            int type = 0;
        };
        // the object a map item counts for
        struct Claim {
            uint32_t gadget = 0;
            Object object;
            double distance = 0;
        };
        static int decide(const Votes& votes);
        static bool hopeless(const Votes& votes);
        void redecide(Votes& votes);
        void drop(uint32_t gadget);

        Known m_known;
        std::unordered_map<uint32_t, Votes> m_votes;
        std::unordered_map<int32_t, Claim> m_claims;
        std::unordered_set<uint32_t> m_never;
        bool m_changed = false;
        bool m_unsaved = false;
    };

    // the links the build ships, learned on the test recordings and checked against the gadget names
    extern Known Shipped();
    // the chest type a Chest gadget's name gives, "SceneObj_Chest_Locked_Lv2" is an
    // Exquisite Chest: a Bramble, Frozen or Rock chest is a Sealed Chest, the rest
    // go by their level. 0 for a Lv3, fake, drop or ticket chest and other names
    extern int ChestByName(std::string_view name);
    // ChestByName of every Chest gadget in the tables that gives a type
    extern Known ChestsByName(const GameTables::Tables& tables);
    // every ViewPoint gadget in the pack's tables, linked to the Viewpoint; none
    // when the pack has no Viewpoint
    extern Known Viewpoints(const ItemCatalog::Catalog& catalog);
    // Each gather gadget whose item grows back, linked to the one type of the
    // regrows groups named like the item (case, spaces and a plural s aside):
    // Sumeru Rose to Sumeru Rose. Not for a gadget that gives more than one item.
    extern Known GathersByName(const GameTables::Tables& tables, const ItemCatalog::Catalog& catalog);
    // the regrows groups' types by their names as ItemType compares them, 0 for a
    // name two types have
    extern std::map<std::string, int> RegrowsByName(const ItemCatalog::Catalog& catalog);
    // The one type named like an item that grows back, 0 when none is. What a
    // gather point really gives can be another item than its row says,
    // Fontaine's Condessence rocks are rows of Crystal Chunk.
    extern int ItemType(uint32_t item, const GameTables::Tables& tables, const std::map<std::string, int>& regrowsByName);
    // [{gadget, type}] and back, false when it doesn't read
    extern std::string KnownToJson(const Known& known);
    extern bool KnownFromJson(const std::string& text, Known& out);
}

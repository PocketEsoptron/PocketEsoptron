#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "floors.hpp"
#include "links.hpp"
#include "puzzles.hpp"
#include "world.hpp"

namespace GameServer { struct Server; }
namespace ItemCatalog { class Catalog; }
namespace MarkerIndex { class Index; }

// The map items the player has just done, from what the World tells: a chest
// opened, a pickup taken (gone with reason 2 with its item got in the same
// moment), a viewpoint used (gone with reason 2 right after the player
// interacted with it) and a puzzle solved. Only objects the World saw appear
// count, so nothing from before the overlay listened. A pickup that grows back
// (Regrow) says when, and can be found again from then, or sooner when its
// object is seen again: the game never sends a picked object before it's back.

namespace Finds {
    // Back: an object waiting to grow back was seen again, it's back already;
    // Used: a viewpoint looked from
    enum class How { Opened, PickedUp, Solved, Back, Used };

    struct Found {
        How how = How::Opened;
        uint64_t ms = 0;
        // the game's object
        uint32_t gadget = 0, group = 0, config = 0;
        // where it was on the map
        int map = 0;
        double x = 0, y = 0;
        // the map item's type: the one its gadget is linked to, or a puzzle chest for a chest
        int type = 0;
        // a pickup's item, and when it grows back in Unix seconds (Regrow), 0 when it doesn't
        uint32_t item = 0;
        int64_t backAt = 0;
        // backAt is by the server's clock, so it tells which server
        bool byServer = false;
        // the map item done, 0 when more than one could be
        int32_t point = 0;
        // when point is 0: the ones that could be, nearest first
        std::vector<int32_t> candidates;
        // when point is 0: it may be none of them, its floor isn't known (Floors::At)
        bool maybeNone = false;
        // when point is 0: the other objects of the types (group, config) about as
        // near, whose map item it may be
        std::vector<std::pair<uint32_t, uint32_t>> rivals;
    };

    // a map item's object picked up, waiting to grow back until at (Unix seconds);
    // group 0 when the object isn't known
    struct Growing {
        uint32_t group = 0, config = 0;
        std::vector<int32_t> points;
        int64_t at = 0;
    };

    // an item got this close to a gone pickup is taken with it
    inline constexpr uint64_t SAME_MOMENT = 200;
    // how far a picked up thing that grows back may sit from its map item: ore
    // rocks and plants sit a bit further out than the objects links are learned
    // from (Links::RADIUS)
    inline constexpr double PICKUP_RADIUS = 15;
    // drops kept, the oldest go first, and how long one may lie before it's picked up
    inline constexpr size_t KEEP_DROPS = 32;
    inline constexpr uint64_t DROP_LIES = 10 * 60 * 1000;
    // how far a puzzle's map item may sit from one of its parts
    inline constexpr double PUZZLE_RADIUS = 12;
    // the minimap's floor is wanted this long after an object whose floor isn't known came by
    inline constexpr uint64_t FLOOR_WANTED = 60 * 1000;
    // a viewpoint goes this soon after the player interacted with it
    inline constexpr uint64_t USED_AFTER = 2000;

    class Finder {
    public:
        explicit Finder(Links::Links& links);
        ~Finder();

        // the items pack to find map items in, nothing is found without one
        void SetCatalog(std::shared_ptr<const ItemCatalog::Catalog> catalog);
        // map items that are done already, passed over, and the objects waiting to
        // grow back. For a pickup that grows back only those waiting count as done:
        // its object being there shows any other tick on it is out of date.
        void SetDone(std::unordered_set<int32_t> done, std::vector<Growing> growing = {});
        // the server whose clock some things grow back by; with none those aren't found
        void SetServer(const GameServer::Server* server);

        // world as it is after the event, its ms a Unix time
        void Feed(const World::Event& event, const World::World& world, std::vector<Found>& out);
        // drops the pickups whose item can't come any more by ms, and what grew back by then
        void Settle(uint64_t ms);
        // the puzzles the last moment finished, when no event comes after it
        void Finish(const World::World& world, std::vector<Found>& out);

        // The floor the minimap shows the player on (HoYoLAB's group and floor,
        // 0 and 0 over ground), map 0 when it isn't sure. A thing picked up,
        // opened or solved whose own floor isn't known (Floors::At) is on it:
        // the player is right there.
        void SetPlayerFloor(int map, int group, int floor);
        // Until when (an event's ms) the minimap's floor is wanted: an object
        // whose floor isn't known came by lately. 0 when none did.
        uint64_t FloorWantedUntil() const { return m_floorWanted; }

    private:
        struct Gone {
            World::Object object;
            uint64_t ms = 0;
            // when it left, for a drop the time its gather point did
            uint64_t at = 0;
            // it's a gather point that came back as the drop it left
            bool drop = false;
        };
        // what a gather point left on the ground, an ore's chunk: its item comes
        // when that is picked up
        struct Drop {
            uint64_t entity = 0;
            Gone point;
        };
        struct Got {
            uint64_t ms = 0;
            std::vector<std::pair<uint32_t, uint32_t>> items;
        };

        std::unordered_set<int> typesFor(int type) const;
        // Floors::At, else the player's floor on that map
        Floors::Of floorOf(uint32_t group, const std::unordered_set<int>& types, int map, double x, double y, double reach) const;
        void wantFloor(const World::Object& object, uint64_t ms);
        bool find(How how, const World::Object& object, uint64_t ms, const World::World& world, Found& out, uint32_t item = 0);
        uint32_t itsItem(const Gone& gone, const Got& got) const;
        int itemType(uint32_t item) const;
        void gone(const World::Event& event, const World::World& world, std::vector<Found>& out);
        void got(const World::Event& event, const World::World& world, std::vector<Found>& out);
        void dropped(const World::Object& object, uint64_t ms);
        void solved(const std::vector<Puzzles::Solved>& solved, std::vector<Found>& out);
        void seen(const World::Object& object, uint64_t ms, std::vector<Found>& out);
        void remake(int64_t now);

        Links::Links& m_links;
        std::shared_ptr<const ItemCatalog::Catalog> m_catalog;
        std::shared_ptr<const MarkerIndex::Index> m_index;
        Links::Types m_types;
        // Links::RegrowsByName of the pack
        std::map<std::string, int> m_regrowsByName;
        using Key = std::pair<uint32_t, uint32_t>;
        struct Back {
            int64_t at = 0;
            std::vector<int32_t> points;
        };
        // found this run; given as done; both, and for pickups that grow back the
        // ones waiting to
        std::unordered_set<int32_t> m_found, m_done, m_taken, m_growingTaken;
        std::set<Key> m_foundObjects;
        // the ones found this run that grow back, with when
        std::map<int32_t, int64_t> m_backPoints;
        std::map<Key, Back> m_backObjects;
        // the ones given as waiting, by object, and the ones without one
        std::map<Key, Back> m_givenObjects;
        std::vector<Back> m_givenPoints;
        // the soonest a given one is back, when the sets are made again
        int64_t m_nextBack = INT64_MAX;
        const GameServer::Server* m_server = nullptr;
        std::deque<Gone> m_waiting;
        std::deque<Got> m_got;
        std::deque<Drop> m_drops;
        Puzzles::Tracker m_puzzles;
        int m_playerMap = 0;
        Floors::Of m_playerFloor;
        uint64_t m_floorWanted = 0;
        // the object the player last interacted with, and when
        uint64_t m_interacted = 0, m_interactedMs = 0;
    };

    // What every extension gave as done (SetDone) goes to the running Finder:
    // the map items they all agree are done, so one can only take hints away,
    // and every object waiting to grow back, one seen again being a fact.
    extern void SetDoneFor(const void* owner, std::unordered_set<int32_t> done, std::vector<Growing> growing);
    extern void ForgetDone(const void* owner);

    // the items pack's links (game/links.json), then the ones the build ships, then
    // the chests by their names in the pack's tables, the viewpoints, then the
    // gathers by their items' names
    extern Links::Known KnownLinks(const ItemCatalog::Catalog* catalog);
    // the map item types found by themselves: the ones the links' gadgets are,
    // the puzzle chests a chest can sit on, and the puzzles
    extern std::set<int> AutoTypes(const Links::Known& links);
    // the votes of the learned file into links; a file that doesn't read is kept aside as
    // .broken, false when it couldn't be
    extern bool LoadVotes(const std::filesystem::path& file, Links::Links& links);

    // The Finder on the World's events, for the whole run. The links' votes are
    // kept in learned-links.json in the data folder. Start after World::Start.
    // The player's floor comes from playerFloor (the minimap's in the overlay):
    // false when it isn't shown right now, else the map, HoYoLAB's group and
    // floor (0 and 0 over ground). It's only called while Finder wants it.
    typedef std::function<bool(int& map, int& group, int& floor)> PlayerFloor;
    extern void Start(const std::filesystem::path& folder, PlayerFloor playerFloor = nullptr);
    extern void Stop();

    // Called on GameTraffic's worker thread for every find, keep it short.
    // Gives an id for RemoveListener.
    using Listener = std::function<void(const Found&)>;
    extern int AddListener(Listener listener);
    extern void RemoveListener(int id);

    // A find of the running Finder and its id. Ids go up, also from one run to
    // the next, so an extension can keep the last one it took.
    struct Numbered {
        uint64_t id = 0;
        Found found;
    };
    // how many of the newest finds are kept for Recent
    inline constexpr size_t RECENT = 256;
    // the kept finds after the id, oldest first
    extern std::vector<Numbered> Recent(uint64_t after);
    // the newest find's id, 0 before the first of the run
    extern uint64_t LastId();

    // What the overlay itself found picked up that grows back, and when, by map
    // item: a sure find's point, an unsure one's every candidate. An object seen
    // again is back at once. The marks job unmarks only these without asking
    // (Marks::SetGrownBack), so nothing an extension says goes in.
    class GrowingRecord {
    public:
        // false when it changed nothing
        bool Add(const Found& found, int64_t now);
        // every one of the points is in it and back by now
        bool AllBack(const std::set<int64_t>& points, int64_t now) const;
        // false when none of them was in it
        bool Unmarked(const std::set<int64_t>& points);
        std::string ToJson() const;
        // ones back longer than FORGET ago are left out
        void FromJson(const std::string& text, int64_t now);
        size_t Size() const { return m_points.size(); }

        // one never unmarked is let go after this long back, in seconds
        static constexpr int64_t FORGET = 30 * 24 * 3600;

    private:
        struct Entry {
            int64_t at = 0;
            // every object picked that may have been it, (group, config)
            std::set<std::pair<uint32_t, uint32_t>> objects;
        };
        std::map<int32_t, Entry> m_points;
    };
    // the running record (growing-finds.json in the data folder), by the clock now
    extern bool AllGrownBack(const std::set<int64_t>& points);
    extern void Unmarked(const std::set<int64_t>& points);

    // AutoTypes of the items pack in use and of what the running Finder learned,
    // none while the game's traffic isn't read or the pack has no game tables,
    // and a number that goes up each time they change
    extern std::set<int> AutoTypesNow();
    extern uint64_t AutoTypesVersion();
}

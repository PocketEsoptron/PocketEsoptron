#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>
#include "links.hpp"
#include "world.hpp"

namespace ItemCatalog { class Catalog; }
namespace MarkerIndex { class Index; }

// The map items the player has just done, from what the World tells: a chest
// opened, a pickup taken (gone with reason 2 with its item got in the same
// moment). Only objects the World saw appear count, so nothing from before
// the overlay listened.

namespace Finds {
    enum class How { Opened, PickedUp };

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
        // the map item done, 0 when more than one could be
        int32_t point = 0;
        // when point is 0: the ones that could be
        std::vector<int32_t> candidates;
    };

    // an item got this close to a gone pickup is taken with it
    inline constexpr uint64_t SAME_MOMENT = 200;

    class Finder {
    public:
        explicit Finder(Links::Links& links);
        ~Finder();

        // the items pack to find map items in, nothing is found without one
        void SetCatalog(std::shared_ptr<const ItemCatalog::Catalog> catalog);
        // map items that are done already, passed over
        void SetDone(std::unordered_set<int32_t> done);

        // world as it is after the event
        void Feed(const World::Event& event, const World::World& world, std::vector<Found>& out);
        // drops the pickups whose item can't come any more by ms
        void Settle(uint64_t ms);

    private:
        struct Gone {
            World::Object object;
            uint64_t ms = 0;
        };
        struct Got {
            uint64_t ms = 0;
            std::vector<std::pair<uint32_t, uint32_t>> items;
        };

        std::unordered_set<int> typesFor(int type) const;
        bool find(How how, const World::Object& object, uint64_t ms, const World::World& world, Found& out);
        bool gotItsItem(const Gone& gone, const Got& got) const;
        void gone(const World::Event& event, const World::World& world, std::vector<Found>& out);
        void got(const World::Event& event, const World::World& world, std::vector<Found>& out);

        Links::Links& m_links;
        std::shared_ptr<const ItemCatalog::Catalog> m_catalog;
        std::shared_ptr<const MarkerIndex::Index> m_index;
        Links::Types m_types;
        // found this run, and those with the ones done before
        std::unordered_set<int32_t> m_found, m_taken;
        std::set<std::pair<uint32_t, uint32_t>> m_foundObjects;
        std::deque<Gone> m_waiting;
        std::deque<Got> m_got;
    };

    // the items pack's links (game/links.json), then the ones the build ships
    extern Links::Known KnownLinks(const ItemCatalog::Catalog* catalog);
    // the votes of the learned file into links; a file that doesn't read is kept aside as
    // .broken, false when it couldn't be
    extern bool LoadVotes(const std::filesystem::path& file, Links::Links& links);

    // The Finder on the World's events, for the whole run. The links' votes are
    // kept in learned-links.json in the data folder. Start after World::Start.
    extern void Start(const std::filesystem::path& folder);
    extern void Stop();

    // Called on GameTraffic's worker thread for every find, keep it short.
    // Gives an id for RemoveListener.
    using Listener = std::function<void(const Found&)>;
    extern int AddListener(Listener listener);
    extern void RemoveListener(int id);
}

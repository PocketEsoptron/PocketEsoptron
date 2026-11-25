#include "regrow.hpp"
#include "game_server.hpp"
#include "game_tables.hpp"

namespace Regrow {
    static constexpr int64_t HOUR = 3600;

    static Rule after(int64_t hours) { return { Clock::AfterPickup, hours * HOUR, 0 }; }
    static Rule atServerHour(int hour) { return { Clock::ServerHour, 0, hour }; }

    // the gather table's refreshIds, kinds of their own
    static bool ofRefresh(uint32_t refresh, Rule& out) {
        switch (refresh) {
        case 999900: // common cooking ingredients and fruit
        case 999930: // Iron Chunk
            out = atServerHour(0);
            return true;
        case 999920: // Electro Crystal, Flaming and Mist Flowers
        case 999921: // local specialties
        case 999931: // White Iron, Starsilver
            out = after(48);
            return true;
        case 999932: // Crystal Chunk, Amethyst Lump
            out = after(72);
            return true;
        default:
            return false;
        }
    }

    Rule Of(uint32_t item, const GameTables::Tables& tables) {
        GameTables::Material material;
        bool known = tables.material(item, material);
        if (known && material.materialType == "MATERIAL_QUEST")
            return {};
        switch (item) {
        case 101004: return atServerHour(6);  // Magical Crystal, at mining outcrops
        case 100064:                           // Fowl
        case 100084:                           // Fish
        case 100085: return after(12);        // Crystal Core, from crystalflies
        }
        Rule rule;
        uint32_t refresh = tables.refreshOf(item);
        if (ofRefresh(refresh, rule))
            return rule;
        // a kind the gather table names that isn't listed above
        if (refresh)
            return after(48);
        if (!known)
            return {};
        if (material.rank > 310 && material.rank < 320)
            return after(48);
        if (material.rank == 321 || material.materialType == "MATERIAL_NOTICE_ADD_HP")
            return atServerHour(0);
        if (material.rank == 301 || material.rank == 320)
            return after(48);
        return {};
    }

    int64_t BackAt(const Rule& rule, int64_t pickedUp, const GameServer::Server* server) {
        switch (rule.clock) {
        case Clock::AfterPickup:
            return pickedUp + rule.seconds;
        case Clock::ServerHour:
            return server ? GameServer::nextAt(*server, rule.hour, pickedUp) : 0;
        default:
            return 0;
        }
    }
}

#pragma once
#include <cstdint>

namespace GameTables { class Tables; }
namespace GameServer { struct Server; }

// When a picked up thing grows back. The game keeps its own times on the
// server, so these follow what players measured (the fandom wiki's Reset page,
// November 2026):
//
//   local specialties, White Iron, Starsilver, Electro Crystal, Flaming and
//   Mist Flowers              48 h after the pickup
//   Crystal Chunk, Amethyst   72 h after the pickup
//   animals and insects       12 h after the pickup (Fowl, Fish, Crystal Core)
//   Iron Chunk and the common cooking ingredients
//                             at the next 0:00 on the server's clock
//   Magical Crystal           mining outcrops move at 6:00 on the server's clock
//
// The kind of an item: the refreshId the gather table gives any of its points,
// else its rank, which the game gives per kind (311 and up a local specialty,
// one per region; 321 a cooking ingredient; 301 an ore; 320 a material).
// A refreshId not listed here grows back 48 h after the pickup. Anything not
// sure to grow back (a quest item, an Oculus) never does.

namespace Regrow {
    enum class Clock { Never, AfterPickup, ServerHour };

    struct Rule {
        Clock clock = Clock::Never;
        // AfterPickup: this long after it
        int64_t seconds = 0;
        // ServerHour: the next time the server's clock shows this hour
        int hour = 0;

        bool grows() const { return clock != Clock::Never; }
        bool needsServer() const { return clock == Clock::ServerHour; }
    };

    extern Rule Of(uint32_t item, const GameTables::Tables& tables);

    // when it's back, in Unix seconds; 0 when it never is, or needs a server and has none
    extern int64_t BackAt(const Rule& rule, int64_t pickedUp, const GameServer::Server* server);
}

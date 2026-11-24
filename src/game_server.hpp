#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The game server the player plays on, picked by the player in the menu.
// "" is not picked. Extensions read it with the Game server permission.

namespace GameServer {
	struct Server {
		// saved in settings.json and given to Lua
		const char* key;
		const char* label;
		// the server's clock against UTC, in hours, all year
		int utcOffset;
	};

	// in the order the menu lists them
	const std::vector<Server>& list();
	// nullptr for "" and for a key this overlay doesn't know
	const Server* find(const std::string& key);
	// the picked one, nullptr when none is
	const Server* picked();

	// The game's day starts at 4:00 on the server's clock.
	inline const int ResetHour = 4;
	// the first daily reset after now, both in Unix seconds
	int64_t nextReset(const Server& server, int64_t now);

	// The "Game server" choice row, "Not picked" first. Saves the pick and
	// gives true when the player changed it.
	bool drawChoice();
}

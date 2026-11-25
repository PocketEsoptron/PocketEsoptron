#include "game_server.hpp"
#include "controls.hpp"
#include "settings.hpp"
#include <algorithm>
#include <atomic>

namespace GameServer {
	const std::vector<Server>& list() {
		static const std::vector<Server> servers = {
			{ "america", "America", -5 },
			{ "europe", "Europe", 1 },
			{ "asia", "Asia", 8 },
			{ "tw_hk_mo", "TW, HK, MO", 8 },
			{ "china", "China", 8 },
		};
		return servers;
	}

	const Server* find(const std::string& key) {
		for (const Server& server : list()) {
			if (key == server.key) return &server;
		}
		return nullptr;
	}

	const Server* picked() {
		return find(Settings::gameServer);
	}

	static std::atomic<const Server*> sharedServer{ nullptr };

	const Server* shared() {
		return sharedServer.load();
	}

	void share() {
		sharedServer.store(picked());
	}

	int64_t nextReset(const Server& server, int64_t now) {
		return nextAt(server, ResetHour, now);
	}

	int64_t nextAt(const Server& server, int hour, int64_t now) {
		const int64_t day = 86400;
		int64_t offset = (int64_t)server.utcOffset * 3600;
		int64_t local = now + offset;
		// floored, also before 1970
		int64_t dayStart = local >= 0 ? local / day * day : (local - day + 1) / day * day;
		int64_t at = dayStart + hour * 3600;
		if (at <= local) at += day;
		return at - offset;
	}

	bool drawChoice() {
		static std::vector<std::string> names;
		if (names.empty()) {
			names.push_back("Not picked");
			for (const Server& server : list()) names.push_back(server.label);
		}
		const Server* now = picked();
		int index = now ? (int)(now - list().data()) + 1 : 0;
		if (!Controls::Choice("Game server", index, names)) return false;
		Settings::gameServer = index > 0 ? list()[index - 1].key : "";
		share();
		Settings::Save();
		return true;
	}
}

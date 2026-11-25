#include "game_tables.hpp"
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace GameTables {
	static bool id(const json& row, const char* key, uint32_t& out) {
		auto found = row.find(key);
		if (found == row.end() || !found->is_number_integer()) return false;
		int64_t value = found->get<int64_t>();
		if (value < 0 || value > std::numeric_limits<uint32_t>::max()) return false;
		out = (uint32_t)value;
		return true;
	}

	static std::string text(const json& row, const char* key) {
		auto found = row.find(key);
		return found != row.end() && found->is_string() ? found->get<std::string>() : std::string();
	}

	uint16_t Tables::word(const std::string& text) {
		auto found = std::find(m_words.begin(), m_words.end(), text);
		if (found != m_words.end()) return (uint16_t)(found - m_words.begin());
		if (m_words.size() > std::numeric_limits<uint16_t>::max()) return 0;
		m_words.push_back(text);
		return (uint16_t)(m_words.size() - 1);
	}

	bool Tables::read(const std::string& name, std::string_view data) {
		if (name == LEARNED) {
			learned = data;
			return true;
		}
		if (name == SCENES) {
			scenes = data;
			return true;
		}
		if (name == LINKS) {
			links = data;
			return true;
		}
		if (name != GADGETS && name != GATHERS && name != MATERIALS) return false;
		json rows = json::parse(data, nullptr, false);
		if (!rows.is_array()) return false;
		if (name == GADGETS) {
			m_gadgets.clear();
			m_names.clear();
			for (const json& row : rows) {
				GadgetRow g;
				if (!row.is_object() || !id(row, "id", g.id)) continue;
				std::string jsonName = text(row, "jsonName");
				if (jsonName.size() <= std::numeric_limits<uint16_t>::max()) {
					g.nameAt = (uint32_t)m_names.size();
					g.nameSize = (uint16_t)jsonName.size();
					m_names += jsonName;
				}
				g.type = word(text(row, "type"));
				m_gadgets.push_back(g);
			}
			std::sort(m_gadgets.begin(), m_gadgets.end(), [](const GadgetRow& a, const GadgetRow& b) { return a.id < b.id; });
			m_gadgets.shrink_to_fit();
			m_names.shrink_to_fit();
		} else if (name == GATHERS) {
			m_gathers.clear();
			for (const json& row : rows) {
				Gather g;
				if (!row.is_object() || !id(row, "gadgetId", g.gadget) || !id(row, "itemId", g.item)) continue;
				id(row, "pointType", g.pointType);
				id(row, "refreshId", g.refresh);
				m_gathers.push_back(g);
			}
			std::stable_sort(m_gathers.begin(), m_gathers.end(), [](const Gather& a, const Gather& b) { return a.gadget < b.gadget; });
			m_gathers.shrink_to_fit();
		} else {
			m_materials.clear();
			m_knowsRanks = false;
			for (const json& row : rows) {
				MaterialRow m;
				if (!row.is_object() || !id(row, "id", m.id)) continue;
				m.itemType = word(text(row, "itemType"));
				m.materialType = word(text(row, "materialType"));
				m_knowsRanks |= id(row, "rank", m.rank);
				m.name = word(text(row, "name"));
				m_materials.push_back(m);
			}
			std::sort(m_materials.begin(), m_materials.end(), [](const MaterialRow& a, const MaterialRow& b) { return a.id < b.id; });
			m_materials.shrink_to_fit();
		}
		return true;
	}

	bool Tables::gadget(uint32_t id, Gadget& out) const {
		auto found = std::lower_bound(m_gadgets.begin(), m_gadgets.end(), id, [](const GadgetRow& g, uint32_t i) { return g.id < i; });
		if (found == m_gadgets.end() || found->id != id) return false;
		out.id = id;
		out.type = wordAt(found->type);
		out.name = std::string_view(m_names).substr(found->nameAt, found->nameSize);
		return true;
	}

	std::vector<Gadget> Tables::gadgetsOfType(std::string_view type) const {
		std::vector<Gadget> out;
		for (const GadgetRow& g : m_gadgets)
			if (wordAt(g.type) == type)
				out.push_back({ g.id, wordAt(g.type), std::string_view(m_names).substr(g.nameAt, g.nameSize) });
		return out;
	}

	bool Tables::material(uint32_t id, Material& out) const {
		auto found = std::lower_bound(m_materials.begin(), m_materials.end(), id, [](const MaterialRow& m, uint32_t i) { return m.id < i; });
		if (found == m_materials.end() || found->id != id) return false;
		out.id = id;
		out.itemType = wordAt(found->itemType);
		out.materialType = wordAt(found->materialType);
		out.rank = found->rank;
		out.name = wordAt(found->name);
		return true;
	}

	uint32_t Tables::refreshOf(uint32_t item) const {
		for (const Gather& g : m_gathers)
			if (g.item == item && g.refresh)
				return g.refresh;
		return 0;
	}

	std::vector<Gather> Tables::gathers(uint32_t gadget) const {
		auto range = std::equal_range(m_gathers.begin(), m_gathers.end(), Gather{ gadget },
			[](const Gather& a, const Gather& b) { return a.gadget < b.gadget; });
		return std::vector<Gather>(range.first, range.second);
	}

	size_t Tables::bytes() const {
		size_t words = 0;
		for (const std::string& w : m_words) words += sizeof(std::string) + w.capacity();
		return m_gadgets.capacity() * sizeof(GadgetRow) + m_gathers.capacity() * sizeof(Gather)
			+ m_materials.capacity() * sizeof(MaterialRow) + m_names.capacity() + words + learned.capacity() + scenes.capacity()
			+ links.capacity();
	}
}

#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The game's own tables, trimmed to the fields the overlay reads, plus the
// message ids the learner ships, the map formulas and the map item links, as
// the items pack has them under game/:
//
//   game/gadgets.json            [{id, type, jsonName}], from GadgetExcelConfigData
//   game/gathers.json            [{gadgetId, itemId, pointType, refreshId}], from GatherExcelConfigData
//   game/materials.json          [{id, itemType, materialType, rank}], from MaterialExcelConfigData
//   game/learned-messages.json   Learner::ToJson of the shipped ids
//   game/scenes.json             [{map, xx, xz, x0, yx, yz, y0}], Scene::Formula
//   game/links.json              [{gadget, type}], Links::KnownToJson of the shipped links
// The tables keep AnimeGameData's key names. A field the game leaves empty is
// missing (a gadget without a jsonName).

namespace GameTables {
	inline const std::string FOLDER = "game/";
	inline const std::string GADGETS = "game/gadgets.json";
	inline const std::string GATHERS = "game/gathers.json";
	inline const std::string MATERIALS = "game/materials.json";
	inline const std::string LEARNED = "game/learned-messages.json";
	inline const std::string SCENES = "game/scenes.json";
	inline const std::string LINKS = "game/links.json";

	struct Gadget {
		uint32_t id = 0;
		std::string_view type;  // "Chest", "GatherObject"; empty when the table has none
		std::string_view name;  // jsonName, "SceneObj_Chest_Default_Lv1"; empty when the table has none
	};

	struct Gather {
		uint32_t gadget = 0, item = 0, pointType = 0;
		// how it grows back, 0 when the table doesn't say (Regrow goes by the item then)
		uint32_t refresh = 0;
	};

	struct Material {
		uint32_t id = 0;
		std::string_view itemType, materialType;
		// the game sorts items by it, one per kind: 311 and up a local specialty, 321 a cooking ingredient
		uint32_t rank = 0;
	};

	class Tables {
	public:
		bool gadget(uint32_t id, Gadget& out) const;
		// every gadget of a type, "Chest", by id
		std::vector<Gadget> gadgetsOfType(std::string_view type) const;
		bool material(uint32_t id, Material& out) const;
		// the gather points that use this gadget
		std::vector<Gather> gathers(uint32_t gadget) const;
		size_t gadgetCount() const { return m_gadgets.size(); }
		size_t gatherCount() const { return m_gathers.size(); }
		size_t materialCount() const { return m_materials.size(); }
		// the refreshId any gather point of the item has, 0 when none does
		uint32_t refreshOf(uint32_t item) const;
		// the materials have their ranks, so Regrow can tell what grows back; a pack from before doesn't
		bool knowsRanks() const { return m_knowsRanks; }
		size_t bytes() const;

		// the files' text, empty when the pack has none
		std::string learned, scenes, links;

		// one game/ file of a pack; false for a name that isn't one, or a table that doesn't read
		bool read(const std::string& name, std::string_view data);

	private:
		struct GadgetRow {
			uint32_t id = 0;
			uint32_t nameAt = 0;
			uint16_t nameSize = 0;
			uint16_t type = 0;
		};
		struct MaterialRow {
			uint32_t id = 0;
			uint16_t itemType = 0, materialType = 0;
			uint32_t rank = 0;
		};
		std::vector<GadgetRow> m_gadgets;      // by id
		std::vector<Gather> m_gathers;         // by gadget
		std::vector<MaterialRow> m_materials;  // by id
		bool m_knowsRanks = false;
		std::string m_names;                   // every jsonName, one after the other
		std::vector<std::string> m_words{ std::string() };  // type names, once each; 0 is the empty one
		uint16_t word(const std::string& text);
		std::string_view wordAt(uint16_t i) const { return i < m_words.size() ? std::string_view(m_words[i]) : std::string_view(); }
	};
}

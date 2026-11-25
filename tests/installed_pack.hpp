#pragma once
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include "files.hpp"
#include "item_catalog.hpp"
#include "item_files.hpp"
#include "recorded_messages.hpp"

// a whole items pack with the game's tables, the one the recordings' answers were checked with; it
// comes in the test-recordings release next to the recordings (tests/recorded_messages.hpp)
inline std::shared_ptr<const ItemCatalog::Catalog> installedPack() {
    static std::shared_ptr<const ItemCatalog::Catalog> loaded = [] {
        std::filesystem::path file = std::filesystem::path(__FILE__).parent_path() / "recordings" / "items.pack";
        std::string text = Files::readText(file);
        size_t line = text.find('\n');
        if (line == std::string::npos && RecordedMessages::onCI())
            throw std::runtime_error("no tests/recordings/items.pack, the test-recordings release isn't unpacked");
        if (line == std::string::npos)
            return std::shared_ptr<const ItemCatalog::Catalog>();
        nlohmann::json meta = nlohmann::json::parse(text.substr(0, line), nullptr, false);
        std::string problem;
        return std::shared_ptr<const ItemCatalog::Catalog>(ItemFiles::open(text.substr(line + 1), meta, nullptr, problem));
    }();
    return loaded;
}

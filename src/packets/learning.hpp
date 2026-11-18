#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "learner.hpp"

namespace ItemCatalog { class Catalog; }

// The learner on GameTraffic's messages, for the whole run. What it learned
// is kept per game version in learned-messages.json in the data folder. The
// items pack's ids (game/learned-messages.json) take the lead, then the ones
// the build ships, then the file's.

namespace Learning {
    // folder: where learned-messages.json is kept
    extern void Start(const std::filesystem::path& folder);
    extern void Stop();

    struct Status {
        // empty until a login was seen
        std::string version;
        std::vector<Learner::Kind> learned;
    };
    extern Status GetStatus();

    // released, then the shipped ids, then the learned file, each kind from the first that has it;
    // saves is false when the file doesn't read and can't be moved aside
    extern Learner::Known Load(const std::filesystem::path& file, const Learner::Known& released = {}, bool* saves = nullptr);
    // the items pack's ids, empty without a pack or when it has none
    extern Learner::Known Released(const ItemCatalog::Catalog* catalog);
    // known without what's the same as in released
    extern bool Save(const std::filesystem::path& file, const Learner::Known& known, const Learner::Known& released = {});

    // Called on GameTraffic's worker thread for every event, keep it short.
    // Gives an id for RemoveListener.
    using Listener = std::function<void(const Learner::Event&)>;
    extern int AddListener(Listener listener);
    extern void RemoveListener(int id);
}

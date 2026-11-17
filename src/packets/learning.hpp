#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "learner.hpp"

// The learner on GameTraffic's messages, for the whole run. What it learned
// is kept per game version in learned-messages.json in the data folder, with
// the ids the build ships taking the lead.

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

    // the shipped ids with what the learned file holds over them; saves is false
    // when the file doesn't read and can't be moved aside
    extern Learner::Known Load(const std::filesystem::path& file, bool* saves = nullptr);
    extern bool Save(const std::filesystem::path& file, const Learner::Known& known);

    // Called on GameTraffic's worker thread for every event, keep it short.
    // Gives an id for RemoveListener.
    using Listener = std::function<void(const Learner::Event&)>;
    extern int AddListener(Listener listener);
    extern void RemoveListener(int id);
}

#include "learning.hpp"
#include <iostream>
#include <mutex>
#include "../files.hpp"
#include "../helpers.hpp"
#include "../item_catalog.hpp"
#include "game_traffic.hpp"

namespace Learning {
    static const char* FILE_NAME = "learned-messages.json";

    static std::mutex mutex;
    static Status status;
    static int trafficListener = 0;

    static std::mutex listenersMutex;
    static std::vector<std::pair<int, Listener>> listeners;
    static int nextListener = 1;

    Learner::Known Released(const ItemCatalog::Catalog* catalog) {
        Learner::Known known;
        if (catalog && !catalog->game.learned.empty() && !Learner::FromJson(catalog->game.learned, known))
            known.clear();
        return known;
    }

    Learner::Known Load(const std::filesystem::path& file, const Learner::Known& released, bool* saves) {
        if (saves)
            *saves = true;
        Learner::Known known = released;
        for (auto& [version, set] : Learner::Shipped())
            for (auto& [kind, learned] : set)
                known[version].insert({ kind, learned });
        std::string text = Files::readText(file);
        Learner::Known saved;
        if (!text.empty() && !Learner::FromJson(text, saved)) {
            // kept aside, so the next save doesn't lose what's in it
            std::filesystem::path broken = file;
            broken += ".broken";
            std::error_code error;
            std::filesystem::rename(file, broken, error);
            if (error) {
                if (saves)
                    *saves = false;
                std::cout << "Learned messages: " << Helpers::utf8(file.filename()) << " doesn't read and can't be moved, "
                          << "starting from the shipped ones and not saving" << std::endl;
            } else {
                std::cout << "Learned messages: " << Helpers::utf8(file.filename()) << " doesn't read, kept it as "
                          << Helpers::utf8(broken.filename()) << ", starting from the shipped ones" << std::endl;
            }
        }
        for (auto& [version, set] : saved)
            for (auto& [kind, learned] : set)
                known[version].insert({ kind, learned });
        return known;
    }

    bool Save(const std::filesystem::path& file, const Learner::Known& known, const Learner::Known& released) {
        Learner::Known own = known;
        for (const auto& [version, set] : released) {
            auto found = own.find(version);
            if (found == own.end())
                continue;
            for (const auto& [kind, learned] : set) {
                auto same = found->second.find(kind);
                if (same != found->second.end() && same->second == learned)
                    found->second.erase(same);
            }
            if (found->second.empty())
                own.erase(found);
        }
        return Files::writeFile(file, Learner::ToJson(own));
    }

    static void hand(const std::vector<Learner::Event>& events) {
        if (events.empty())
            return;
        std::vector<std::pair<int, Listener>> now;
        {
            std::lock_guard lock(listenersMutex);
            now = listeners;
        }
        for (const Learner::Event& event : events)
            for (auto& [id, listener] : now)
                listener(event);
    }

    void Start(const std::filesystem::path& folder) {
        std::lock_guard lock(mutex);
        if (trafficListener)
            return;
        std::filesystem::path file = folder / FILE_NAME;
        auto pack = ItemCatalog::current();
        auto catalog = std::make_shared<uint64_t>(pack ? pack->version : 0);
        auto released = std::make_shared<Learner::Known>(Released(pack.get()));
        bool saves = true;
        auto learner = std::make_shared<Learner::Learner>(Load(file, *released, &saves));
        auto events = std::make_shared<std::vector<Learner::Event>>();
        trafficListener = GameTraffic::AddListener([learner, events, file, catalog, released, saves](const Messages::Message& message) {
            if (ItemCatalog::version() != *catalog) {
                auto pack = ItemCatalog::current();
                *catalog = pack ? pack->version : 0;
                *released = Released(pack.get());
                learner->Release(*released, *events);
            }
            std::string version = learner->Version();
            learner->Feed(message, *events);
            if (learner->TakeChanged() || learner->Version() != version) {
                Status now;
                now.version = learner->Version();
                std::string line;
                for (auto& [kind, learned] : learner->LearnedNow()) {
                    now.learned.push_back(kind);
                    line += std::string(line.empty() ? "" : ", ") + Learner::KindCode(kind) + " " + std::to_string(learned.id);
                }
                std::cout << "Learned messages: " << (now.version.empty() ? "no version yet" : now.version) << ", "
                          << (line.empty() ? "none" : line) << std::endl;
                {
                    std::lock_guard lock(mutex);
                    status = now;
                }
                if (saves && !now.version.empty() && !Save(file, learner->All(), *released))
                    std::cout << "Learned messages: can't write " << Helpers::utf8(file) << std::endl;
            }
            hand(*events);
            events->clear();
        });
    }

    void Stop() {
        std::lock_guard lock(mutex);
        if (!trafficListener)
            return;
        GameTraffic::RemoveListener(trafficListener);
        trafficListener = 0;
        status = Status();
    }

    Status GetStatus() {
        std::lock_guard lock(mutex);
        return status;
    }

    int AddListener(Listener listener) {
        std::lock_guard lock(listenersMutex);
        int id = nextListener++;
        listeners.push_back({ id, std::move(listener) });
        return id;
    }

    void RemoveListener(int id) {
        std::lock_guard lock(listenersMutex);
        for (size_t i = 0; i < listeners.size(); i++)
            if (listeners[i].first == id) {
                listeners.erase(listeners.begin() + i);
                return;
            }
    }
}

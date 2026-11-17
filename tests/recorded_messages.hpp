#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <vector>
#include "packets/messages.hpp"

// Messages kept by the recorder (tests/data/learner/answers.md has the format):
// per message a u64 unix ms, a u8 side (0 received, 1 sent), a u16 id, a u32 size, the body.

namespace RecordedMessages {
    // whole recordings, text and UIDs blanked, too big for the repo: the test-recordings-1 release
    // unpacked here (the Tests workflow does it), one folder per recording
    inline const std::filesystem::path RECORDINGS = std::filesystem::path(__FILE__).parent_path() / "recordings";

    // GitHub's runners set CI, a missing recording fails there instead of skipping
    inline bool onCI() {
        char* value = nullptr;
        size_t size = 0;
        bool set = _dupenv_s(&value, &size, "CI") == 0 && value;
        free(value);
        return set;
    }

    // the recordings' folders that have a messages.bin; on CI they must be there
    inline std::vector<std::filesystem::path> recordings() {
        std::vector<std::filesystem::path> out;
        std::error_code error;
        for (auto& entry : std::filesystem::directory_iterator(RECORDINGS, error))
            if (std::filesystem::exists(entry.path() / "messages.bin"))
                out.push_back(entry.path());
        if (out.empty() && onCI())
            throw std::runtime_error("no recordings in tests/recordings, the test-recordings release isn't unpacked");
        return out;
    }

    inline std::vector<Messages::Message> read(const std::filesystem::path& path) {
        std::vector<Messages::Message> out;
        std::ifstream file(path, std::ios::binary);
        uint8_t head[15];
        while (file.read((char*)head, sizeof(head))) {
            Messages::Message m;
            for (int b = 0; b < 8; b++)
                m.ms |= uint64_t(head[b]) << (8 * b);
            m.direction = head[8] ? Frames::Direction::Sent : Frames::Direction::Received;
            m.id = uint16_t(head[9] | head[10] << 8);
            uint32_t size = 0;
            for (int b = 0; b < 4; b++)
                size |= uint32_t(head[11 + b]) << (8 * b);
            m.body.resize(size);
            if (!file.read(m.body.data(), size))
                break;
            out.push_back(std::move(m));
        }
        return out;
    }
}

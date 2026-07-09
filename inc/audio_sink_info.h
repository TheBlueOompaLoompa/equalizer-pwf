#pragma once

#include <cstdint>
#include <string>

class AudioSinkInfo {
public:
    uint32_t id;
    std::string nick;

    AudioSinkInfo(uint32_t id, std::string nick);
    ~AudioSinkInfo();
};


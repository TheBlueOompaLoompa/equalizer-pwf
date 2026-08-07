#pragma once
#include <cstdint>

struct PwDevice {
    uint32_t id;
    const char* desc;
    const char** channels;
    uint8_t n_channels;
};

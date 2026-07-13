#pragma once
#include <cstdint>
#include <string>

struct PwDevice {
    uint32_t id;
    const char* desc;
};

struct PwClient {
    uint32_t id;
};

struct PwNode {
    uint32_t id;
};

enum PwPortDirection {
    INPUT,
    OUTPUT
};

struct PwPort {
    uint32_t id = 0;
    uint32_t port_id;
    uint32_t node_id;

    std::string audio_channel;
    std::string format_dsp;
    PwPortDirection direction;
};

struct PwLink {

};

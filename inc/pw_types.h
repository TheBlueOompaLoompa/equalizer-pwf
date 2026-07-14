#pragma once
#include "pipewire/link.h"
#include "pipewire/node.h"
#include "pipewire/port.h"
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
    pw_node_info* info;
    void* data;
    spa_hook hook;
};

enum PwPortDirection {
    INPUT,
    OUTPUT
};

enum PwAudioChannel {
    FL,
    FR
};

struct PwPort {
    pw_port_info* info;
    void* data;
    spa_hook hook;
};

struct PwLink {
    pw_link_info* info;
    void* data;
    spa_hook hook;
};

#pragma once

#include <cstdint>
#include <unordered_map>
#include <pipewire/link.h>

#include "pipewire/node.h"
#include "pw_types.h"

struct proxy_data {
    pw_proxy* proxy = nullptr;
};

class Graph {
    uint32_t link_replace_target = 0;
public:
    void set_link_replace_target(uint32_t node_id);
    void on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    void on_global_reg_remove_event(uint32_t id);
    std::unordered_map<uint32_t, proxy_data> devices;
    std::unordered_map<uint32_t, proxy_data> ports;
    std::unordered_map<uint32_t, proxy_data> clients;
    std::unordered_map<uint32_t, proxy_data> nodes;
    std::unordered_map<uint32_t, proxy_data> links;
};

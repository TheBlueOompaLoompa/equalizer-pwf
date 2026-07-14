#pragma once

#include <cstdint>
#include <unordered_map>
#include <pipewire/link.h>

#include "pipewire/core.h"
#include "pipewire/node.h"
#include "pw_types.h"
#include "spa/utils/defs.h"

struct proxy_data {
    uint32_t id;
    pw_proxy* proxy = nullptr;
};

class Graph {
public:
    uint32_t filter_id = SPA_ID_INVALID;
    uint32_t sink_id = SPA_ID_INVALID;
    bool filter_linked = false;
    struct pw_core *core = nullptr;
    struct pw_context *context = nullptr;
    struct pw_registry *registry = nullptr;
    std::unordered_map<uint32_t, proxy_data> devices;
    std::unordered_map<uint32_t, PwPort> ports;
    std::unordered_map<uint32_t, proxy_data> clients;
    std::unordered_map<uint32_t, PwNode> nodes;
    std::unordered_map<uint32_t, PwLink> links;

    void close();

    void set_link_replace_target(uint32_t node_id);
    void on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    void on_global_reg_remove_event(uint32_t id);
};

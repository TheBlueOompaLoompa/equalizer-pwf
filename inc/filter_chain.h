#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <pipewire/filter.h>
#include <pipewire/proxy.h>
#include <wp/core.h>
#include <wp/node.h>

#include "command.h"

struct Port;

class FilterChain {
    WpCore* core;
    pw_core* core_pw;
    pw_registry* registry;
    WpNode* sink_node;
    pw_filter* filter;
    struct spa_hook filter_listener;
    WpObjectManager* om;
public:
    uint32_t device_node_id;
    uint32_t filter_id;
    uint32_t sink_id;
    uint32_t expected_ports_n;
    std::unordered_map<std::string, Port*> input_ports;
    std::unordered_map<std::string, Port*> output_ports;
    std::unordered_map<uint32_t, bool> stream_nodes;
    std::vector<Command>* commands;

    uint16_t processing_channels = 0x1ff;

    bool sink_bound = false;
    uint32_t filter_ports_bound = 0;
    uint32_t sink_ports_bound = 0;
    uint32_t device_ports_bound = 0;

    FilterChain(pw_core *pw_core, WpCore* wp_core, pw_registry* registry, WpObjectManager* om, gpointer object, uint32_t expected_ports_n, std::vector<Command>* commands);
    ~FilterChain();
    
    void process(Command& command, const std::string& channel, float* in, float* out, uint32_t n_samples);
    void update_filters();
    void update_ports();
    void maybe_create_links();
};

struct Port {
    FilterChain* chain;
};

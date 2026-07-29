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

#define GAIN(v) powf(10.0, v / 20.0)

struct Port;

class FilterChain {
    WpCore* core;
    pw_core* core_pw;
    pw_registry* registry;
    WpNode* sink_node;
    pw_filter* filter;
    struct spa_hook filter_listener;
    std::vector<Command>* commands;
    WpObjectManager* om;
public:
    uint32_t device_node_id;
    uint32_t filter_id;
    uint32_t sink_id;
    uint32_t expected_ports_n;
    std::unordered_map<std::string, Port*> input_ports;
    std::unordered_map<std::string, Port*> output_ports;
    std::unordered_map<uint32_t, bool> stream_nodes;

    bool sink_bound = false;
    uint32_t filter_ports_bound = 0;
    uint32_t sink_ports_bound = 0;
    uint32_t device_ports_bound = 0;

    FilterChain(pw_core *pw_core, WpCore* wp_core, pw_registry* registry, WpObjectManager* om, gpointer object, uint32_t expected_ports_n, std::vector<Command>* commands);
    ~FilterChain();
    
    void process(float* in, float* out, uint32_t n_samples, const std::string& channel);
    void update_ports();
    void maybe_create_links();
};

struct Port {
    FilterChain* chain;
};

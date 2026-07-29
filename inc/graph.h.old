#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>
#include <spa/utils/defs.h>
#include <wp/wp.h>

#include "pw_types.h"

class Graph {
public:
    uint32_t filter_id = SPA_ID_INVALID;
    uint32_t sink_id = SPA_ID_INVALID;
    bool filter_linked = false;
    WpCore *core = nullptr;
    WpObjectManager *om = nullptr;
    struct pw_registry *registry = nullptr;
    std::vector<PwDevice> devices;

    void close();
    void init(WpCore *wp_core, 
        void(*on_device_node_added)(WpObjectManager*, gpointer, void*),
        void(*on_device_node_removed)(WpObjectManager*, gpointer, void*),
        void(*on_port_changed)(uint32_t, void*),
        void* data
    );

    void on_object_added(gpointer object);
    void on_object_removed(gpointer object);
private:
    void* init_data;
    void (*device_node_added_cb)(WpObjectManager*, gpointer, void*) = nullptr;
    void (*device_node_removed_cb)(WpObjectManager*, gpointer, void*) = nullptr;
    void (*port_changed_cb)(uint32_t, void*) = nullptr;
    std::vector<uint32_t> nodeless_port_change_queue;
    std::unordered_set<uint32_t> audio_sink_ids;
    void rebuild_device_list();

    void on_port_added(uint32_t node_id);
};

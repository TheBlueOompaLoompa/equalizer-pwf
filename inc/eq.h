#pragma once
#include <mutex>
#include <pipewire/core.h>
#include <pipewire/proxy.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <pipewire/filter.h>
#include <wp/wp.h>

#include "command.h"

#include "filter_chain.h"
#include "msg.h"
#include "channel.h"
#include "pw_types.h"

using Filter = signalsmith::filters::BiquadStatic<float, true>;
using FilterDesign = signalsmith::filters::BiquadDesign;

class Equalizer {
public:
    GMainLoop *main_loop = nullptr;
    WpCore *core;
    Channel<Msg>* eq_channel;
    Channel<Msg>* ui_channel;

    std::vector<Command> commands;
    std::mutex commands_mutex;

    std::vector<FilterChain> chains;

    Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch);
    ~Equalizer();

    void loop();
    void on_timeout();
    void on_object_added(gpointer object);
    void on_object_removed(gpointer object);

private:
    std::vector<PwDevice> devices;

    struct pw_core* pw_core;
    struct pw_registry* registry;
    WpObjectManager *om = nullptr;

    GSource* timer_source = nullptr;
    std::unordered_map<uint32_t, FilterChain*> filter_chains;
    std::vector<uint32_t> nodeless_port_change_queue;
    std::unordered_set<uint32_t> audio_sink_ids;
    std::unordered_set<uint32_t> pwf_node_ids;

    void rebuild_device_list();
    void on_device_node_added(gpointer object);
    void on_device_node_removed(gpointer object);
    void on_port_changed(uint32_t node_id);
    void on_port_added(uint32_t node_id);
    static gboolean timeout(gpointer data);
};

#pragma once
#include <mutex>
#include <pipewire/proxy.h>
#include <unordered_map>
#include <vector>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <pipewire/filter.h>
#include <wp/wp.h>

#include "command.h"

#include "filter_chain.h"
#include "msg.h"
#include "channel.h"
#include "graph.h"

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

    Graph graph;

    Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch);
    ~Equalizer();

    void loop();
    void on_timeout();
    void on_device_node_added(WpObjectManager* om, gpointer object);
    void on_device_node_removed(WpObjectManager* om, gpointer object);
    void on_port_changed(uint32_t node_id);

private:
    std::unordered_map<uint32_t, FilterChain*> filter_chains;
    struct pw_core *pw_core;
    struct spa_hook filter_listener;
    GSource* timer_source = nullptr;
    static gboolean timeout(gpointer data);
};

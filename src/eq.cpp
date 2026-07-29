#include "eq.h"
#include "pipewire/core.h"
#include "pipewire/filter.h"
#include "pipewire/keys.h"
#include "pipewire/node.h"
#include "spa/utils/dict.h"
#include <cmath>
#include <glib.h>
#include <wp/node.h>
#include <wp/properties.h>
#include <wp/wp.h>

Equalizer::Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch):
eq_channel(eq_ch), ui_channel(ui_ch), graph() {}

Equalizer::~Equalizer() {}

static void device_node_added(WpObjectManager* om, gpointer object, void* data) {
    static_cast<Equalizer*>(data)->on_device_node_added(om, object);
}
void Equalizer::on_device_node_added(WpObjectManager* om, gpointer object) {
    uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
    uint32_t n_inputs = wp_node_get_n_input_ports(WP_NODE(object), nullptr);
    filter_chains.insert_or_assign(id, new FilterChain(pw_core, core, om, object, n_inputs, &commands));
}

static void device_node_removed(WpObjectManager* om, gpointer object, void* data) {
    static_cast<Equalizer*>(data)->on_device_node_removed(om, object);
}
void Equalizer::on_device_node_removed(WpObjectManager* om, gpointer object) {
    uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
    delete filter_chains[id];
    filter_chains.erase(id);
}

static void port_changed(uint32_t node_id, void* data) {
    static_cast<Equalizer*>(data)->on_port_changed(node_id);
}
void Equalizer::on_port_changed(uint32_t node_id) {
    if(filter_chains.find(node_id) != filter_chains.end()) {
        filter_chains[node_id]->update_ports();
    } 
}

void Equalizer::loop() {
    wp_init(WP_INIT_PIPEWIRE);

    core = wp_core_new(nullptr, nullptr, nullptr);
    wp_core_connect(core);
    pw_core = wp_core_get_pw_core(core);

    graph.init(core, device_node_added, device_node_removed, port_changed, this);

    wp_core_timeout_add(core, &timer_source, 20, timeout, this, NULL);

    main_loop = g_main_loop_new(wp_core_get_g_main_context(core), FALSE);
    g_main_loop_run(main_loop);

    g_source_destroy(timer_source);

    if(graph.registry) {
        pw_proxy_destroy((struct pw_proxy*)graph.registry);
        graph.registry = nullptr;
    }

    wp_core_disconnect(core);

    if(graph.om) {
        g_object_unref(graph.om);
        graph.om = nullptr;
    }

    g_main_loop_unref(main_loop);
}

void Equalizer::on_timeout() {
    Msg* msg = eq_channel->receive();
    if(msg != nullptr) {
        switch(msg->type) {
        case MsgType::QUIT:
            graph.close();
            g_main_loop_quit(main_loop);
            break;
        case MsgType::DEVICE_LIST:
            ui_channel->send({
                .type = MsgType::DEVICE_LIST,
                .data = &graph.devices
            });
            break;
        case MsgType::UPSERT_COMMAND:
            //commands.size()
            break;
        default:
            printf("Msg type %i Eq unimplemented\n", msg->type);
            break;
        }
    }
}

gboolean Equalizer::timeout(gpointer data) {
    Equalizer* eq = static_cast<Equalizer*>(data);
    eq->on_timeout();
    return G_SOURCE_CONTINUE;
}

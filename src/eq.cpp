#include "eq.h"
#include "command.h"
#include "pipewire/core.h"
#include "pipewire/filter.h"
#include "pipewire/keys.h"
#include "pipewire/node.h"
#include "spa/utils/dict.h"
#include "util.h"
#include <cmath>
#include <glib.h>
#include <iostream>
#include <wp/node.h>
#include <wp/port.h>
#include <wp/properties.h>
#include <wp/wp.h>

Equalizer::Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch):
eq_channel(eq_ch), ui_channel(ui_ch) {}

Equalizer::~Equalizer() {}

void Equalizer::on_device_node_added(gpointer object) {
    uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
    uint32_t n_inputs = wp_node_get_n_input_ports(WP_NODE(object), nullptr);
    filter_chains.insert_or_assign(id, new FilterChain(pw_core, core, registry, om, object, n_inputs, &commands));
    FilterChain* chain = filter_chains[id];
}

void Equalizer::on_device_node_removed(gpointer object) {
    uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
    delete filter_chains[id];
    filter_chains.erase(id);
}

void Equalizer::on_port_changed(uint32_t node_id) {
    if(filter_chains.find(node_id) != filter_chains.end()) {
        filter_chains[node_id]->update_ports();
    } 
}

static void object_added_cb(WpObjectManager *om, gpointer object, gpointer data) {
    static_cast<Equalizer*>(data)->on_object_added(object);
}

static void object_removed_cb(WpObjectManager *om, gpointer object, gpointer data) {
    static_cast<Equalizer*>(data)->on_object_removed(object);
}

void Equalizer::loop() {
    wp_init(WP_INIT_PIPEWIRE);

    core = wp_core_new(nullptr, nullptr, nullptr);
    wp_core_connect(core);
    pw_core = wp_core_get_pw_core(core);
    registry = pw_core_get_registry(pw_core, PW_VERSION_REGISTRY, 0);

    om = wp_object_manager_new();

    wp_object_manager_add_interest(om, WP_TYPE_LINK, NULL);
    wp_object_manager_request_object_features(om, WP_TYPE_LINK,
        WP_PIPEWIRE_OBJECT_FEATURE_INFO);

    wp_object_manager_add_interest(om, WP_TYPE_PORT, NULL);
    wp_object_manager_request_object_features(om, WP_TYPE_PORT,
        WP_PIPEWIRE_OBJECT_FEATURE_INFO);

    wp_object_manager_add_interest(om, WP_TYPE_NODE, NULL);
    wp_object_manager_request_object_features(om, WP_TYPE_NODE,
        WP_PIPEWIRE_OBJECT_FEATURE_INFO | WP_NODE_FEATURE_PORTS);

    g_signal_connect(om, "object-added", G_CALLBACK(object_added_cb), this);
    g_signal_connect(om, "object-removed", G_CALLBACK(object_removed_cb), this);

    wp_core_install_object_manager(core, om);

    wp_core_timeout_add(core, &timer_source, 20, timeout, this, NULL);

    main_loop = g_main_loop_new(wp_core_get_g_main_context(core), FALSE);
    g_main_loop_run(main_loop);

    g_source_destroy(timer_source);

    if(registry) {
        pw_proxy_destroy((struct pw_proxy*)registry);
        registry = nullptr;
    }

    wp_core_disconnect(core);

    if(om) {
        g_object_unref(om);
        om = nullptr;
    }

    g_main_loop_unref(main_loop);
}

void Equalizer::on_timeout() {
    Msg* msg = eq_channel->receive();
    if(msg != nullptr) {
        switch(msg->type) {
        case MsgType::QUIT:
            g_main_loop_quit(main_loop);
            break;
        case MsgType::DEVICE_LIST:
            ui_channel->send({
                .type = MsgType::DEVICE_LIST,
                .data = &devices
            });
            break;
        case MsgType::COMMANDS_CHANGED:
            for(auto& chain : filter_chains) {
                chain.second->update_filters();
            }
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

void Equalizer::rebuild_device_list() {
    for(auto &dev : devices)
        free((void*)dev.desc);
    devices.clear();

    WpIterator *it = wp_object_manager_new_filtered_iterator(om,
        WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_MEDIA_CLASS,
        "=s", "Audio/Sink",
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_DESCRIPTION,
        "!s", "Equalizer PWF Sink",
        NULL);
    if(!it) return;

    GValue item = G_VALUE_INIT;
    while(wp_iterator_next(it, &item)) {
        gpointer object = g_value_get_object(&item);
        if(!object || !WP_IS_NODE(object)) { g_value_unset(&item); continue; }
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            { g_value_unset(&item); continue; }

        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char *desc_str = wp_properties_get(props, PW_KEY_NODE_DESCRIPTION);
        uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));

        if(desc_str) {
            size_t len = strlen(desc_str);
            char *desc_copy = (char*)malloc(len + 1);
            memcpy(desc_copy, desc_str, len + 1);
            devices.push_back({ id, desc_copy });
        }
        wp_properties_unref(props);
        g_value_unset(&item);
    }
    wp_iterator_unref(it);
}

void Equalizer::on_object_added(gpointer object) {
    if(WP_IS_NODE(object)) {
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            return;

        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char *name = wp_properties_get(props, PW_KEY_NODE_NAME);
        const char *desc = wp_properties_get(props, PW_KEY_NODE_DESCRIPTION);
        const char *media_class = wp_properties_get(props, PW_KEY_MEDIA_CLASS);

        uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
        const char *dev_id_str = wp_properties_get(props, "target-device-id");
        if(name && strcmp(name, "equalizer-pwf-filter") == 0 && dev_id_str != nullptr) {
            uint32_t dev_id = strtoul(dev_id_str, nullptr, 10);
            pwf_node_ids.insert(id);
            filter_chains[dev_id]->filter_id = id;
        }else if(desc && strcmp(desc, "Equalizer PWF Sink") == 0) {
            pwf_node_ids.insert(id);
        }else if(media_class && strcmp(media_class, "Audio/Sink") == 0) {
            audio_sink_ids.insert(id);
            on_device_node_added(object);
        }

        for(int i = 0; i < nodeless_port_change_queue.size(); i++) {
            if(nodeless_port_change_queue[i] == id) {
                on_port_added(id);
                nodeless_port_change_queue.erase(nodeless_port_change_queue.begin()+i);
                i--;
            }
        }

        wp_properties_unref(props);
        rebuild_device_list();
    }else if(WP_IS_PORT(object)) {
        WpProperties* props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char* node_id_str = wp_properties_get(props, PW_KEY_NODE_ID);
        if(node_id_str) {
            uint32_t port_node_id = strtoul(node_id_str, nullptr, 10);
            on_port_added(port_node_id);
        }
        wp_properties_unref(props);
    }else if(WP_IS_LINK(object)) {
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            return;

        guint32 output_node, output_port, input_node, input_port;
        wp_link_get_linked_object_ids(WP_LINK(object), &output_node, &output_port, &input_node, &input_port);

        if(pwf_node_ids.find(input_node) != pwf_node_ids.end() || 
            pwf_node_ids.find(output_node) != pwf_node_ids.end())
            return;

        if(filter_chains.find(input_node) == filter_chains.end()) return;
        FilterChain* chain = filter_chains[input_node];
        if(chain->stream_nodes.find(output_node) == chain->stream_nodes.end()) {
            filter_chains[input_node]->stream_nodes.insert_or_assign(output_node, false);
            WpProxy* proxy = WP_PROXY(object);
            if(proxy) {
                std::cout << "Intercepting link " << output_node << " -> " << input_node << std::endl;
                pw_registry_destroy(registry, wp_proxy_get_bound_id(proxy));
            }
        }

        if(chain->stream_nodes[output_node] == false){
            for(int i = 0; i < chain->expected_ports_n; i++) {
                WpProperties *props = wp_properties_new(
                    PW_KEY_LINK_OUTPUT_NODE, std::to_string(output_node).c_str(),
                    PW_KEY_LINK_OUTPUT_PORT, std::to_string(i).c_str(),
                    PW_KEY_LINK_INPUT_NODE,  std::to_string(chain->sink_id).c_str(),
                    PW_KEY_LINK_INPUT_PORT,  std::to_string(i).c_str(),
                    NULL);
                WpLink *link = wp_link_new_from_factory(core, "link-factory", props);
                if(link)
                    wp_object_activate(WP_OBJECT(link), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);
            }
            filter_chains[input_node]->stream_nodes.insert_or_assign(output_node, true);
        }
    }
}

void Equalizer::on_object_removed(gpointer object) {
    if(WP_IS_NODE(object)) {
        uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
        if(audio_sink_ids.erase(id) > 0) {
            on_device_node_removed(object);
            rebuild_device_list();
        }

        for(auto& chain : filter_chains) {
            chain.second->stream_nodes.erase(id);
        }
    }else if(WP_IS_PORT(object)) {
        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char *node_id_str = wp_properties_get(props, "node.id");
        if(node_id_str) {
            uint32_t port_node_id = strtoul(node_id_str, nullptr, 10);
            on_port_changed(port_node_id);
        }
    }else if(WP_IS_LINK(object)) {
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            return;

        guint32 output_node, output_port, input_node, input_port;
        wp_link_get_linked_object_ids(WP_LINK(object), &output_node, &output_port, &input_node, &input_port);
        
        if(filter_chains.find(input_node) != filter_chains.end()) {
            FilterChain* chain = filter_chains[input_node];
            if(chain->stream_nodes[output_node] == false){
                for(int i = 0; i < chain->expected_ports_n; i++) {
                    WpProperties *props = wp_properties_new(
                        PW_KEY_LINK_OUTPUT_NODE, std::to_string(output_node).c_str(),
                        PW_KEY_LINK_OUTPUT_PORT, std::to_string(i).c_str(),
                        PW_KEY_LINK_INPUT_NODE,  std::to_string(chain->sink_id).c_str(),
                        PW_KEY_LINK_INPUT_PORT,  std::to_string(i).c_str(),
                        NULL);
                    WpLink *link = wp_link_new_from_factory(core, "link-factory", props);
                    if(link)
                        wp_object_activate(WP_OBJECT(link), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);
                }
                filter_chains[input_node]->stream_nodes.insert_or_assign(output_node, true);
            }
        }
    }
}

void Equalizer::on_port_added(uint32_t node_id) {
    WpNode* node = WP_NODE(wp_object_manager_lookup(om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id",
        "=u", node_id,
        NULL));
    if(!node) {
        std::cout << "No node" << std::endl;
        nodeless_port_change_queue.push_back(node_id);
        return;
    }
    WpProperties* node_props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(node));
    const char* target_device_id_str = wp_properties_get(node_props, "target-device-id");
    const char* node_name = wp_properties_get(node_props, "node.name");
    if(target_device_id_str) {
        if(strcmp(node_name, "equalizer-pwf-filter") == 0) {
            uint32_t target_device_id = strtoul(target_device_id_str, nullptr, 10);
            filter_chains[target_device_id]->filter_ports_bound++;
            on_port_changed(target_device_id);
        }else if(strcmp(node_name, "equalizer-pwf-sink") == 0) {
            uint32_t target_device_id = strtoul(target_device_id_str, nullptr, 10);
            filter_chains[target_device_id]->sink_ports_bound++;
            on_port_changed(target_device_id);
        }
    }else if(filter_chains.find(node_id) != filter_chains.end()) {
        filter_chains[node_id]->device_ports_bound++;
    }

    wp_properties_unref(node_props);
}

/*
void Graph::close() {
    if(!om) return;

    WpIterator *it = wp_object_manager_new_filtered_iterator(om, WP_TYPE_LINK, NULL);
    if(!it) return;

    struct LinkInfo {
        guint32 output_node;
        guint32 input_node;
    };
    std::vector<LinkInfo> left_sides;
    LinkInfo right_side = { SPA_ID_INVALID, SPA_ID_INVALID };

    GValue item = G_VALUE_INIT;
    while(wp_iterator_next(it, &item)) {
        gpointer object = g_value_get_object(&item);
        if(!object || !WP_IS_LINK(object)) { g_value_unset(&item); continue; }
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            { g_value_unset(&item); continue; }

        guint32 out_node, out_port, in_node, in_port;
        wp_link_get_linked_object_ids(WP_LINK(object), &out_node, &out_port, &in_node, &in_port);

        if(in_node == filter_id)
            left_sides.push_back({ out_node, in_node });
        if(out_node == filter_id)
            right_side = { out_node, in_node };
        g_value_unset(&item);
    }
    g_object_unref(it);

    if(right_side.input_node == SPA_ID_INVALID)
        return;

    for(const auto &link : left_sides) {
        WpProperties *props0 = wp_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(link.output_node).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "0",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(right_side.input_node).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "0",
            NULL);
        WpLink *new_link0 = wp_link_new_from_factory(core, "link-factory", props0);
        if(new_link0)
            wp_object_activate(WP_OBJECT(new_link0), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);

        WpProperties *props1 = wp_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(link.output_node).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "1",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(right_side.input_node).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "1",
            NULL);
        WpLink *new_link1 = wp_link_new_from_factory(core, "link-factory", props1);
        if(new_link1)
            wp_object_activate(WP_OBJECT(new_link1), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);
    }

}*/

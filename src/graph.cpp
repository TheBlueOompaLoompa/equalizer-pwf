#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <glib-object.h>
#include <iostream>
#include <pipewire/keys.h>
#include <pipewire/proxy.h>
#include <string>
#include <wp/iterator.h>
#include <wp/link.h>
#include <wp/node.h>
#include <wp/object-interest.h>
#include <wp/object-manager.h>
#include <wp/object.h>
#include <wp/properties.h>
#include <wp/proxy-interfaces.h>
#include <wp/proxy.h>
#include "graph.h"
#include "util.h"

static void object_added_cb(WpObjectManager *om, gpointer object, gpointer data) {
    static_cast<Graph*>(data)->on_object_added(object);
}

static void object_removed_cb(WpObjectManager *om, gpointer object, gpointer data) {
    static_cast<Graph*>(data)->on_object_removed(object);
}

void Graph::init(WpCore *wp_core, 
        void(*on_device_node_added)(WpObjectManager*, gpointer, void*),
        void(*on_device_node_removed)(WpObjectManager*, gpointer, void*),
        void(*on_filter_node_added)(WpObjectManager*, gpointer, void*),
        void(*on_sink_node_added)(WpObjectManager*, gpointer, void*),
        void(*on_port_changed)(uint32_t, void*),
        void* data
    ) {
    core = wp_core;
    struct pw_core *pw_core = wp_core_get_pw_core(core);
    registry = pw_core_get_registry(pw_core, PW_VERSION_REGISTRY, 0);

    om = wp_object_manager_new();

    init_data = data;
    device_node_added_cb = on_device_node_added;
    device_node_removed_cb = on_device_node_removed;
    filter_node_added_cb = on_filter_node_added;
    sink_node_added_cb = on_sink_node_added;
    port_changed_cb = on_port_changed;


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
}


void Graph::rebuild_device_list() {
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

void Graph::on_object_added(gpointer object) {
    if(WP_IS_NODE(object)) {
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            return;

        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char *name = wp_properties_get(props, PW_KEY_NODE_NAME);
        const char *desc = wp_properties_get(props, PW_KEY_NODE_DESCRIPTION);
        const char *media_class = wp_properties_get(props, PW_KEY_MEDIA_CLASS);

        if(name && strcmp(name, "equalizer-pwf-filter") == 0) {
            uint32_t id = strtoul(wp_properties_get(props, "target-device-id"), nullptr, 10);
            Util::get_node_ports(om, object, wp_di)

        }else if(desc && strcmp(desc, "Equalizer PWF Sink") == 0) {
            uint32_t id = strtoul(wp_properties_get(props, "target-device-id"), nullptr, 10);
            std::cout << "Sink node found, id=" << id << std::endl;
        }else if(media_class && strcmp(media_class, "Audio/Sink") == 0) {
            uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
            audio_sink_ids.insert(id);
            device_node_added_cb(om, object, init_data);
        }

        uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
        for(auto &queue_id : nodeless_port_change_queue) {
            if(queue_id == id) {
                on_port_added(id);
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
            /*if(port_node_id == sink_id && filter_id != SPA_ID_INVALID) {
                uint32_t port_id = wp_proxy_get_bound_id(WP_PROXY(object));
                WpProperties *link_props = wp_properties_new(
                    PW_KEY_LINK_OUTPUT_NODE, std::to_string(sink_id).c_str(),
                    PW_KEY_LINK_OUTPUT_PORT, std::to_string(port_id).c_str(),
                    PW_KEY_LINK_INPUT_NODE,  std::to_string(filter_id).c_str(),
                    PW_KEY_LINK_INPUT_PORT,  std::to_string(port_id).c_str(),
                    NULL);
                WpLink *link = wp_link_new_from_factory(core, "link-factory", link_props);
                if(link)
                    wp_object_activate(WP_OBJECT(link), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);
            }*/
        }
        wp_properties_unref(props);
    }
    else if(WP_IS_LINK(object)) {
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            return;

        guint32 output_node, output_port, input_node, input_port;
        wp_link_get_linked_object_ids(WP_LINK(object), &output_node, &output_port, &input_node, &input_port);

        if(filter_id == SPA_ID_INVALID || sink_id == SPA_ID_INVALID)
            return;

        if(filter_id == input_node || filter_id == output_node)
            return;
        if(sink_id == input_node || sink_id == output_node)
            return;

        if(audio_sink_ids.find(input_node) == audio_sink_ids.end())
            return;

        WpProxy* proxy = WP_PROXY(object);
        if(proxy) {
            std::cout << "Intercepting link " << output_node << " -> " << input_node << std::endl;
            pw_registry_destroy(registry, wp_proxy_get_bound_id(proxy));
        }

        if(!filter_linked) {
            WpProperties *rprops0 = wp_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, std::to_string(filter_id).c_str(),
                PW_KEY_LINK_OUTPUT_PORT, "0",
                PW_KEY_LINK_INPUT_NODE,  std::to_string(input_node).c_str(),
                PW_KEY_LINK_INPUT_PORT,  "0",
                NULL);
            WpLink *rlink0 = wp_link_new_from_factory(core, "link-factory", rprops0);
            if(rlink0)
                wp_object_activate(WP_OBJECT(rlink0), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);

            WpProperties *rprops1 = wp_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, std::to_string(filter_id).c_str(),
                PW_KEY_LINK_OUTPUT_PORT, "1",
                PW_KEY_LINK_INPUT_NODE,  std::to_string(input_node).c_str(),
                PW_KEY_LINK_INPUT_PORT,  "1",
                NULL);
            WpLink *rlink1 = wp_link_new_from_factory(core, "link-factory", rprops1);
            if(rlink1)
                wp_object_activate(WP_OBJECT(rlink1), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);

            filter_linked = true;
        }

        WpProperties *lprops0 = wp_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(output_node).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "0",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(sink_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "0",
            NULL);
        WpLink *llink0 = wp_link_new_from_factory(core, "link-factory", lprops0);
        if(llink0)
            wp_object_activate(WP_OBJECT(llink0), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);

        WpProperties *lprops1 = wp_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(output_node).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "1",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(sink_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "1",
            NULL);
        WpLink *llink1 = wp_link_new_from_factory(core, "link-factory", lprops1);
        if(llink1)
            wp_object_activate(WP_OBJECT(llink1), WP_PROXY_FEATURE_BOUND, NULL, NULL, NULL);
    }
}

void Graph::on_object_removed(gpointer object) {
    if(WP_IS_NODE(object)) {
        uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
        if(audio_sink_ids.erase(id) > 0) {
            device_node_removed_cb(om, object, init_data);
            rebuild_device_list();
        }
    }else if(WP_IS_PORT(object)) {
        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
        const char *node_id_str = wp_properties_get(props, "node.id");
        if(node_id_str) {
            uint32_t port_node_id = strtoul(node_id_str, nullptr, 10);
            port_changed_cb(port_node_id, init_data);
        }
    }
}

void Graph::on_port_added(uint32_t node_id) {
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
    if(node_name) std::cout << node_name << std::endl;
    if(target_device_id_str && strcmp(node_name, "equalizer-pwf-filter") == 0) {
        std::cout << wp_node_get_n_input_ports(node, nullptr) << std::endl;
        uint32_t target_device_id = strtoul(target_device_id_str, nullptr, 10);
        port_changed_cb(target_device_id, init_data);
    }

    wp_properties_unref(node_props);
}

void Graph::close() {
    if(!om) return;

    /*WpIterator *it = wp_object_manager_new_filtered_iterator(om, WP_TYPE_LINK, NULL);
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
    }*/

}

#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <pipewire/keys.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "graph.h"
#include "pipewire/core.h"
#include "pipewire/device.h"
#include "pipewire/link.h"
#include "pipewire/node.h"
#include "pipewire/port.h"
#include "pw_types.h"
#include "spa/utils/dict.h"
#include "spa/utils/hook.h"

template<typename T>
bool spa_dict_get_num(const spa_dict *props, const char* key, T& num) {
    const char* val = spa_dict_lookup(props, key); 
    if(val != nullptr) {
        auto str = std::string(val);
        
        std::from_chars(str.data(), str.data() + str.size(), num);
        return true;
    }

    return false;
}

static void on_link_info(void *data, const struct pw_link_info *info) {
    PwLink* link = static_cast<PwLink*>(data);
    Graph* graph = static_cast<Graph*>(link->data);

    link->info = (pw_link_info*)info;
    if(graph->nodes.find(info->input_node_id) != graph->nodes.end()) {
        if(graph->filter_id == info->input_node_id || graph->filter_id == info->output_node_id) return;

        pw_registry_destroy(graph->registry, info->id);

        bool has_output_conn = false;
        if(graph->filter_linked) {
            for(const auto &l : graph->links) {
                if(l.second.info->output_node_id == graph->filter_id && l.second.info->input_node_id == info->input_node_id) has_output_conn = true;
            }
        }
        graph->filter_linked = true;
        if(!has_output_conn) {
            struct pw_properties *rprops0 = pw_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, std::to_string(graph->filter_id).c_str(),
                PW_KEY_LINK_OUTPUT_PORT, "0",
                PW_KEY_LINK_INPUT_NODE,  std::to_string(info->input_node_id).c_str(),
                PW_KEY_LINK_INPUT_PORT,  "0",
                nullptr);

            struct pw_properties *rprops1 = pw_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, std::to_string(graph->filter_id).c_str(),
                PW_KEY_LINK_OUTPUT_PORT, "1",
                PW_KEY_LINK_INPUT_NODE,  std::to_string(info->input_node_id).c_str(),
                PW_KEY_LINK_INPUT_PORT,  "1",
                nullptr);

            pw_proxy* rlink0 = (pw_proxy*)pw_core_create_object(
                graph->core,
                "link-factory",
                PW_TYPE_INTERFACE_Link,
                PW_VERSION_LINK,
                &rprops0->dict,
                0);

            pw_proxy* rlink1 = (pw_proxy*)pw_core_create_object(
                graph->core,
                "link-factory",
                PW_TYPE_INTERFACE_Link,
                PW_VERSION_LINK,
                &rprops1->dict,
                0);
            }

        struct pw_properties *lprops0 = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(info->output_node_id).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "0",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(graph->filter_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "0",
            nullptr);

        struct pw_properties *lprops1 = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(info->output_node_id).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "1",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(graph->filter_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "1",
            nullptr);

        pw_proxy* llink0 = (pw_proxy*)pw_core_create_object(
            graph->core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &lprops0->dict,
            0);

        pw_proxy* llink1 = (pw_proxy*)pw_core_create_object(
            graph->core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &lprops1->dict,
            0);
        std::cout << graph->filter_id << "Output link found " << info->id << " output " << info->input_node_id << std::endl; 
    }
}

static void on_node_info(void *data, const struct pw_node_info *info) {
    PwNode* node = static_cast<PwNode*>(data);
    Graph* graph = static_cast<Graph*>(node->data);

    graph->nodes[info->id].info = (pw_node_info*)info;
}

static const pw_link_events link_events = {
    .version = PW_VERSION_LINK_EVENTS,
    .info = on_link_info
};

static const pw_node_events node_events = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = on_node_info
};

void Graph::on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
    if(strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        links.insert_or_assign(id, PwLink {
            .info = nullptr,
            .data = this
        });
        pw_link* link = (pw_link*)pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Link, version, 0);
        pw_link_add_listener(link, &links[id].hook, &link_events, &links[id]);
    }else if(strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if(desc && strcmp(desc, "Equalizer PWF") == 0) filter_id = id;
        const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if(media_class && strcmp(media_class, "Audio/Sink") == 0) {
            nodes.insert_or_assign(id, PwNode {
                .info = nullptr,
                .data = this
            });

            pw_node *node = (pw_node *)pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node, version, 0);
            pw_node_add_listener(node, &nodes[id].hook, &node_events, &nodes[id]);
        }
    }
}

void Graph::on_global_reg_remove_event(uint32_t id) {
    if(devices.erase(id)) {}
    else if(links.erase(id)) {}
    else if(nodes.erase(id)) {}
}

void Graph::close() {
    std::vector<const PwLink*> l_sides;
    const PwLink* r_side;
    for(const auto &link : links) {
        if(link.second.info->input_node_id == filter_id) l_sides.push_back(&link.second);
        else if(link.second.info->output_port_id == filter_id) r_side = &link.second;
    }

    for(const auto &link : l_sides) {
        pw_registry_destroy(registry, link->info->id);
        struct pw_properties *props0 = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(link->info->output_node_id).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "0",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(r_side->info->input_node_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "0",
            nullptr);

        struct pw_properties *props1 = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(link->info->output_node_id).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, "1",
            PW_KEY_LINK_INPUT_NODE,  std::to_string(r_side->info->input_node_id).c_str(),
            PW_KEY_LINK_INPUT_PORT,  "1",
            nullptr);

        pw_proxy* link0 = (pw_proxy*)pw_core_create_object(
            core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &props0->dict,
            0);

        pw_proxy* link1 = (pw_proxy*)pw_core_create_object(
            core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &props1->dict,
            0);
    }
}

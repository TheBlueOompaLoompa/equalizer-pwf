#include <charconv>
#include <cstdio>
#include <cstring>
#include <pipewire/keys.h>
#include "graph.h"
#include "pipewire/device.h"
#include "pipewire/link.h"
#include "pipewire/node.h"

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

void Graph::on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
    if(strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        /*devices.insert_or_assign(id, PwDevice{
            .id = id,
            .desc = spa_dict_lookup(props, PW_KEY_DEVICE_DESCRIPTION),
        });*/
    }else if(strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        /*uint32_t output_node_id, output_port_id,
                 input_node_id, input_port_id = 0;
        spa_dict_get_num(props, PW_KEY_LINK_OUTPUT_NODE, output_node_id);
        spa_dict_get_num(props, PW_KEY_LINK_OUTPUT_PORT, output_port_id);
        spa_dict_get_num(props, PW_KEY_LINK_INPUT_NODE, input_node_id);
        spa_dict_get_num(props, PW_KEY_LINK_INPUT_PORT, input_port_id);
        links.insert_or_assign(id, pw_link_info {
            .id = id,
            .output_node_id = output_node_id,
            .output_port_id = output_port_id,
            .input_node_id = input_node_id,
            .input_port_id = input_port_id,
        });
        
        printf("Node Id: %i\n", id);
        for(int i = 0; i < props->n_items; i++) {
            printf("    %s: %s\n", props->items[i].key, props->items[i].value);
        }*/
    }else if(strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        /*if(spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION), "Equalizer PWF") {
            this->link_replace_target = id;
        }*/
    }
}
void Graph::on_global_reg_remove_event(uint32_t id) {
    if(devices.erase(id)) {

    }else if(links.erase(id)) {
    }
}

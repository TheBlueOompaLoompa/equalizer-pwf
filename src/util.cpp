#include "util.h" 
#include <wp/core.h>
#include <wp/node.h>

std::vector<WpPort*> Util::get_node_ports(WpObjectManager *om, WpNode *node, WpDirection direction) {
    std::vector<WpPort*> ports;
    guint32 node_id = wp_proxy_get_bound_id (WP_PROXY (node));
 
    g_autoptr (WpIterator) it = wp_object_manager_new_filtered_iterator (
        om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.id", "=u", node_id,
        NULL);
 
    GValue val = G_VALUE_INIT;
    while (wp_iterator_next (it, &val)) {
        WpPort *port = static_cast<WpPort *> (g_value_get_object (&val));
        if (wp_port_get_direction (port) == direction)
            ports.push_back (port);
        g_value_unset (&val);
    }

    return ports;
}

void Util::get_linked_objects(WpObjectManager* om, WpLink* link, WpNode** input, WpNode** output) {
    guint32 output_node, output_port, input_node, input_port;
    wp_link_get_linked_object_ids(link, &output_node, &output_port, &input_node, &input_port);
    *input = WP_NODE(wp_object_manager_lookup(om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id",
        "=u", input_node,
        NULL));
    *output = WP_NODE(wp_object_manager_lookup(om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id",
        "=u", input_node,
        NULL));
}

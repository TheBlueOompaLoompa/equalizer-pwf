#include "util.h" 

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

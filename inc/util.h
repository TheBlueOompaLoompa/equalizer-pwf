#pragma once

#include <vector>
#include <wp/link.h>
#include <wp/object-manager.h>
#include <wp/node.h>
#include <wp/port.h>

#define GAIN(v) powf(10.0, v / 20.0)

namespace Util {
    std::vector<WpPort*> get_node_ports(WpObjectManager *om, WpNode *node, WpDirection direction);
    void get_linked_objects(WpObjectManager* om, WpLink* link, WpNode** input_node, WpNode** output_node);
};


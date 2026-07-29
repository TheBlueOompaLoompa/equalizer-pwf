#pragma once

#include <vector>
#include <wp/object-manager.h>
#include <wp/node.h>
#include <wp/port.h>

namespace Util {
    std::vector<WpPort*> get_node_ports(WpObjectManager *om, WpNode *node, WpDirection direction);
};


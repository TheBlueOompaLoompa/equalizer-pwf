#include <vector>
#include "pw_types.h"

class PwInfoMgr {
public:
    std::vector<PwDevice> device_list;
    std::vector<PwPort> port_list;
    std::vector<PwClient> client_list;
    std::vector<PwNode> node_list;
    std::vector<PwLink> link_list;

    PwInfoMgr();
    ~PwInfoMgr();

    void global_registry_event(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
};

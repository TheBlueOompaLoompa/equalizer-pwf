#pragma once
#include <cmath>
#include <mutex>
#include <vector>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/loop.h>

#include <filters.h>

#include "msg.h"
#include "channel.h"
#include "graph.h"

using Filter = signalsmith::filters::BiquadStatic<float, true>;
using FilterDesign = signalsmith::filters::BiquadDesign;

#define GAIN(v) powf(10.0, v / 20.0)

struct Port;

enum CommandType {
    PREAMP,
    PEAKING,
    LOW_SHELF,
    HIGH_SHELF,
    CHANNEL,
};

enum ShelfShaper {
    Q,
    FIXED_S,
    SLOPE,
};

struct AudioFilterConfig {
    float gain;

    float center_freq;
    bool use_bandwith;
    ShelfShaper shaper;
    float q;
    float bandwidth;

    std::vector<Filter>* filters = nullptr;

    inline void update_bandwidth() {
        bandwidth = log2f(q + sqrtf(powf(q, 2.0) - 1.0));
    }

    inline void update_q() {
        q = (powf(2.0, bandwidth) + powf(2.0, -bandwidth)) / 2.0;
    }
};

struct Command {
    CommandType type;
    union {
        AudioFilterConfig audio;
        int channel;
    };
};

class Equalizer {
public:
    struct pw_main_loop *main_loop = nullptr;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct pw_filter *filter = nullptr;
    std::vector<Port*> filter_inputs;
    std::vector<Port*> filter_outputs;
    Channel<Msg>* eq_channel;
    Channel<Msg>* ui_channel;

    std::vector<Command> commands;
    std::mutex commands_mutex;

    Graph graph;

    Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch);
    ~Equalizer();

    void loop();

    uint32_t filter_id;

    static void process(std::vector<Command> &commands, float* in, float* out, uint32_t n_samples, int channel);

    void on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    void on_global_reg_remove_event(uint32_t id);
private:
    struct spa_source* timer;
    static void on_link_info(void* data, uint32_t id);
    static void on_process(void* userdata, struct spa_io_position *position);
    static void on_timeout(void* data, uint64_t expirations);
    static void registry_event_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    static void registry_event_global_remove(void *data, uint32_t id);
};

struct Port {
    Equalizer* eq;
};


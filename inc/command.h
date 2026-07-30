#pragma once

#include <cmath>
#include <unordered_map>
#include <vector>
#include <filters.h>

using Filter = signalsmith::filters::BiquadStatic<float, true>;
using FilterDesign = signalsmith::filters::BiquadDesign;

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

    std::unordered_map<std::string, Filter*> filters;

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

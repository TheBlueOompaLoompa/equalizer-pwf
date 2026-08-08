#pragma once

#include "util.h"
#include <cmath>
#include <cstdint>
#include <iostream>
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
    COLOR,
    COMMENT,
};
#define IS_FILTER_COMMAND_TYPE(type) (type != CommandType::PREAMP && type != CommandType::CHANNEL && type != CommandType::COLOR && type != CommandType::COMMENT)

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

    std::unordered_map<uint32_t, std::unordered_map<std::string, Filter*>>* chain_filters;

    inline void update_bandwidth() {
        bandwidth = log2f(q + sqrtf(powf(q, 2.0) - 1.0));
    }

    inline void update_q() {
        q = (powf(2.0, bandwidth) + powf(2.0, -bandwidth)) / 2.0;
    }
};

class Command {
public:
    CommandType type;
    union {
        AudioFilterConfig audio;
        uint16_t channels;
    };

    ~Command() {
        if(type == CommandType::PREAMP || type == CommandType::CHANNEL) return;
        for(auto& chain_set : *audio.chain_filters) {
            for(auto& filter : chain_set.second) {
                if(filter.second != nullptr) {
                    delete filter.second;
                    filter.second = nullptr;
                }
            }
        }
    }

    bool is_filter() {
        return IS_FILTER_COMMAND_TYPE(type);
    }

    void update_filters() {
        if(!is_filter()) return;
        for(auto& filters : *audio.chain_filters) {
            for(auto& filter : filters.second) {
                update_filter(filter.second);
            }
        }
    }

    float responseDb(const uint32_t& device_id, const std::string& channel, float fq) {
        switch(type) {
        case CommandType::PREAMP:
            {
                float gain_linear = GAIN(audio.gain);
                return 20.0f * log10f(fmaxf(gain_linear, 1e-9f));
            }
            break;
        case CommandType::CHANNEL:
        case CommandType::COLOR:
        case CommandType::COMMENT:
            break;
        default:
            if(audio.chain_filters->find(device_id) != audio.chain_filters->end() && (*audio.chain_filters)[device_id].find(channel) != (*audio.chain_filters)[device_id].end())
                return (*audio.chain_filters)[device_id][channel]->responseDb((float)fq/44100.0);
            break;
        }

        return 0.0f;
    }

    bool has_channel(const std::string& channel) {
        if(channel == "FL") return (channels & (1 << 0)) > 0;
        else if(channel == "FR") return (channels & (1 << 1)) > 0;
        else if(channel == "C") return (channels & (1 << 2)) > 0;
        else if(channel == "LFE") return (channels & (1 << 3)) > 0;
        else if(channel == "RL") return (channels & (1 << 4)) > 0;
        else if(channel == "RR") return (channels & (1 << 5)) > 0;
        else if(channel == "RC") return (channels & (1 << 6)) > 0;
        else if(channel == "SL") return (channels & (1 << 7)) > 0;
        else if(channel == "SR") return (channels & (1 << 8)) > 0;
        return false;
    }

private:
    void update_filter(Filter* &filter_ref) {
        if(filter_ref == nullptr) {
            filter_ref = new Filter();
            //std::cout << "New filter" << std::endl;
        }
        switch(type) {
        case CommandType::PEAKING:
            update_peaking(filter_ref);
            break;
        case CommandType::LOW_SHELF:
        case CommandType::HIGH_SHELF:
            update_shelf(filter_ref);
            break;
        default:
            std::cerr << "Error: Unhandled filter in command.h:update_filter() " << type << std::endl;
            break;
        }
    }

    void update_peaking(Filter* filter) {
        filter->peakDbQ(audio.center_freq/44100.0, audio.gain, audio.q);
    }

    void update_shelf(Filter* filter) {
        if(type == CommandType::LOW_SHELF) {
            filter->lowShelfDbQ(audio.center_freq/44100.0, audio.gain, audio.q);
        }else {
            filter->highShelfDbQ(audio.center_freq/44100.0, audio.gain, audio.q);
        }
    }
};

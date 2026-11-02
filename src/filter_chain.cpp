#include <cstdint>
#include <iostream>
#include <pipewire/filter.h>
#include <pipewire/keys.h>
#include <pipewire/node.h>
#include <pipewire/properties.h>
#include <spa/param/latency-utils.h>
#include <spa/param/latency.h>
#include <spa/pod/builder.h>
#include <spa/utils/defs.h>
#include <string>
#include <vector>
#include <wp/iterator.h>
#include <wp/link.h>
#include <wp/node.h>
#include <wp/object-manager.h>
#include <wp/properties.h>
#include <wp/proxy-interfaces.h>
#include <wp/proxy.h>

#include "filter_chain.h"
#include "command.h"
#include "util.h"

namespace {
    // A helper to create links sequentially to avoid PipeWire port-assignment race conditions
    struct SequentialLinker {
        WpCore* core;
        uint32_t sink_id;
        uint32_t filter_id;
        uint32_t device_id;
        uint32_t total_links;
        uint32_t current_link;

        static void step(SequentialLinker* linker) {
            // Once all links are created, clean up and exit
            if (linker->current_link >= linker->total_links) {
                delete linker;
                return;
            }

            // 1. Create Sink -> Filter link
            WpProperties* p1 = wp_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, std::to_string(linker->sink_id).c_str(),
                PW_KEY_LINK_INPUT_NODE,  std::to_string(linker->filter_id).c_str(),
                NULL);
                
            WpLink* link1 = wp_link_new_from_factory(linker->core, "link-factory", p1);
            wp_object_activate(WP_OBJECT(link1), WP_PROXY_FEATURE_BOUND, NULL, 
                [](GObject *obj, GAsyncResult *res, gpointer data) {
                    SequentialLinker* l = static_cast<SequentialLinker*>(data);
                    wp_object_activate_finish(WP_OBJECT(obj), res, NULL);

                    // 2. Create Filter -> Device link
                    WpProperties* p2 = wp_properties_new(
                        PW_KEY_LINK_OUTPUT_NODE, std::to_string(l->filter_id).c_str(),
                        PW_KEY_LINK_INPUT_NODE, std::to_string(l->device_id).c_str(),
                        NULL);
                        
                    WpLink* link2 = wp_link_new_from_factory(l->core, "link-factory", p2);
                    wp_object_activate(WP_OBJECT(link2), WP_PROXY_FEATURE_BOUND, NULL,
                        [](GObject *o, GAsyncResult *r, gpointer d) {
                            SequentialLinker* ll = static_cast<SequentialLinker*>(d);
                            wp_object_activate_finish(WP_OBJECT(o), r, NULL);
                            
                            // 3. Move to the next channel and trigger the next step
                            ll->current_link++;
                            step(ll); 
                        }, l);
                }, linker);
        }
    };
}

/*void on_process(void* userdata, struct spa_io_position *position) {
    FilterChain* chain = static_cast<FilterChain*>(userdata);
    uint32_t n_samples = position->clock.duration;

    if(chain->commands->size() == 0) {
        for(auto& channel : chain->input_ports) {
            float *in, *out;
            in = static_cast<float*>(pw_filter_get_dsp_buffer(channel.second, n_samples));
            out = static_cast<float*>(pw_filter_get_dsp_buffer(chain->output_ports[channel.first], n_samples));
            if (in == nullptr || out == nullptr)
                continue;
            memcpy(out, in, n_samples*sizeof(float));
        }
    }

    for(auto& channel : chain->input_ports) {
        float *in, *out;
        in = static_cast<float*>(pw_filter_get_dsp_buffer(channel.second, n_samples));
        out = static_cast<float*>(pw_filter_get_dsp_buffer(chain->output_ports[channel.first], n_samples));
        if (in == nullptr || out == nullptr)
            continue;
        chain->processing_channels = 0x1ff;
        for(auto& command : *chain->commands) {
            chain->process(command, channel.first, in, out, n_samples);
        }
    }
}*/

void on_process(void* userdata, struct spa_io_position *position) {
    FilterChain* chain = static_cast<FilterChain*>(userdata);
    uint32_t n_samples = position->clock.duration;

    // Channel order matching bitmask positions (1 << 0 to 1 << 8)
    static const std::string CHANNELS[9] = { 
        "FL", "FR", "C", "LFE", "RL", "RR", "RC", "SL", "SR" 
    };

    // 1. Gather all input and output DSP buffers upfront
    std::unordered_map<std::string, float*> in_buffers;
    std::unordered_map<std::string, float*> out_buffers;

    for(auto& port_pair : chain->input_ports) {
        const std::string& ch_name = port_pair.first;
        float* in = static_cast<float*>(pw_filter_get_dsp_buffer(port_pair.second, n_samples));
        float* out = static_cast<float*>(pw_filter_get_dsp_buffer(chain->output_ports[ch_name], n_samples));

        if (in != nullptr && out != nullptr) {
            in_buffers[ch_name] = in;
            out_buffers[ch_name] = out;

            // Initial passthrough so untouched channels pass audio unchanged
            memcpy(out, in, n_samples * sizeof(float));
        }
    }

    // Passthrough if there are no commands to execute
    if(!chain->commands || chain->commands->empty()) {
        return;
    }

    // 2. Initialize processing channels mask for the start of the command chain
    chain->processing_channels = 0x1ff; // Default: all channels active

    // 3. Process commands sequentially (Outer Command Loop)
    for(auto& command : *chain->commands) {

        // A. Dynamic Channel Selection (State Update)
        if(command.type == CommandType::CHANNEL) {
            chain->processing_channels = command.channels;
            continue;
        }

        // B. Ignore non-audio metadata
        if(command.type == CommandType::COMMENT || command.type == CommandType::COLOR) {
            continue;
        }

        // C. Cross-Channel Copying & Multi-Source Downmixing
        if(command.type == CommandType::COPY) {
            uint16_t dst_mask = command.copy.to_channel;
            uint16_t src_mask = command.copy.from_channels;

            float* dst_out = nullptr;
            std::string dst_name;

            // Locate destination output buffer
            for(int i = 0; i < 9; ++i) {
                if(dst_mask & (1 << i)) {
                    dst_name = CHANNELS[i];
                    auto it = out_buffers.find(dst_name);
                    if(it != out_buffers.end()) {
                        dst_out = it->second;
                    }
                    break;
                }
            }

            if(dst_out) {
                bool first_src = true;

                // Copy or accumulate from all specified source channels
                for(int i = 0; i < 9; ++i) {
                    if(src_mask & (1 << i)) {
                        auto it = out_buffers.find(CHANNELS[i]);
                        if(it == out_buffers.end()) continue;

                        float* src_out = it->second;

                        if(first_src) {
                            // First source channel overwrites the destination buffer
                            for (uint32_t s = 0; s < n_samples; ++s) {
                                dst_out[s] = src_out[s];
                            }
                            first_src = false;
                        }else {
                            // Subsequent source channels mix into the destination buffer
                            for(uint32_t s = 0; s < n_samples; ++s) {
                                dst_out[s] += src_out[s];
                            }
                        }
                    }
                }

                // Synchronize destination input buffer so subsequent DSP filters receive the copied audio
                if (!first_src && in_buffers.find(dst_name) != in_buffers.end()) {
                    memcpy(in_buffers[dst_name], dst_out, n_samples * sizeof(float));
                }
            }
            continue;
        }

        // D. Per-Channel DSP Audio Processing
        for (int i = 0; i < 9; ++i) {
            // Skip channels disabled by the current CHANNEL mask state
            if ((chain->processing_channels & (1 << i)) == 0) {
                continue;
            }

            const std::string& ch_name = CHANNELS[i];
            auto in_it = in_buffers.find(ch_name);
            auto out_it = out_buffers.find(ch_name);

            if (in_it != in_buffers.end() && out_it != out_buffers.end()) {
                chain->process(command, ch_name, in_it->second, out_it->second, n_samples);
            }
        }
    }
}

static const struct pw_filter_events filter_events = {
    .version = PW_VERSION_FILTER_EVENTS,
    .process = on_process,
};

FilterChain::FilterChain(pw_core *pw_core, WpCore* wp_core, pw_registry* registry, WpObjectManager* om, gpointer object, uint32_t expected_ports_n, std::vector<Command>* commands):
om(om), core(wp_core), expected_ports_n(expected_ports_n), registry(registry), commands(commands) {
    WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));
    const gchar* audio_positions = wp_properties_get(props, "audio.position");

    device_node_id = wp_proxy_get_bound_id(WP_PROXY(object));

    const struct spa_pod *params[2];
    uint32_t n_params = 0;
    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    WpProperties *sink_props = wp_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_NODE_NAME, "equalizer-pwf-sink",
        PW_KEY_NODE_DESCRIPTION, "Equalizer PWF Sink",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_PASSIVE, "out",
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        "factory.name", "support.null-audio-sink",
        "audio.position", audio_positions,
        "target-device-id", std::to_string(device_node_id).c_str(),
        NULL);
    
    sink_node = wp_node_new_from_factory(core, "adapter", sink_props);

    filter = pw_filter_new(
        pw_core,
        "Equalizer PWF",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_NODE_NAME, "equalizer-pwf-filter",
            PW_KEY_NODE_DESCRIPTION, "Equalizer PWF",
            "target-device-id", std::to_string(device_node_id).c_str(),
            NULL));

    spa_zero(filter_listener);
    pw_filter_add_listener(filter, &filter_listener, &filter_events, this);

    static struct spa_process_latency_info latency_info =
        SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC);

    params[n_params++] = spa_process_latency_build(
        &builder,
        SPA_PARAM_ProcessLatency,
        &latency_info);

    if (pw_filter_connect(filter, PW_FILTER_FLAG_RT_PROCESS, params, n_params) < 0) {
        std::cerr << "Can't connect filter for device id: " << device_node_id << std::endl;
        return;
    }

    wp_object_activate(WP_OBJECT(sink_node), WP_PROXY_FEATURE_BOUND, NULL,
        (GAsyncReadyCallback)[](GObject *obj, GAsyncResult *res, gpointer data) {
            g_autoptr(GError) error = NULL;
            if (!wp_object_activate_finish(WP_OBJECT(obj), res, &error)) {
                std::cerr << "Sink activation failed: " << error->message << std::endl;
                return;
            }
            FilterChain* chain = static_cast<FilterChain*>(data);
            chain->sink_bound = true;
            chain->sink_id = wp_proxy_get_bound_id(WP_PROXY(obj));
        }, this);

    update_ports();
}

FilterChain::~FilterChain() {
    pw_proxy_destroy((struct pw_proxy*)sink_node);
    pw_filter_destroy(filter);
}

void FilterChain::process(Command& command, const std::string& channel, float* in, float* out, uint32_t n_samples) {
    memcpy(out, in, n_samples*sizeof(float));

    switch (command.type) {
    case CommandType::CHANNEL:
        processing_channels = command.channels;
        return;
    case CommandType::COMMENT:
    case CommandType::COLOR:
    case CommandType::COPY:
        return;
    default:
        break;
    }

    if((processing_channels & (0b1)) == 0 && channel == "FL") return;
    if((processing_channels & (0b1 << 1)) == 0 && channel == "FR") return;
    if((processing_channels & (0b1 << 2)) == 0 && channel == "C") return;
    if((processing_channels & (0b1 << 3)) == 0 && channel == "LFE") return;
    if((processing_channels & (0b1 << 4)) == 0 && channel == "RL") return;
    if((processing_channels & (0b1 << 5)) == 0 && channel == "RR") return;
    if((processing_channels & (0b1 << 6)) == 0 && channel == "RC") return;
    if((processing_channels & (0b1 << 7)) == 0 && channel == "SL") return;
    if((processing_channels & (0b1 << 8)) == 0 && channel == "SR") return;

    switch(command.type) {
    case CommandType::PREAMP:
        {
            float gain = GAIN(command.audio.gain);
            for(int i = 0; i < n_samples; i++) {
                out[i] = in[i] * gain;
            }
            // Update 'in' so the next command in the chain receives this output
            memcpy(in, out, n_samples*sizeof(float));
        }
        break;
    default:
        if((*command.audio.chain_filters)[device_node_id][channel] != nullptr) {
            for(int i = 0; i < n_samples; i++) {
                out[i] = (*(*command.audio.chain_filters)[device_node_id][channel])(in[i]);
            }
        }
        // Update 'in' so the next command in the chain receives this output
        memcpy(in, out, n_samples*sizeof(float));
        break;
    }
}

void FilterChain::update_filters() {
    for(auto& command : *commands) {
        if(command.is_filter()) {
            if(command.audio.chain_filters == nullptr)
                command.audio.chain_filters = new std::unordered_map<uint32_t, std::unordered_map<std::string, Filter*>>();
            std::unordered_map<std::string, Filter*>* filters = &(*command.audio.chain_filters)[device_node_id];
            for(const auto& channel : input_ports) {
                if(filters->find(channel.first) == filters->end())
                    filters->insert_or_assign(channel.first, new Filter());
            }
            command.update_filters();
        }
    }
}

void FilterChain::update_ports() {
    gpointer object = wp_object_manager_lookup(om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id",
        "=u", device_node_id,
        NULL);

    if(object == NULL) {
        std::cerr << "Couldn't find node " << device_node_id << std::endl;
        return;
    }

    WpIterator* it = wp_node_new_ports_filtered_iterator(WP_NODE(object),
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction",
        "=s", "in",
        NULL);


    GValue item = G_VALUE_INIT;
    while(wp_iterator_next(it, &item)) {
        gpointer object = g_value_get_object(&item);
        if(!object || !WP_IS_PORT(object)) { g_value_unset(&item); continue; }
        if(!wp_object_test_active_features(WP_OBJECT(object), WP_PIPEWIRE_OBJECT_FEATURE_INFO))
            { g_value_unset(&item); continue; }
        WpPort* port = WP_PORT(object);
        WpProperties *props = wp_pipewire_object_get_properties(WP_PIPEWIRE_OBJECT(object));

        const gchar* channel = wp_properties_get(props, PW_KEY_AUDIO_CHANNEL);

        if(input_ports.find(std::string(channel)) == input_ports.end()) {
            input_ports.insert_or_assign(std::string(channel), (Port*)pw_filter_add_port(
                filter,
                PW_DIRECTION_INPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(struct Port*),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                    PW_KEY_PORT_GROUP, "stream.0",
                    PW_KEY_AUDIO_CHANNEL, channel,
                    nullptr),
                nullptr,
                0));
            
            output_ports.insert_or_assign(std::string(channel), (Port*)pw_filter_add_port(
                filter,
                PW_DIRECTION_OUTPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(struct Port*),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                    PW_KEY_PORT_GROUP, "stream.0",
                    PW_KEY_AUDIO_CHANNEL, channel,
                    nullptr),
                nullptr,
                0));
        }

        maybe_create_links();

        g_value_unset(&item);
    }

    wp_iterator_unref(it);
}

static uint32_t get_port_id_by_channel(WpObjectManager* om, uint32_t node_id, const std::string& direction, const std::string& channel) {
    WpIterator* it = wp_object_manager_new_filtered_iterator(om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_ID, "=s", std::to_string(node_id).c_str(),
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_PORT_DIRECTION, "=s", direction.c_str(),
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_AUDIO_CHANNEL, "=s", channel.c_str(),
        NULL);

    if (!it) return 0;

    GValue item = G_VALUE_INIT;
    uint32_t port_id = 0;
    if (wp_iterator_next(it, &item)) {
        WpPort* port = WP_PORT(g_value_get_object(&item));
        port_id = wp_proxy_get_bound_id(WP_PROXY(port));
        g_value_unset(&item);
    }
    wp_iterator_unref(it);
    return port_id;
}

void FilterChain::maybe_create_links() {
    if(!sink_bound) return;
    if(sink_ports_bound < expected_ports_n*2) return;
    if(filter_ports_bound < expected_ports_n*2) return;
    if(device_ports_bound < expected_ports_n) return;

    WpIterator *it = wp_object_manager_new_filtered_iterator(om, WP_TYPE_LINK,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_LINK_INPUT_NODE, "=s", std::to_string(device_node_id).c_str(),
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_LINK_OUTPUT_NODE, "!s", std::to_string(pw_filter_get_node_id(filter)).c_str(),
        NULL);

    if(it) {
        bool requires_sync = false;
        GValue item = G_VALUE_INIT;
        
        while(wp_iterator_next(it, &item)) {
            gpointer object = g_value_get_object(&item);
            WpProxy* proxy = WP_PROXY(object);
            if(proxy) {
                uint32_t id = wp_proxy_get_bound_id(proxy);
                stream_nodes.insert_or_assign(id, false);
                wp_global_proxy_request_destroy(WP_GLOBAL_PROXY(proxy));
                requires_sync = true;
            }
            g_value_unset(&item);
        }
        wp_iterator_unref(it);

        if (requires_sync) {
            // Pass the chain pointer so we can access om and input_ports
            struct SyncData {
                FilterChain* chain;
                WpCore* core;
                uint32_t sink_id;
                uint32_t filter_id;
                uint32_t device_id;
            };
            
            SyncData* data = new SyncData{
                this, core, sink_id, pw_filter_get_node_id(filter), device_node_id
            };

            wp_core_sync(core, NULL, [](GObject *obj, GAsyncResult *res, gpointer user_data) {
                SyncData* d = static_cast<SyncData*>(user_data);
                g_autoptr(GError) error = NULL;
                
                if (!wp_core_sync_finish(d->core, res, &error)) {
                    std::cerr << "Core sync failed: " << error->message << std::endl;
                    delete d;
                    return;
                }

                // Explicitly link Port ID to Port ID for each channel
                for (const auto& kv : d->chain->input_ports) {
                    const std::string& channel = kv.first;
                    
                    uint32_t sink_out  = get_port_id_by_channel(d->chain->om, d->sink_id,   "out", channel);
                    uint32_t filter_in = get_port_id_by_channel(d->chain->om, d->filter_id, "in",  channel);
                    uint32_t filter_out= get_port_id_by_channel(d->chain->om, d->filter_id, "out", channel);
                    uint32_t device_in = get_port_id_by_channel(d->chain->om, d->device_id, "in",  channel);

                    if (sink_out > 0 && filter_in > 0) {
                        WpProperties* p1 = wp_properties_new(
                            PW_KEY_LINK_OUTPUT_PORT, std::to_string(sink_out).c_str(),
                            PW_KEY_LINK_INPUT_PORT,  std::to_string(filter_in).c_str(),
                            NULL);
                        WpLink* link1 = wp_link_new_from_factory(d->core, "link-factory", p1);
                        wp_object_activate(WP_OBJECT(link1), WP_PROXY_FEATURE_BOUND, NULL, 
                            [](GObject *o, GAsyncResult *r, gpointer) { wp_object_activate_finish(WP_OBJECT(o), r, NULL); }, nullptr);
                    } else {
                        std::cerr << "Missing Sink Out / Filter In port for channel: " << channel << std::endl;
                    }

                    if (filter_out > 0 && device_in > 0) {
                        WpProperties* p2 = wp_properties_new(
                            PW_KEY_LINK_OUTPUT_PORT, std::to_string(filter_out).c_str(),
                            PW_KEY_LINK_INPUT_PORT,  std::to_string(device_in).c_str(),
                            NULL);
                        WpLink* link2 = wp_link_new_from_factory(d->core, "link-factory", p2);
                        wp_object_activate(WP_OBJECT(link2), WP_PROXY_FEATURE_BOUND, NULL, 
                            [](GObject *o, GAsyncResult *r, gpointer) { wp_object_activate_finish(WP_OBJECT(o), r, NULL); }, nullptr);
                    } else {
                        std::cerr << "Missing Filter Out / Device In port for channel: " << channel << std::endl;
                    }
                }

                delete d;
            }, data);
        }
    }
}

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

void on_process(void* userdata, struct spa_io_position *position) {
    FilterChain* chain = static_cast<FilterChain*>(userdata);
    uint32_t n_samples = position->clock.duration;
    chain->processing_channels = 0x1ff;

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
        for(auto& command : *chain->commands) {
            chain->process(command, channel.first, in, out, n_samples);
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
    if((processing_channels & (0b1 << 0)) == 0 && channel == "FL") return;
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
            memcpy(in, out, n_samples*sizeof(float));
        }
        break;
    case CommandType::CHANNEL:
        processing_channels = command.channels;
        break;
    default:
        if((*command.audio.chain_filters)[device_node_id][channel] != nullptr) {
            for(int i = 0; i < n_samples; i++) {
                out[i] = (*(*command.audio.chain_filters)[device_node_id][channel])(in[i]);
            }
        }
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

void FilterChain::maybe_create_links() {
    if(!sink_bound) {
#ifdef DEBUG
        std::cout << "sink not bound yet, skipping link creation" << std::endl;
#endif
        return;
    }
    if(sink_ports_bound < expected_ports_n*2) {
#ifdef DEBUG
        std::cout << "not enough sink ports yet, skipping link creation" << std::endl;
#endif
        return;
    }
    if(filter_ports_bound < expected_ports_n*2) {
#ifdef DEBUG
        std::cout << "not enough filter ports yet, skipping link creation" << std::endl;
#endif
        return;
    }
    if(device_ports_bound < expected_ports_n) {
#ifdef DEBUG
        std::cout << "not enough device ports yet, skipping link creation" << std::endl;
#endif
        return;
    }

    WpIterator *it = wp_object_manager_new_filtered_iterator(om, WP_TYPE_LINK,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_LINK_INPUT_NODE,
        "=s", std::to_string(device_node_id).c_str(),
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_LINK_OUTPUT_NODE,
        "!s", std::to_string(filter_id).c_str(),
        NULL);
    if(it) {
        GValue item = G_VALUE_INIT;
        while(wp_iterator_next(it, &item)) {
            gpointer object = g_value_get_object(&item);
            uint32_t id = wp_proxy_get_bound_id(WP_PROXY(object));
            WpProxy* proxy = WP_PROXY(object);
            if(proxy) {
                stream_nodes.insert_or_assign(id, false);
                pw_registry_destroy(registry, id);
            }
            g_value_unset(&item);
        }
        wp_iterator_unref(it);
    }

    WpProperties* inter_link_props = wp_properties_new(
        PW_KEY_LINK_OUTPUT_NODE, std::to_string(sink_id).c_str(),
        PW_KEY_LINK_INPUT_NODE,  std::to_string(pw_filter_get_node_id(filter)).c_str(),
        NULL);

    WpLink* link = wp_link_new_from_factory(core, "link-factory", inter_link_props);
    wp_object_activate(WP_OBJECT(link), WP_PROXY_FEATURE_BOUND, NULL,
        (GAsyncReadyCallback)[](GObject *obj, GAsyncResult *res, gpointer data) {
            g_autoptr(GError) error = NULL;
            if (!wp_object_activate_finish(WP_OBJECT(obj), res, &error)) {
                std::cerr << "Link activation failed: " << error->message << std::endl;
            }
        }, nullptr);

    WpProperties* link_props = wp_properties_new(
        PW_KEY_LINK_OUTPUT_NODE, std::to_string(pw_filter_get_node_id(filter)).c_str(),
        PW_KEY_LINK_INPUT_NODE, std::to_string(device_node_id).c_str(),
        NULL);
    link = wp_link_new_from_factory(core, "link-factory", link_props);
    wp_object_activate(WP_OBJECT(link), WP_PROXY_FEATURE_BOUND, NULL,
        (GAsyncReadyCallback)[](GObject *obj, GAsyncResult *res, gpointer data) {
            g_autoptr(GError) error = NULL;
            if (!wp_object_activate_finish(WP_OBJECT(obj), res, &error)) {
                std::cerr << "Link activation failed: " << error->message << std::endl;
            }
        }, nullptr);
}

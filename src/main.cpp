#include "app.h"

int main(int argc, char** argv) {
    App app = App();
    return app.loop();
}

/*
#include <bits/stdc++.h>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/defs.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/core.h>
#include <pipewire/keys.h>
struct Data;

struct Port {
    Data* data{};
};

struct Data {
    pw_main_loop* loop{};
    pw_filter* filter{};
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    std::vector<Port*> inputs;
    std::vector<Port*> outputs;
    PwInfoMgr info_mgr;

    spa_audio_info_raw format{};
};

struct Biquad {
    float b0{}, b1{}, b2{};
    float a1{}, a2{};

    float x1{}, x2{};
    float y1{}, y2{};

    float sample_rate{44100.0f};
};

static Biquad peak{};

static void biquad_set_peak(Biquad& b, float freq, float Q, float gain_db) {
    float A = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / b.sample_rate;
    float alpha = std::sin(w0) / (2.0f * Q);

    float c = std::cos(w0);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * c;
    float b2 = 1.0f - alpha * A;

    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * c;
    float a2 = 1.0f - alpha / A;

    b.b0 = b0 / a0;
    b.b1 = b1 / a0;
    b.b2 = b2 / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
}

static void on_process(void* userdata, spa_io_position* position) {
    auto* data = static_cast<Data*>(userdata);

    uint32_t n_samples = position->clock.duration;

    for(int port_i = 0; port_i < data->inputs.size(); port_i++) {
        float* in = (float*)pw_filter_get_dsp_buffer(data->inputs[port_i], n_samples);
        float* out = (float*)pw_filter_get_dsp_buffer(data->outputs[port_i], n_samples);

        if (!in || !out)
            return;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float x = in[i];

            float y =
                peak.b0 * x +
                peak.b1 * peak.x1 +
                peak.b2 * peak.x2 -
                peak.a1 * peak.y1 -
                peak.a2 * peak.y2;

            peak.x2 = peak.x1;
            peak.x1 = x;

            peak.y2 = peak.y1;
            peak.y1 = y;

            out[i] = y;
        }
    }
}

static void registry_event_global(void *data, uint32_t id,
    uint32_t permissions, const char *type, uint32_t version,
    const struct spa_dict *props) {
    if(id == SPA_ID_INVALID) return;


    //PwInfoMgr* mgr = static_cast<PwInfoMgr*>(data);
    //mgr->global_registry_event(mgr, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props)
    printf("%s\n", type);
    if(!strcmp(type, "PipeWire:Interface:Device")) {
        bool found = false;
        for(int i = 0; i < props->n_items; i++) {
            if(!strcmp(props->items[i].key, "media.class") && !strcmp(props->items[i].value, "Audio/Device")) {
                found = true;
                break;
            }
        }
        if(!found) return;
        //audio_sink_info_mutex.lock();
        printf("object: id:%u type:%s/%d\n", id, type, version);
        for(int i = 0; i < props->n_items; i++) {
            //if(!strcmp(props->items[i].key, "device.description"))
                //audio_sink_info.push_back(AudioSinkInfo(id, props->items[i].value));
        }
        //audio_sink_info_mutex.unlock();
    }
}

static void registry_event_global_remove(void *data, uint32_t id) {
}

static const pw_filter_events filter_events {
    .version = PW_VERSION_FILTER_EVENTS,
    .process = on_process
};

static const pw_registry_events registry_events {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static void do_quit(void* userdata, int) {
    auto* data = static_cast<Data*>(userdata);
    pw_main_loop_quit(data->loop);
}

int dsp_process() {
    Data data{};

    const spa_pod* params[1];
    uint32_t n_params = 0;

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(0, nullptr);

    data.loop = pw_main_loop_new(nullptr);
    data.context = pw_context_new(pw_main_loop_get_loop(data.loop), NULL, 0);
    data.core = pw_context_connect(data.context, NULL, 0);
    data.registry = pw_core_get_registry(data.core, PW_VERSION_REGISTRY, 0);

    struct spa_hook registry_listener;

    spa_zero(registry_listener);
    pw_registry_add_listener(data.registry, &registry_listener, &registry_events, NULL);

    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

    data.filter = pw_filter_new_simple(
        pw_main_loop_get_loop(data.loop),
        "Equalizer PWF",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_AUDIO_CHANNELS, "2",
            "audio.position", "[FL,FR]",
            nullptr),
        &filter_events,
        &data);

    data.inputs.push_back((Port*)pw_filter_add_port(
        data.filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_AUDIO_CHANNEL, "FL",
            PW_KEY_PORT_NAME, "playback_FL",
            nullptr),
        nullptr,
        0));

    data.inputs.push_back((Port*)pw_filter_add_port(
        data.filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_AUDIO_CHANNEL, "FR",
            PW_KEY_PORT_NAME, "playback_FR",
            nullptr),
        nullptr,
        0));

    data.outputs.push_back((Port*)pw_filter_add_port(
        data.filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_AUDIO_CHANNEL, "FL",
            PW_KEY_PORT_NAME, "output_FL",
            nullptr),
        nullptr,
        0));

    data.outputs.push_back((Port*)pw_filter_add_port(
        data.filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_AUDIO_CHANNEL, "FR",
            PW_KEY_PORT_NAME, "output_FR",
            nullptr),
        nullptr,
        0));

    static struct spa_process_latency_info latency_info =
        SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC);

    params[n_params++] = spa_process_latency_build(
        &builder,
        SPA_PARAM_ProcessLatency,
        &latency_info);

    if (pw_filter_connect(
            data.filter,
            PW_FILTER_FLAG_RT_PROCESS,
            params,
            n_params) < 0) {
        std::fprintf(stderr, "can't connect\n");
        return -1;
    }

    peak.sample_rate = 44100.0f;
    biquad_set_peak(peak, 300.0f, 1.0f, -3.0f);

    pw_main_loop_run(data.loop);

    pw_proxy_destroy((struct pw_proxy*)data.registry);
    pw_core_disconnect(data.core);
    pw_context_destroy(data.context);
    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();

    return 0;
}
*/

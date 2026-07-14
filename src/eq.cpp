#include "eq.h"
#include "pipewire/core.h"
#include "pipewire/keys.h"
#include <cmath>

Equalizer::Equalizer(Channel<Msg>* eq_ch, Channel<Msg>* ui_ch):
eq_channel(eq_ch), ui_channel(ui_ch), graph() {
    
}

Equalizer::~Equalizer() {
    
}

static bool channel = false;

void Equalizer::process(std::vector<FilterCommand> &commands, float* in, float* out, uint32_t n_samples) {
    memcpy(out, in, n_samples*sizeof(float));
    for(auto &command : commands) {
        switch(command.type) {
        case FilterCommandType::PREAMP:
            {
                float gain = GAIN(command.audio.gain);
                for(int i = 0; i < n_samples; i++) {
                    out[i] = in[i] * gain;
                }
                memcpy(in, out, n_samples*sizeof(float));
            }
            break;
        default:
            for(int i = 0; i < n_samples; i++) {
                if(channel) out[i] = command.audio.filter_l(in[i]);
                else out[i] = command.audio.filter_r(in[i]);
            }
            memcpy(in, out, n_samples*sizeof(float));
            break;
        }
    }
}

void Equalizer::on_process(void* userdata, struct spa_io_position *position) {
    Equalizer* eq = static_cast<Equalizer*>(userdata);
    uint32_t n_samples = position->clock.duration;
    eq->commands_mutex.lock();
    for(int i = 0; i < eq->filter_inputs.size(); i++) {
        float *in, *out;
        in = static_cast<float*>(pw_filter_get_dsp_buffer(eq->filter_inputs[i], n_samples));
        out = static_cast<float*>(pw_filter_get_dsp_buffer(eq->filter_outputs[i], n_samples));
        if (in == nullptr || out == nullptr)
            continue;
        process(eq->commands, in, out, n_samples);
        channel = !channel;
    }
    eq->commands_mutex.unlock();
}

void Equalizer::loop() {
    const struct spa_pod *params[1];
    uint32_t n_params = 0;
    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
 

    pw_init(nullptr, nullptr);

    main_loop = pw_main_loop_new(NULL);

    static const pw_registry_events registry_events {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = registry_event_global,
        .global_remove = registry_event_global_remove,
    };

    context = pw_context_new(pw_main_loop_get_loop(main_loop), NULL, 0);
    core = pw_context_connect(context, NULL, 0);
    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);

    struct spa_hook registry_listener;

    spa_zero(registry_listener);
    pw_registry_add_listener(registry, &registry_listener, &registry_events, this);

    static const struct pw_filter_events filter_events = {
        .version = PW_VERSION_FILTER_EVENTS,
        .process = on_process,
    };

    filter = pw_filter_new_simple(
        pw_main_loop_get_loop(main_loop),
        "Equalizer PWF",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_NODE_NAME, "Equalizer PWF",
            PW_KEY_NODE_DESCRIPTION, "Equalizer PWF",
            PW_KEY_NODE_AUTOCONNECT, "true",
            PW_KEY_OBJECT_LINGER, "true",
            "filter.smart", "true",
            NULL),
        &filter_events,
        this);
    
    filter_inputs.push_back((Port*)pw_filter_add_port(
        filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(struct Port*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "input_FL",
            PW_KEY_PORT_GROUP, "stream.0",
            PW_KEY_AUDIO_CHANNEL, "FL",
            nullptr),
        nullptr,
        0));

    filter_inputs.push_back((Port*)pw_filter_add_port(
        filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(struct Port*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "input_FR",
            PW_KEY_PORT_GROUP, "stream.0",
            PW_KEY_AUDIO_CHANNEL, "FR",
            nullptr),
        nullptr,
        0));

    filter_outputs.push_back((Port*)pw_filter_add_port(
        filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(struct Port*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "output_FL",
            PW_KEY_PORT_GROUP, "stream.0",
            PW_KEY_AUDIO_CHANNEL, "FL",
            nullptr),
        nullptr,
        0));

    filter_outputs.push_back((Port*)pw_filter_add_port(
        filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(struct Port*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "output_FR",
            PW_KEY_PORT_GROUP, "stream.0",
            PW_KEY_AUDIO_CHANNEL, "FR",
            nullptr),
        nullptr,
        0));

    static struct spa_process_latency_info latency_info =
        SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC);

    params[n_params++] = spa_process_latency_build(
        &builder,
        SPA_PARAM_ProcessLatency,
        &latency_info);

    if (pw_filter_connect(filter,
                            PW_FILTER_FLAG_RT_PROCESS,
                            params, n_params) < 0) {
        fprintf(stderr, "can't connect\n");
        return;
    }

    static struct timespec timeout, interval;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 1;
    interval.tv_sec = 0;
    interval.tv_nsec = 20 * SPA_NSEC_PER_MSEC;

    timer = pw_loop_add_timer(pw_main_loop_get_loop(main_loop), &on_timeout, this);
    pw_loop_update_timer(pw_main_loop_get_loop(main_loop), timer, &timeout, &interval, false);

    /* and wait while we let things run */
    pw_main_loop_run(main_loop);
 
    pw_proxy_destroy((struct pw_proxy*)registry);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_filter_destroy(filter);
    pw_main_loop_destroy(main_loop);
    pw_deinit();
}

void Equalizer::on_global_reg_event(uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
    graph.on_global_reg_event(id, permissions, type, version, props);
    ui_channel->send({
        .type = MsgType::DEVICE_LIST,
        .data = &graph.devices
    });
}

void Equalizer::on_global_reg_remove_event(uint32_t id) {
    graph.on_global_reg_remove_event(id);
    ui_channel->send({
        .type = MsgType::DEVICE_LIST,
        .data = &graph.devices
    });
}

void Equalizer::on_timeout(void* data, uint64_t expirations) {
    Equalizer* eq = static_cast<Equalizer*>(data);
    Msg* msg = eq->eq_channel->receive();
    if(msg != nullptr) {
        switch(msg->type) {
        case MsgType::QUIT:
            pw_main_loop_quit(eq->main_loop);
            break;
        case MsgType::DEVICE_LIST:
            eq->ui_channel->send({
                .type = MsgType::DEVICE_LIST,
                .data = &eq->graph.devices
            });
            break;
        default:
            printf("Msg type %i Eq unimplemented\n", msg->type);
            break;
        }
    }
}

void Equalizer::registry_event_global(void *data, uint32_t id,
    uint32_t permissions, const char *type, uint32_t version,
    const struct spa_dict *props) {
    if(id == SPA_ID_INVALID) return;
    static_cast<Equalizer*>(data)->on_global_reg_event(id, permissions, type, version, props);


    //PwInfoMgr* mgr = static_cast<PwInfoMgr*>(data);
    //mgr->global_registry_event(mgr, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props)
    //printf("%s\n", type);
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
        //printf("object: id:%u type:%s/%d\n", id, type, version);
        for(int i = 0; i < props->n_items; i++) {
            //if(!strcmp(props->items[i].key, "device.description"))
                //audio_sink_info.push_back(AudioSinkInfo(id, props->items[i].value));
        }
        //audio_sink_info_mutex.unlock();
    }
}

void Equalizer::registry_event_global_remove(void *data, uint32_t id) {
    static_cast<Equalizer*>(data)->on_global_reg_remove_event(id);
}

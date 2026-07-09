#include <cstdio>
#include <cmath>
#include <cstdint>
#include <csignal>
#include <vector>
#include <bits/stdc++.h>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <spa/param/audio/format-utils.h>

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "pipewire/core.h"
#include "pipewire/keys.h"
#include <SDL3/SDL.h>

#include "audio_sink_info.h"

int dsp_process();

std::mutex audio_sink_info_mutex;
std::vector<AudioSinkInfo> audio_sink_info;

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }

    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("Equalizer PWF", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Create GPU Device
    SDL_GPUDevice* gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr)
    {
        printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window))
    {
        printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;                      // Only used in multi-viewports mode.
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;  // Only used in multi-viewports mode.
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    std::thread dsp_thread(dsp_process);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        static std::string selected_nick = "";

        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
            if(ImGui::BeginCombo("###combo", selected_nick.c_str())) {
                if(audio_sink_info_mutex.try_lock()) {
                    for(int i = 0; i < audio_sink_info.size(); i++) {
                        if(ImGui::Selectable(audio_sink_info[i].nick.c_str(), false)) {
                            selected_nick = audio_sink_info[i].nick;
                        }
                    }
                    audio_sink_info_mutex.unlock();
                }
                ImGui::EndCombo();
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

        SDL_GPUTexture* swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture

        if (swapchain_texture != nullptr && !is_minimized)
        {
            // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

            // Setup and start a render pass
            SDL_GPUColorTargetInfo target_info = {};
            target_info.texture = swapchain_texture;
            target_info.clear_color = SDL_FColor { clear_color.x, clear_color.y, clear_color.z, clear_color.w };
            target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            target_info.store_op = SDL_GPU_STOREOP_STORE;
            target_info.mip_level = 0;
            target_info.layer_or_depth_plane = 0;
            target_info.cycle = false;
            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);

            // Render ImGui
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

            SDL_EndGPURenderPass(render_pass);
        }

        // Submit the command buffer
        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();


    return 0;
}

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
    if(!strcmp(type, "PipeWire:Interface:Node")) {
        bool found = false;
        for(int i = 0; i < props->n_items; i++) {
            if(!strcmp(props->items[i].key, "media.class") && !strcmp(props->items[i].value, "Audio/Sink")) {
                found = true;
                break;
            }
        }
        if(!found) return;
        audio_sink_info_mutex.lock();
        printf("object: id:%u type:%s/%d\n", id, type, version);
        for(int i = 0; i < props->n_items; i++) {
            if(!strcmp(props->items[i].key, "node.nick"))
                audio_sink_info.push_back(AudioSinkInfo(id, props->items[i].value));
        }
        audio_sink_info_mutex.unlock();
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

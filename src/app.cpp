#include "app.h"
#include "SDL3/SDL_error.h"
#include "eq.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "msg.h"
#include "pw_types.h"
#include "config.h"
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_surface.h>
#include <cstdio>
#include <cstdlib>

App::App() {
    ui_done = false;
    ui_thread = new std::thread(&App::ui_start, this);
    eq_channel = new Channel<Msg>();
    ui_channel = new Channel<Msg>();

    equalizer = new Equalizer(eq_channel, ui_channel);

    config_path = std::string(getenv("HOME"));
    config_path.append("/eq-config.txt");
    
    Config::deserialize_config(config_path.c_str(), commands);
    for(const auto command : commands) {
        equalizer->commands.push_back(command);
    }
    update_response_samples();

    App::instance = this;
    std::signal(SIGINT, sigint);
}

App::~App() {
    free(equalizer);
}

static float peak_gain = 0.0;

static const double tick_nums[] = {
    20, 30, 40, 50, 60, 70, 80, 90,
    100, 200, 300, 400, 500, 600, 700, 800, 900,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
    10000, 13000, 16000, 20000
};

static const int n_ticks = sizeof(tick_nums)/sizeof(double);

static const char* const tick_labels[] = {
    "20", "30", "40", "50", "60", "", "80", "",
    "100", "200", "300", "400", "", "600", "", "800", "",
    "1k", "2k", "3k", "4k", "5k", "6k", "", "8k", "",
    "10k", "13k", "16k", "20k"
};

bool App::ui_render() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);

    static std::string selected_name = "Default";
    ImGui::Columns(2, nullptr, false);
    ImGui::Text("Device:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("###devices", selected_name.c_str())) {
        /*if(audio_sink_info_mutex.try_lock()) {
            for(int i = 0; i < audio_sink_info.size(); i++) {
                if(ImGui::Selectable(audio_sink_info[i].name.c_str(), false)) {
                    selected_name = audio_sink_info[i].name;
                }
            }
            audio_sink_info_mutex.unlock();
        }*/
        for(const auto device : devices) {
            if(ImGui::Selectable(device.desc, false)){

            }
        }
        ImGui::EndCombo();
    }
    ImGui::NextColumn();
    ImGui::Text("Channel configuration:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("###channelconfig", "67.1 Surround")) {
        ImGui::EndCombo();
    }
    ImGui::EndColumns();

    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, ImGui::CalcTextSize("Channels: Abc").x + 80.0);
    ImGui::Text("Channels:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Abc").x + 50.0);
    if(ImGui::BeginCombo("###channels", "L")) {
        ImGui::EndCombo();
    }
    ImGui::NextColumn();
    if(ImPlot::BeginPlot("Frequency Response", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("Frequency (Hz)", "Gain (dB)",
                ImPlotAxisFlags_Lock | ImPlotAxisFlags_ShowMinorTickLabels, ImPlotAxisFlags_ShowMinorTickLabels);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
        ImPlot::SetupAxisLimits(ImAxis_X1, 10, 20000);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -60, 20);
        ImPlot::SetupAxisTicks(ImAxis_X1, tick_nums, n_ticks, tick_labels, false);
        ImPlotSpec spec;
        ImPlot::PlotLine("Response", resp_samples_x, resp_samples,
                            sizeof(resp_samples)/sizeof(float), spec);
        spec.LineColor = ImVec4(193.0/255.0, 114.0/255.0, 144.0/255.0, 1.0);
        spec.Flags = ImPlotLineFlags_Shaded;
        ImPlot::PlotLine("Peaking Response", resp_samples_x, bad_resp_samples,
                            sizeof(resp_samples)/sizeof(float), spec);
        ImPlot::EndPlot();
    }
    ImGui::EndColumns();
    ImGui::Text("Peak gain: %f dB", peak_gain);

    ImGui::BeginListBox("##Commands", ImVec2(-1.0, -1.0));

    bool combo = false;
    for(int i = 0; i < commands.size(); i++) {
        ImGui::PushID(i);
        bool changed = false;
        add_filter_menu(i);
        ImGui::Separator();
        switch(commands[i].type) {
        case CommandType::PREAMP:
            changed = rack_preamp(commands[i]);
            break;
        case CommandType::PEAKING:
            changed = rack_pk(commands[i]);
            break;
        case CommandType::LOW_SHELF:
            changed = rack_shelf(commands[i], true);
            break;
        case CommandType::HIGH_SHELF:
            changed = rack_shelf(commands[i], false);
            break;
        }
        if(changed) {
            equalizer->commands_mutex.lock();
            equalizer->commands[i] = commands[i];
            equalizer->commands_mutex.unlock();
            update_response_samples();
            Config::serialize_config(config_path.c_str(), commands);
        }
        if(ImGui::Button("Delete")) {
            commands.erase(commands.begin() + i);
            equalizer->commands_mutex.lock();
            equalizer->commands.erase(equalizer->commands.begin() + i);
            equalizer->commands_mutex.unlock();
            update_response_samples();
            Config::serialize_config(config_path.c_str(), commands);
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    add_filter_menu(commands.size());

    ImGui::EndListBox();

    ImGui::End();

    return true;
}

#define GAIN_INPUT ImGui::DragFloat("gain", &command.audio.gain, .1, -20.0f, 20.0f, "%.2f dB")
#define CENTER_FREQ changed |= ImGui::DragFloat("center", &command.audio.center_freq, 12.0f, 1.0f, 22000.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic)
#define QFAC ImGui::DragFloat("Q Factor", &command.audio.q, .1, .333f, 33.333f)
#define BANDWIDTH ImGui::DragFloat("oct Bandwith", &command.audio.bandwidth, .1, .01f, 20.0f)
#define FIXED_S_CALC float A = powf(10.0, command.audio.gain/40.0); \
                command.audio.q = 1.0/sqrtf((A + 1.0/A)*(1.0/.9-1.0)+2.0);

bool App::rack_preamp(Command& command) {
    bool changed = false;
    ImGui::Text("Preamp");
    changed = GAIN_INPUT;
    ImGui::SameLine();
    if(ImGui::Button("Level to peak gain")) {
        command.audio.gain -= peak_gain;
        changed = true;
    }

    return changed;
}

bool App::rack_pk(Command& command) {
    bool changed = false;
    ImGui::Text("Peaking");
    GAIN_INPUT;
    CENTER_FREQ;
    if(!command.audio.use_bandwith) {
        if(QFAC) {
            command.audio.update_bandwidth();
            changed = true;
        }
    }else {
        if(BANDWIDTH) {
            command.audio.update_q();
            changed = true;
        }
    }
    if(changed) {
        for(auto &filter : *command.audio.filters)
            filter.peakDbQ(command.audio.center_freq/44100.0, command.audio.gain, command.audio.q);
    }

    return changed;
}

bool App::rack_shelf(Command& command, bool is_low) {
    bool changed = false;
    if(is_low) ImGui::Text("Low Shelf");
    else ImGui::Text("High Shelf");
    GAIN_INPUT;
    CENTER_FREQ;
    bool combo = false;
    switch(command.audio.shaper) {
    case ShelfShaper::Q:
        combo = ImGui::BeginCombo("Shaper", "Q Factor");
        break;
    case ShelfShaper::FIXED_S:
        combo = ImGui::BeginCombo("Shaper", "Fixed S");
        break;
    case ShelfShaper::SLOPE:
        combo = ImGui::BeginCombo("Shaper", "Slope");
        break;
    }
    if(combo) {
        if(ImGui::Selectable("Q", command.audio.shaper == ShelfShaper::Q)) {
            command.audio.shaper = ShelfShaper::Q;
            changed = true;
        }else if(ImGui::Selectable("Fixed S", command.audio.shaper == ShelfShaper::FIXED_S)) {
            command.audio.shaper = ShelfShaper::FIXED_S;
            changed = true;
        }
        ImGui::EndCombo();
    }

    if(command.audio.shaper == ShelfShaper::Q) {
        changed |= QFAC;
    }

    if(changed) {
        if(command.audio.shaper == ShelfShaper::FIXED_S) { FIXED_S_CALC }
        if(is_low) {
            for(auto &filter : *command.audio.filters)
                filter.lowShelfDbQ(command.audio.center_freq/44100.0, command.audio.gain, command.audio.q);
        }else {
            for(auto &filter : *command.audio.filters)
                filter.highShelfDbQ(command.audio.center_freq/44100.0, command.audio.gain, command.audio.q);
        }
    }

    return changed;
}

void App::add_filter_menu(int pos) {
    if(ImGui::BeginMenu("Add Filter Command")) {
        if(ImGui::BeginMenu("Basic Filters")) {
            if (ImGui::MenuItem("Preamp"))   {
                Command cmd = {
                    .type = CommandType::PREAMP,
                    .audio = {
                        .gain = 0
                    }
                };
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Parametric Filters")) {
            if (ImGui::MenuItem("Low Shelf Filter"))   {
                Command cmd = {
                    .type = CommandType::LOW_SHELF,
                    .audio = {
                        .gain = 0.0,
                        .center_freq = 100,
                        .q = 0.7,
                        .filters = new std::vector<Filter>()
                    }
                };
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            if (ImGui::MenuItem("High Shelf Filter"))   {
                Command cmd = {
                    .type = CommandType::HIGH_SHELF,
                    .audio = {
                        .gain = 0.0,
                        .center_freq = 10000,
                        .q = 0.7,
                        .filters = new std::vector<Filter>()
                    }
                };
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            if (ImGui::MenuItem("Peaking Filter"))   {
                Command cmd = {
                    .type = CommandType::PEAKING,
                    .audio = {
                        .gain = 0.0,
                        .center_freq = 100,
                        .q = 10.0,
                        .filters = new std::vector<Filter>()
                    }
                };
                cmd.audio.update_bandwidth();
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
}

int App::loop() {
    equalizer->loop();
    ui_thread->join();

    return 0;
}

void toggle_window_callback(void* app, SDL_TrayEntry* _entry) {
    static_cast<App*>(app)->hide_window = !static_cast<App*>(app)->hide_window;
}

void quit_callback(void* app, SDL_TrayEntry* _entry) {
    static_cast<App*>(app)->quit();
}

static bool tray_created = false;
static SDL_Surface* tray_icon_surface = nullptr;

void App::ui_start() {
    last_hide_window = true;

    SDL_SetHint(SDL_HINT_APP_ID, "org.bloompa.EqualizerPWF");
    SDL_SetHint(SDL_HINT_APP_NAME, "Equalizer PWF");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return;
    }

#ifdef FLATPAK
    tray_icon_surface = SDL_LoadSurface("/app/share/icons/hicolor/128x128/apps/org.bloompa.EqualizerPWF.png");
#else
    tray_icon_surface = SDL_LoadSurface("equalizer-pwf-tray.png");
#endif

    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow("Equalizer PWF", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Create GPU Device
    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr) {
        printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
        return;
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
        printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
        return;
    }
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
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

    io.Fonts->AddFontDefaultVector();

    tray = SDL_CreateTray(tray_icon_surface, "Equalizer PWF");
    menu = SDL_CreateTrayMenu(tray);
    entry[0] = SDL_InsertTrayEntryAt(menu, 0, "Show/Hide Window", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(entry[0], toggle_window_callback, this);
    entry[1] = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(entry[1], quit_callback, this);

    while(!ui_done)
        ui_loop();

    ui_end();

    return;
}

static ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

void App::ui_event_handler(SDL_Event &event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (
        event.type == SDL_EVENT_QUIT ||
        (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
    )
        hide_window = true;
}

void App::ui_loop() {
    SDL_Event event;

    Msg* msg = ui_channel->receive();
    if(msg != nullptr) {
        switch(msg->type) {
        case MsgType::QUIT:
            quit();
            break;
        case MsgType::DEVICE_LIST:
            devices.clear();
            for(auto device : *static_cast<std::unordered_map<uint32_t, PwDevice>*>(msg->data)) {
                PwDevice new_device = device.second;
                std::string str = new_device.desc;
                new_device.desc = (const char*)malloc(str.size()+1);
                strncpy((char*)new_device.desc, str.c_str(), str.size());
                devices.push_back(new_device);
            }
            break;
        default:
            printf("Msg type %i UI unimplemented\n", msg->type);
            break;
        }
    }

    if(last_hide_window != hide_window) {
        if(hide_window) on_hide_window();
        else on_show_window();
        last_hide_window = hide_window;
    }

    if(hide_window) {
        while(SDL_PollEvent(&event)) {
            ui_event_handler(event);
        }
        SDL_HideWindow(window);
        SDL_Delay(10);
    }else {
        while(SDL_PollEvent(&event)) {
            ui_event_handler(event);
        }

        SDL_ShowWindow(window);
        if(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            return;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();


        ui_render();

        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

        SDL_GPUTexture* swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture

        if (swapchain_texture != nullptr && !is_minimized) {
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
}

void App::ui_end() {
    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    SDL_DestroyTray(tray);
    SDL_DestroySurface(tray_icon_surface);

    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

float to_db(float m) {
    const float eps = 1e-9f;              // avoid log(0)
    return 20.0f * log10f(fmaxf(m, eps));
}

void App::update_response_samples() {
    for(int i = 0; i < sizeof(resp_samples)/sizeof(float); i++) {
        resp_samples[i] = 0.0;
        resp_samples_x[i] = i * 2 + 1;
    }

    for(const auto &command : commands) {
        for(int i = 0; i < sizeof(resp_samples)/sizeof(float); i++) {
            int fq = i*2 + 1;
            switch(command.type) {
            case CommandType::PREAMP:
                {
                    float gain_linear = GAIN(command.audio.gain);
                    resp_samples[i] += 20.0f * log10f(fmaxf(gain_linear, 1e-9f));
                }
                break;
            default:
                if(command.audio.filters != nullptr && command.audio.filters->size() > 0)
                    resp_samples[i] += (*command.audio.filters)[0].responseDb((float)fq/44100.0);
                break;
            }
        }
    }

    peak_gain = -9999.0;
    for(int i = 0; i < sizeof(resp_samples)/sizeof(float); i++) {
        if(resp_samples[i] > peak_gain) peak_gain = resp_samples[i];
        if(resp_samples[i] > 0.0f) {
            bad_resp_samples[i] = resp_samples[i];
            resp_samples[i] = 0;
        }else {
            bad_resp_samples[i] = 0;
        }
    }
}

void App::on_show_window() {
    eq_channel->send({
        .type = MsgType::DEVICE_LIST,
    });
}

void App::on_hide_window() {

}

void App::quit() {
    if(ui_thread->get_id() == std::this_thread::get_id()) {
        ui_done = true;
        eq_channel->send({
            .type = MsgType::QUIT,
        });
    }else {
        ui_channel->send({
            .type = MsgType::QUIT,
        });
    }
}

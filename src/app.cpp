#include "app.h"
#include "eq.h"
#include "imgui.h"
#include "implot.h"
#include "msg.h"
#include "pw_types.h"
#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

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

bool App::ui_render() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);

    static std::string selected_name = "Default";
    if(ImGui::BeginCombo("###combo", selected_name.c_str())) {
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

    if(ImPlot::BeginPlot("Frequency Response", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("Frequency (Hz)", "Gain (dB)", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
        ImPlot::SetupAxisLimits(ImAxis_X1, 1, 22000);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -60, 20);
        ImPlotSpec spec;
        spec.Flags = ImPlotLineFlags_Shaded;
        ImPlot::PlotLine("Response", resp_samples_x, resp_samples, sizeof(resp_samples)/sizeof(float), spec);
        spec.LineColor = ImVec4(193.0/255.0, 114.0/255.0, 144.0/255.0, 1.0);
        ImPlot::PlotLine("Peaking Response", resp_samples_x, bad_resp_samples, sizeof(resp_samples)/sizeof(float), spec);
        ImPlot::EndPlot();
    }
    ImGui::Text("Peak gain: %f dB", peak_gain);

    ImGui::BeginListBox("##Commands", ImVec2(-1.0, -1.0));

    for(int i = 0; i < commands.size(); i++) {
        ImGui::PushID(i);
        bool changed = false;
        add_filter_menu(i);
        switch(commands[i].type) {
        case FilterCommandType::PREAMP:
            ImGui::Text("Preamp");
            changed |= ImGui::SliderFloat("gain", &commands[i].audio.gain, -20.0f, 20.0f, "%f dB");
            ImGui::SameLine();
            if(ImGui::Button("Level to peak gain")) {
                commands[i].audio.gain -= peak_gain;
                changed = true;
            }
            break;
        case FilterCommandType::PEAKING:
            ImGui::Text("Peaking");
            changed |= ImGui::SliderFloat("gain", &commands[i].audio.gain, -20.0f, 20.0f, "%f dB");
            changed |= ImGui::SliderFloat("center", &commands[i].audio.peaking.center_freq, 1.0f, 22000.0f, "%f Hz", ImGuiSliderFlags_Logarithmic);
            if(!commands[i].audio.peaking.use_bandwith) {
                if(ImGui::SliderFloat("Q Factor", &commands[i].audio.peaking.q, .333f, 33.333f)) {
                    commands[i].audio.peaking.update_bandwidth();
                    changed = true;
                }
            }else {
                if(ImGui::SliderFloat("oct Bandwith", &commands[i].audio.peaking.bandwidth, -20.0f, 20.0f)) {
                    commands[i].audio.peaking.update_q();
                    changed = true;
                }
            }
            if(changed) {
                commands[i].audio.filter_l.peakDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.peaking.q);
                commands[i].audio.filter_r.peakDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.peaking.q);
            }
            break;
        case FilterCommandType::LOW_SHELF:
            ImGui::Text("Low Shelf");
            changed |= ImGui::SliderFloat("gain", &commands[i].audio.gain, -20.0f, 20.0f, "%f dB");
            changed |= ImGui::SliderFloat("center", &commands[i].audio.shelf.center_freq, 1.0f, 22000.0f, "%f Hz", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Q Factor", &commands[i].audio.shelf.q, .333f, 33.333f);
            if(changed) {
                commands[i].audio.filter_l.lowShelfDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.shelf.q);
                commands[i].audio.filter_r.lowShelfDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.shelf.q);
            }
            break;
        case FilterCommandType::HIGH_SHELF:
            ImGui::Text("High Shelf");
            changed |= ImGui::SliderFloat("gain", &commands[i].audio.gain, -20.0f, 20.0f, "%f dB");
            changed |= ImGui::SliderFloat("center", &commands[i].audio.shelf.center_freq, 1.0f, 22000.0f, "%f Hz", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Q Factor", &commands[i].audio.shelf.q, .333f, 33.333f);
            if(changed) {
                commands[i].audio.filter_l.highShelfDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.shelf.q);
                commands[i].audio.filter_r.highShelfDbQ(commands[i].audio.peaking.center_freq/44100.0, commands[i].audio.gain, commands[i].audio.shelf.q);
            }
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

void App::add_filter_menu(int pos) {
    if(ImGui::BeginMenu("Add Filter Command")) {
        if(ImGui::BeginMenu("Basic Filters")) {
            if (ImGui::MenuItem("Preamp"))   {
                FilterCommand cmd = {
                    .type = FilterCommandType::PREAMP,
                    .audio = {
                        .filter_l = Filter(),
                        .filter_r = Filter(),
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
                FilterCommand cmd = {
                    .type = FilterCommandType::LOW_SHELF,
                    .audio = {
                        .filter_l = Filter(),
                        .filter_r = Filter(),
                        .gain = 0.0,
                        .shelf = {
                            .center_freq = 100,
                            .q = 10.0
                        }
                    }
                };
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            if (ImGui::MenuItem("High Shelf Filter"))   {
                FilterCommand cmd = {
                    .type = FilterCommandType::HIGH_SHELF,
                    .audio = {
                        .filter_l = Filter(),
                        .filter_r = Filter(),
                        .gain = 0.0,
                        .shelf = {
                            .center_freq = 10000,
                            .q = 10.0
                        }
                    }
                };
                commands.insert(commands.begin() + pos, cmd);
                equalizer->commands_mutex.lock();
                equalizer->commands.insert(equalizer->commands.begin() + pos, cmd);
                equalizer->commands_mutex.unlock();
            }
            if (ImGui::MenuItem("Peaking Filter"))   {
                FilterCommand cmd = {
                    .type = FilterCommandType::PEAKING,
                    .audio = {
                        .filter_l = Filter(),
                        .filter_r = Filter(),
                        .gain = 0.0,
                        .peaking = PeakingConfig {
                            .center_freq = 100,
                            .q = 10.0
                        }
                    }
                };
                cmd.audio.peaking.update_bandwidth();
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

void show_window_callback(void* app, SDL_TrayEntry* _entry) {
    static_cast<App*>(app)->hide_window = false;
}

void quit_callback(void* app, SDL_TrayEntry* _entry) {
    static_cast<App*>(app)->quit();
}

void App::ui_start() {
    last_hide_window = true;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return;
    }

    tray = SDL_CreateTray(NULL, "Equalizer PWF");
    menu = SDL_CreateTrayMenu(tray);
    entry[0] = SDL_InsertTrayEntryAt(menu, 0, "Show Window", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(entry[0], show_window_callback, this);
    entry[1] = SDL_InsertTrayEntryAt(menu, 1, "Quit", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(entry[1], quit_callback, this);

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

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

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
    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    //SDL_DestroyTray(tray);
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
        resp_samples_x[i] = i * 4 + 1;
    }

    for(const auto &command : commands) {
        for(int i = 0; i < sizeof(resp_samples)/sizeof(float); i++) {
            int fq = i*2 + 1;
            switch(command.type) {
            case FilterCommandType::PREAMP:
                {
                    float gain_linear = GAIN(command.audio.gain);
                    resp_samples[i] += 20.0f * log10f(fmaxf(gain_linear, 1e-9f));
                }
                break;
            default:
                resp_samples[i] += command.audio.filter_l.responseDb((float)fq/44100.0);
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

#pragma once
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <csignal>

#include "channel.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <SDL3/SDL.h>

#include "eq.h"
#include "msg.h"
#include "pw_types.h"

class App {
public:
    App();
    ~App();
    int loop();
    std::atomic<bool> ui_done;
    bool hide_window;
    bool last_hide_window;
    void quit();

    void on_show_window();
    void on_hide_window();

private:
    inline static App* instance;

    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu_device = nullptr;
    SDL_Tray* tray = nullptr;
    SDL_TrayMenu* menu = nullptr;
    SDL_TrayEntry* entry[2] = {nullptr, nullptr};
    std::thread* ui_thread = nullptr;

    Channel<Msg>* ui_channel = nullptr;
    Channel<Msg>* eq_channel = nullptr;

    Equalizer* equalizer = nullptr;

    std::vector<PwDevice> devices;
    std::vector<Command> commands;

    float resp_samples_x[22000/2] = {0};
    float resp_samples[22000/2] = {0};
    float bad_resp_samples[22000/2] = {0};

    std::string config_path;

    void ui_event_handler(SDL_Event &event);

    void ui_start();
    void ui_end();
    void ui_loop();
    bool ui_render();

    bool rack_preamp(Command& command);
    bool rack_pk(Command& command);
    bool rack_lp(Command& command);
    bool rack_hp(Command& command);
    bool rack_bp(Command& command);
    bool rack_shelf(Command& command, bool is_low);
    bool rack_ap(Command& command);

    void update_response_samples();

    void add_filter_menu(int pos);

    static inline void sigint(int s) { instance->quit(); }
};

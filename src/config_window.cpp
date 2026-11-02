#include "config_window.hpp"
#include "imgui.h"

ConfigWindow::ConfigWindow(AppConfig* config):
m_config(config) {

}

ConfigWindow::~ConfigWindow() {}

bool ConfigWindow::render() {
    bool win_open = true;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("EqualizerPWF Configuration", &win_open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);
    bool changed = false;
    changed |= ImGui::Checkbox("Show window on application start", &m_config->show_window_on_start);
    if(changed) {
        m_config->save();
    }
    if(ImGui::Button("Close")) win_open = false;
    ImGui::End();

    return win_open;
}

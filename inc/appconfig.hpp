#pragma once

#include <string>

class AppConfig {
    std::string config_path;
public: 
    AppConfig(std::string path);
    ~AppConfig();

    void save();

    bool show_window_on_start = false;
};

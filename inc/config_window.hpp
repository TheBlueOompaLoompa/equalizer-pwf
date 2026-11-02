#pragma once
#include "appconfig.hpp"

class ConfigWindow {
    AppConfig* m_config;
public:
    ConfigWindow(AppConfig* config);
    ~ConfigWindow();

    bool render();
};

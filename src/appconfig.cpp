#include "appconfig.hpp"

#include <fstream>
#include <sstream>

AppConfig::AppConfig(std::string path) {
    config_path = path;
    std::ifstream file(path);

    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cmdlet;
        while(iss >> cmdlet) {
            if(cmdlet == "ShowOnStart:") {
                iss >> show_window_on_start;
                while(iss >> cmdlet) {}
            }
        }
    }
}

AppConfig::~AppConfig() {}

void AppConfig::save() {
    std::ofstream file(config_path);

    if(file) {
        file.clear();
        file << "ShowOnStart: " << show_window_on_start;
    }
}

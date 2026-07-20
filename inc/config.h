#pragma once

#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "eq.h"

namespace Config{
static void parse_filter(std::istringstream &iss, std::vector<Command>& commands, int id = -1) {
    std::string cmdlet;
    CommandType type;
    while(iss >> cmdlet) {
        if(cmdlet.compare("LS") == 0) {
            type = CommandType::LOW_SHELF;
            commands.push_back({
                .type = type,
                .audio = {
                    .gain = 0.0,
                    .center_freq = 100,
                    .q = 10.0,
                    .filters = new std::vector<Filter>()
                }
            });
        }else if(cmdlet.compare("HS") == 0) {
            type = CommandType::HIGH_SHELF;
            commands.push_back({
                .type = type,
                .audio = {
                    .gain = 0.0,
                    .center_freq = 100,
                    .q = 10.0,
                    .filters = new std::vector<Filter>()
                }
            });
        }else if(cmdlet.compare("PK") == 0) {
            type = CommandType::PEAKING;
            commands.push_back({
                .type = type,
                .audio = {
                    .gain = 0.0,
                    .center_freq = 100,
                    .q = 10.0,
                    .filters = new std::vector<Filter>()
                }
            });
        }else if(cmdlet.compare("Fc") == 0) {
            switch(type) {
            case CommandType::LOW_SHELF:
            case CommandType::HIGH_SHELF:
            case CommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.center_freq;
                break;
            default:
                break;
            }
        }else if(cmdlet.compare("Q") == 0) {
            switch(type) {
            case CommandType::LOW_SHELF:
            case CommandType::HIGH_SHELF:
            case CommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.q;
                break;
            default:
                break;
            }
            if(type == CommandType::PEAKING) commands[commands.size()-1].audio.update_bandwidth();
        }else if(cmdlet.compare("Oct") == 0) {
            switch(type) {
            case CommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.bandwidth;
                commands[commands.size()-1].audio.update_q();
                break;
            default:
                break;
            }
        }else if(cmdlet.compare("Gain") == 0) {
            iss >> commands[commands.size()-1].audio.gain;
        }
    }
}

static void deserialize_config(const char* path, std::vector<Command>& commands) {
    std::ifstream file(path);

    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cmdlet;
        while(iss >> cmdlet) {
            if(cmdlet.compare("Preamp:") == 0) {
                commands.push_back({
                    .type = CommandType::PREAMP,
                    .audio = { .gain = 0.0 }
                });
                iss >> commands[commands.size()-1].audio.gain;
            }else if(cmdlet.compare("Filter:") == 0) {
                parse_filter(iss, commands);
            }else if(cmdlet.compare("Filter") == 0) {
                std::string next;
                iss >> next;
                if(next.find(":") != std::string::npos) {
                    int id = std::stoi(next.substr(0, next.size()-1));
                    parse_filter(iss, commands, id);
                }
            }
        }
    }
}

static void serialize_config(const char* path, std::vector<Command>& commands) {
    std::ofstream file(path);
    if(file) {
        file.clear();

        for(const auto &command : commands) {
            switch(command.type) {
            case CommandType::PREAMP:
                file << "Preamp: " << command.audio.gain << " dB";
                break;
            case CommandType::LOW_SHELF:
                file << "Filter: ON LS Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.q;
                break;
            case CommandType::HIGH_SHELF:
                file << "Filter: ON HS Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.q;
                break;
            case CommandType::PEAKING:
                file << "Filter: ON PK Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.q;
                if(command.audio.use_bandwith)
                break;
            default:
                std::cout << "Failed to save config param of type " << command.type << std::endl;
            }
            file << std::endl;
        }

        file.close();
    }else std::cerr << "Failed to open file " << path << " for writing!" << std::endl;
}
}

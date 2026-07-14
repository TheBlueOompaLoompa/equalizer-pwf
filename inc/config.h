#pragma once

#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "eq.h"

namespace Config{
static void parse_filter(std::istringstream &iss, std::vector<FilterCommand>& commands, int id = -1) {
    std::string cmdlet;
    FilterCommandType type;
    while(iss >> cmdlet) {
        if(cmdlet.compare("LS") == 0) {
            type = FilterCommandType::LOW_SHELF;
            commands.push_back({
                .type = type,
                .audio = {
                    .filter_l = Filter(),
                    .filter_r = Filter(),
                    .gain = 0.0,
                    .shelf = {
                        .center_freq = 100,
                        .q = 10.0
                    }
                }
            });
        }else if(cmdlet.compare("HS") == 0) {
            type = FilterCommandType::HIGH_SHELF;
            commands.push_back({
                .type = type,
                .audio = {
                    .filter_l = Filter(),
                    .filter_r = Filter(),
                    .gain = 0.0,
                    .shelf = {
                        .center_freq = 100,
                        .q = 10.0
                    }
                }
            });
        }else if(cmdlet.compare("PK") == 0) {
            type = FilterCommandType::PEAKING;
            commands.push_back({
                .type = type,
                .audio = {
                    .filter_l = Filter(),
                    .filter_r = Filter(),
                    .gain = 0.0,
                    .peaking = {
                        .center_freq = 100,
                        .q = 10.0
                    }
                }
            });
        }else if(cmdlet.compare("Fc") == 0) {
            switch(type) {
            case FilterCommandType::LOW_SHELF:
            case FilterCommandType::HIGH_SHELF:
                iss >> commands[commands.size()-1].audio.shelf.center_freq;
                break;
            case FilterCommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.peaking.center_freq;
                break;
            default:
                break;
            }
        }else if(cmdlet.compare("Q") == 0) {
            switch(type) {
            case FilterCommandType::LOW_SHELF:
            case FilterCommandType::HIGH_SHELF:
                iss >> commands[commands.size()-1].audio.shelf.q;
                break;
            case FilterCommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.peaking.q;
                commands[commands.size()-1].audio.peaking.update_bandwidth();
                break;
            default:
                break;
            }
        }else if(cmdlet.compare("Oct") == 0) {
            switch(type) {
            case FilterCommandType::PEAKING:
                iss >> commands[commands.size()-1].audio.peaking.bandwidth;
                commands[commands.size()-1].audio.peaking.update_q();
                break;
            default:
                break;
            }
        }else if(cmdlet.compare("Gain") == 0) {
            iss >> commands[commands.size()-1].audio.gain;
        }
    }

    auto &command = commands[commands.size()-1];
    
    switch(command.type) {
    case FilterCommandType::LOW_SHELF:
        command.audio.filter_l.lowShelfDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.shelf.q);
        command.audio.filter_r.lowShelfDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.shelf.q);
        break;
    case FilterCommandType::HIGH_SHELF:
        command.audio.filter_l.highShelfDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.shelf.q);
        command.audio.filter_r.highShelfDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.shelf.q);
        break;
    case FilterCommandType::PEAKING:
        command.audio.filter_l.peakDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.peaking.q);
        command.audio.filter_r.peakDbQ(command.audio.peaking.center_freq/44100.0, command.audio.gain, command.audio.peaking.q);
        break;
    default:
        std::cout << "Unhandled type " << command.type << " in filter parser filter setup." << std::endl;
    }
}

static void deserialize_config(const char* path, std::vector<FilterCommand>& commands) {
    std::ifstream file(path);

    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cmdlet;
        while(iss >> cmdlet) {
            if(cmdlet.compare("Preamp:") == 0) {
                commands.push_back({
                    .type = FilterCommandType::PREAMP,
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

static void serialize_config(const char* path, std::vector<FilterCommand>& commands) {
    std::ofstream file(path);
    if(file) {
        file.clear();

        for(const auto &command : commands) {
            switch(command.type) {
            case FilterCommandType::PREAMP:
                file << "Preamp: " << command.audio.gain << " dB";
                break;
            case FilterCommandType::LOW_SHELF:
                file << "Filter: ON LS Fc " << command.audio.shelf.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.shelf.q;
                break;
            case FilterCommandType::HIGH_SHELF:
                file << "Filter: ON HS Fc " << command.audio.shelf.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.shelf.q;
                break;
            case FilterCommandType::PEAKING:
                file << "Filter: ON PK Fc " << command.audio.peaking.center_freq << " Hz Gain " << command.audio.gain << " dB Q " << command.audio.peaking.q;
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

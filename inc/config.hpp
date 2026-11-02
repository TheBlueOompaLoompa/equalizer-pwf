#pragma once

#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "command.h"
#include "eq.h"

namespace Config{
static void parse_filter(std::istringstream &iss, std::vector<Command>& commands, int id = -1) {
    std::string cmdlet;
    CommandType type;
    while(iss >> cmdlet) {
        if(cmdlet.compare("LS") == 0) {
            type = CommandType::LOW_SHELF;
            commands.push_back(Command(type, {
                    .gain = 0.0,
                    .center_freq = 100,
                    .shaper = ShelfShaper::FIXED_S,
                    .chain_filters = new std::unordered_map<uint32_t, std::unordered_map<std::string, Filter*>>()
                }));
        }else if(cmdlet.compare("HS") == 0) {
            type = CommandType::HIGH_SHELF;
            commands.push_back(Command(type, {
                    .gain = 0.0,
                    .center_freq = 100,
                    .shaper = ShelfShaper::FIXED_S,
                    .chain_filters = new std::unordered_map<uint32_t, std::unordered_map<std::string, Filter*>>()
                }));
        }else if(cmdlet.compare("PK") == 0) {
            type = CommandType::PEAKING;
            commands.push_back(Command(type, {
                    .gain = 0.0,
                    .center_freq = 100,
                    .q = 3.0,
                    .chain_filters = new std::unordered_map<uint32_t, std::unordered_map<std::string, Filter*>>()
                }));
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
                commands[commands.size()-1].audio.shaper = ShelfShaper::Q;
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

struct ChannelName {
    const char* name;
    uint16_t channel;
};

static const ChannelName CHANNEL_NAMES[] = {
    { "LFE", 1 << 3 }, { "SUB", 1 << 3 },
    { "FL",  1 << 0 }, { "FR",  1 << 1 }, { "FC",  1 << 2 },
    { "RL",  1 << 4 }, { "RR",  1 << 5 }, { "RC",  1 << 6 },
    { "SL",  1 << 7 }, { "SR",  1 << 8 },
    { "L",   1 << 0 }, { "R",   1 << 1 }, { "C",   1 << 2 },
};

static uint16_t parse_channel_num(const std::string& str) {
    for(const auto& entry : CHANNEL_NAMES) {
        if(str == entry.name) return entry.channel;
    }
    return 0;
}

static const char* channel_name(uint16_t channel) {
    switch(channel) {
    case 1 << 0: return "L";
    case 1 << 1: return "R";
    case 1 << 2: return "C";
    case 1 << 3: return "LFE";
    case 1 << 4: return "RL";
    case 1 << 5: return "RR";
    case 1 << 6: return "RC";
    case 1 << 7: return "SL";
    case 1 << 8: return "SR";
    default: return nullptr;
    }
}

static bool match_channel_name(const std::string& str, size_t pos, uint16_t& channel, size_t& length) {
    for(const auto& entry : CHANNEL_NAMES) {
        size_t len = strlen(entry.name);
        if(str.compare(pos, len, entry.name) == 0) {
            channel = entry.channel;
            length = len;
            return true;
        }
    }
    return false;
}

static void parse_channel(std::istringstream &iss, std::vector<Command>& commands) {
    std::string cmdlet;
    uint16_t channels = 0;
    while(iss >> cmdlet) {
        channels |= parse_channel_num(cmdlet);
    }
    commands.push_back(Command(CommandType::CHANNEL, channels));
}

static void parse_comment(std::string &line, std::vector<Command>& commands) {
    Comment* comment = (Comment*)malloc(sizeof(Comment));
    comment->size = line.size()+2;
    comment->text = (char*)malloc(comment->size);
    memcpy(comment->text, line.c_str(), comment->size-1);
    commands.push_back(Command(CommandType::COMMENT, comment));
}

static void parse_color(std::istringstream &iss, std::vector<Command>& commands) {
    uint32_t r, g, b;
    iss >> r;
    iss >> g;
    iss >> b;
    commands.push_back(Command(CommandType::COLOR, (float)r/255.0, (float)g/255.0, (float)b/255.0));
    std::string temp;
    while(iss >> temp) {}
}

static void parse_copy(std::istringstream &iss, std::vector<Command>& commands) {
    std::string line;
    std::getline(iss, line);

    std::string expression;
    for(const char c : line) {
        if(!std::isspace(static_cast<unsigned char>(c))) expression += c;
    }

    size_t pos = 0;
    while(pos < expression.size()) {
        uint16_t to_channel = 0;
        uint16_t from_channels = 0;
        size_t length = 0;

        if(!match_channel_name(expression, pos, to_channel, length)) break;
        pos += length;

        if(pos >= expression.size() || expression[pos] != '=') break;
        pos++;

        bool have_source = false;
        while(pos < expression.size()) {
            uint16_t source = 0;
            if(!match_channel_name(expression, pos, source, length)) break;
            from_channels |= source;
            have_source = true;
            pos += length;

            if(pos < expression.size() && expression[pos] == '+') {
                pos++;
                continue;
            }
            break;
        }

        if(!have_source) break;
        commands.push_back(Command(CommandType::COPY, Copy{ .from_channels = from_channels, .to_channel = to_channel }));
    }
}

static void deserialize_config(const char* path, std::vector<Command>& commands) {
    std::ifstream file(path);

    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cmdlet;
        while(iss >> cmdlet) {
            if(cmdlet == "Preamp:") {
                commands.push_back(Command(CommandType::PREAMP, { .gain = 0.0 }));
                iss >> commands[commands.size()-1].audio.gain;
                while(iss >> cmdlet) {}
            }else if(cmdlet == "Filter:") {
                parse_filter(iss, commands);
            }else if(cmdlet == "Filter") {
                std::string next;
                iss >> next;
                if(next.find(":") != std::string::npos) {
                    int id = std::stoi(next.substr(0, next.size()-1));
                    parse_filter(iss, commands, id);
                }
            }else if(cmdlet == "Channel:") {
                parse_channel(iss, commands);
            }else if(cmdlet == "Color:") {
                parse_color(iss, commands);
            }else if(cmdlet == "Copy:") {
                parse_copy(iss, commands);
            }else {
                parse_comment(line, commands);
                while(iss >> cmdlet) {}
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
                file << "Filter: ON LS Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain << " dB";
                if(command.audio.shaper == ShelfShaper::Q) file << " Q " << command.audio.q;
                break;
            case CommandType::HIGH_SHELF:
                file << "Filter: ON HS Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain << " dB";
                if(command.audio.shaper == ShelfShaper::Q) file << " Q " << command.audio.q;
                break;
            case CommandType::PEAKING:
                file << "Filter: ON PK Fc " << command.audio.center_freq << " Hz Gain " << command.audio.gain;
                if(command.audio.use_bandwith)
                    file << "BW Oct " << command.audio.bandwidth;
                else
                    file << " dB Q " << command.audio.q;
                break;
            case CommandType::CHANNEL:
                file << "Channel: ";
                if(command.channels & (1 << 0)) file << "L ";
                if(command.channels & (1 << 1)) file << "R ";
                if(command.channels & (1 << 2)) file << "C ";
                if(command.channels & (1 << 3)) file << "LFE ";
                if(command.channels & (1 << 4)) file << "RL ";
                if(command.channels & (1 << 5)) file << "RR ";
                if(command.channels & (1 << 6)) file << "RC ";
                if(command.channels & (1 << 7)) file << "SL ";
                if(command.channels & (1 << 8)) file << "SR ";
                break;
            case CommandType::COMMENT:
                file << command.comment->text;
                break;
            case CommandType::COLOR:
                file << "Color: " << (int)(command.color.r*255.0) << " " << (int)(command.color.g*255.0) << " " << (int)(command.color.b*255.0);
                break;
            case CommandType::COPY:
                file << "Copy: ";
                {
                    const char* destination = nullptr;
                    for(int i = 0; i < 9; i++) {
                        if(command.copy.to_channel & (1 << i)) {
                            destination = channel_name(1 << i);
                            break;
                        }
                    }
                    if(destination == nullptr) break;

                    file << destination << '=';

                    bool need_plus = false;
                    for(int i = 0; i < 9; i++) {
                        if(command.copy.from_channels & (1 << i)) {
                            if(need_plus) file << '+';
                            file << channel_name(1 << i);
                            need_plus = true;
                        }
                    }
                }
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

#pragma once

#include "command.h"

enum MsgType {
    QUIT,
    DEVICE_LIST,
    UPDATE_CONFIG,
    COMMANDS_CHANGED,
    FREQ_RESPONSE_COMPUTED,
};

struct Msg_CommandData {
    int id;
    Command cmd;
};

struct Msg {
    MsgType type;
    union {
        void* data = nullptr;
        Msg_CommandData* command_data;
        int* id;
    };
    bool free = false;
};


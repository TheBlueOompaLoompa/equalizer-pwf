#pragma once

#include "command.h"

enum MsgType {
    QUIT,
    DEVICE_LIST,
    UPDATE_CONFIG,
    UPSERT_COMMAND,
    DELETE_COMMAND,
};

struct Msg_UpsertData {
    int id;
    Command cmd;
};

struct Msg {
    MsgType type;
    union {
        void* data = nullptr;
        Msg_UpsertData* upsert_data;
        int* id;
    };
    bool free = false;
};


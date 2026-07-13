#pragma once

enum MsgType {
    QUIT,
    DEVICE_LIST,
    UPDATE_CONFIG,
};

struct Msg {
    MsgType type;
    void* data = nullptr;
    bool free = false;
};


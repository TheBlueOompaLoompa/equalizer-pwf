#pragma once

#include <mutex>
#include <vector>

template<typename T>
class Channel {
    std::mutex mut;
    std::vector<T> msgs;

public:
    Channel() {}
    ~Channel() {}

    void send(T msg) {
        mut.lock();
        msgs.insert(msgs.begin(), msg);
        mut.unlock();
    }

    T* receive() {
        T* res = nullptr;
        if(mut.try_lock()) {
            if(msgs.size() > 0) {
                res = &msgs[msgs.size()-1];
                msgs.pop_back();
            }
            mut.unlock();
        }
        return res;
    }
};

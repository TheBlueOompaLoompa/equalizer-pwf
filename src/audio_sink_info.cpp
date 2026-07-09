#include <audio_sink_info.h>

AudioSinkInfo::AudioSinkInfo(uint32_t id, std::string nick) {
    this->id = id;
    this->nick = nick;
}

AudioSinkInfo::~AudioSinkInfo() {
}

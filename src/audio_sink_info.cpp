#include <audio_sink_info.h>

AudioSinkInfo::AudioSinkInfo(uint32_t id, std::string name) {
    this->id = id;
    this->name = name;
}

AudioSinkInfo::~AudioSinkInfo() {
}

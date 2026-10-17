// Optional tap: the first seconds of exactly what was played, as a WAV, because simctl recordVideo does not capture
// audio.
#pragma once
#include <cstdint>
#include <string>
extern "C" {
#include "ar_runtime.h"
}
namespace ar {
class VosAudio {
public:
    explicit VosAudio(ar_runtime *rt);
    ~VosAudio();
    bool start(float tap_seconds, std::string *err);
    bool mixes_lanes() const;
    void stop();
    // An AVAudioSession interruption stops the unit too; when it ends the unit plays again only if a layer is attached.
    void pause();
    void resume();
    bool running() const;
    uint64_t frames_out() const;
    uint64_t missing_in() const;
    uint64_t trimmed_in() const;
    bool tap_ready() const;
    bool write_tap(const std::string &path);
    struct Impl;
private:
    Impl *impl_;
};
}

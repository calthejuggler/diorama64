#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ar_audio_settings.h"
#include "debug_points.h"
#include "vos_polyphase.h"
#include "vos_scene_pos.h"
extern "C" {
#include "ar_audio_tuning.h"
#include "ar_runtime.h"
}
namespace ar {
class VosPhase {
public:
    VosPhase(ar_runtime *rt, float tap_seconds, bool polyphase = false);
    ~VosPhase();
    // Setup thread, before the game boots. A failed rung is torn down completely; false when no rung runs (then use
    // RemoteIO stereo).
    bool start_engine(const std::vector<PhaseRungPlan> &rungs, AudioSpatializer spatializer, std::string *err);
    bool start_voices(std::string *err);
    bool voices_failed() const;
    void update(const ar_render_frame &rf);
    // Layer thread (the same thread as update); from the next update.
    void set_tuning(const ar_audio_tuning &t);
    void set_spatializer(AudioSpatializer m);
    void set_poly_mode(PolyMode m);
    void pause();
    void resume();
    bool running() const;
    void stop();
    const char *render_mode() const;
    const char *rung() const;
    PhaseRung started_rung() const;
    const char *head_mode() const;
    std::string counters() const;
    std::string lanes_debug() const;
    void sound_points(std::vector<DebugPoint> *out, float size_factor = 1.f) const;
    bool tap_ready() const;
    bool write_tap(const std::string &path);
    struct Impl;
private:
    Impl *impl_;
};
}

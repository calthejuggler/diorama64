// One per session, render thread. Shared by the visionOS session and the harness so both light the same way.
#pragma once
#include <deque>
#include "render_device.h"
#include "room_light.h"
extern "C" {
#include "ar_runtime.h"
}
namespace ar {
class RoomLightDriver {
public:
    void set_key_light_callback(void (*fn)(void *ctx, int on, const float dir[3]), void *ctx) { key_fn_ = fn; key_ctx_ = ctx; }
    void set_log_callback(void (*fn)(void *ctx, const char *line), void *ctx) { log_fn_ = fn; log_ctx_ = ctx; }
    static constexpr double kProbeLogMinS = 1.0;
    LightingFrame begin(IRenderDevice &dev, const LookSettings &look, const ar_render_frame &rf);
    void encode(IRenderDevice &dev, const ar_render_frame &rf);
    RoomLightStatus status() const { return status_; }
    LightingFrame step(const LookSettings &look, const ar_render_frame &rf, const EnvStats *reduced, uint32_t reduced_generation);
    // The slot is never one this frame's LightingFrame shows.
    int note_environment(const ar_render_frame &rf);
    void set_copied(void *cube);
    static constexpr double kEnvFadeS = kRoomLightTauS;
    static constexpr double kEnvFadeSteadyS = 8.0;
    static constexpr float kEnvCubeLumRatio = 1.25f, kEnvCubeDirDeg = 20.f;
    static constexpr double kEstimateLogMinS = 2.0;
private:
    struct Pending { uint32_t generation; ar_pose tracking_from_probe; float scale_ref; int slot; void *cube; };
    RoomLightEstimator est_;
    std::deque<Pending> pending_;
    uint32_t submitted_ = 0, used_ = 0;
    bool have_probe_ = false;
    ar_pose probe_pose_{};
    float table_quat_[4] = {0.f, 0.f, 0.f, 1.f};
    bool have_table_ = false;
    KeyLightSender key_;
    void (*key_fn_)(void *, int, const float *) = nullptr;
    void *key_ctx_ = nullptr;
    RoomLightStatus status_;
    struct EnvCube { void *cube = nullptr; ar_pose pose{}; float scale = 0.f; float lum = 0.f; float dir_track[3] = {0.f, 1.f, 0.f}; bool directional = false; };
    EnvCube env_[IRenderDevice::kEnvCopySlots];
    int env_cur_ = -1, env_prev_ = -1, env_wait_ = -1;
    float env_fade_ = 1.f, env_mix_ = 0.f;
    bool have_env_t_ = false;
    double env_t_ = 0.0;
    void adopt_cube(const Pending &p, const ProbeSample &s, const LookSettings &look, const ar_pose &tracking_from_table);
    float exposure_of(const EnvCube &c, const LookSettings &look, bool *ok) const;
    void log_probe(const LookSettings &look, const Pending &p, double t);
    void (*log_fn_)(void *, const char *) = nullptr;
    void *log_ctx_ = nullptr;
    double last_probe_t_ = 0.0, logged_t_ = 0.0;
    bool probes_seen_ = false, logged_ = false;
    uint32_t log_skipped_ = 0;
    void log_steady(const LookSettings &look, double t);
    uint64_t est_logged_changes_ = 0, fast_logged_ = 0, reverts_logged_ = 0;
    double est_logged_t_ = 0.0;
    bool est_logged_ = false;
};
}

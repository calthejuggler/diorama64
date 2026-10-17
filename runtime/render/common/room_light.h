#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "lighting.h"
#include "look.h"
extern "C" {
#include "ar_types.h"
}
namespace ar {
struct LightRig {
    float dir[3] = {0.f, 1.f, 0.f};    // towards the light, table (= level) axes, unit
    float tint[3] = {1.f, 1.f, 1.f};
    float gain = 1.f;
    float ambient = 0.5f;
    bool from_room = false;
    float directionality = 1.f;
};
LightRig fixed_light_rig(const LookSettings &s);
LightingFrame lighting_frame(const LookSettings &s, const LightRig &r);
class KeyLightSender {
public:
    bool changed(bool on, const float dir[3]);
private:
    bool sent_ = false, on_ = false;
    float dir_[3] = {0.f, 1.f, 0.f};
};
struct ProbeSample {
    float l0[3];
    float l1[3];
    float scale_ref;
    float table_from_probe[9];
};
void table_from_probe_rotation(ar_pose tracking_from_table, ar_pose tracking_from_probe, float out[9]);
// Metal's cube faces (slice order +X, -X, +Y, -Y, +Z, -Z); u right, v down, both in [-1, 1]; out unnormalised.
void cube_face_dir(int face, float u, float v, float out[3]);
std::vector<float> fake_probe_texels(uint32_t size, const float dir[3], const float rgb[3], float ambient);
// The room light's easing time constant: every output glides towards its target, a new probe's or the Fixed light's,
// by 1 - exp(-dt / tau) per frame: the same light at any frame rate. The key direction also turns at most
// kRoomLightTurnDegS, and at a fifth of the speed (or more, up to full speed at D 0.5) the less directional the room.
constexpr float kRoomLightTauS = 3.f, kRoomLightTurnDegS = 20.f;
float room_light_ease(double dt_s);
// Steady: the probes are noisy samples of a slowly changing room, not truth. The long-run estimate moves towards each
// probe by 1 - exp(-gap / SM64_LIGHT_TAU_S), per probe, not per frame, so a burst or a lone outlier moves it only a
// little.
constexpr float SM64_LIGHT_TAU_S = 20.f;
constexpr float kSteadyGapMinS = 0.5f, kSteadyGapMaxS = 10.f, kSteadyNominalGapS = 3.f;
constexpr float kSteadyTrackFraction = 0.25f;
constexpr float kFastAgreeRatio = 1.3f;
constexpr float kProvisionalS = 30.f, kRecurringS = 60.f, kOutlierCapRatio = 1.25f;
// the key direction's own, slower share per probe (about a 60 s time constant at a 3 s cadence; the room's
// key scatters ~33 degrees from probe to probe), with a 4 degree deadband. An outlier's colour and directionality count
// at half its share. Fast paths never move the key.
constexpr float kSteadyKeyShare = 0.05f;
constexpr float kSteadyGainBand = 0.04f, kSteadyDirBandDeg = 4.f, kSteadyAmbientBand = 0.03f, kSteadyTintBand = 0.03f;
constexpr float kSteadyEaseS = 6.f, kSteadyTurnDegS = 5.f;
constexpr float kFastRatio = 1.8f, kFastHoldS = 4.f, kFastEaseS = 2.f;
float light_ease(double dt_s, float tau_s);
struct RoomReading {
    float lum = 0.f;
    float scale = 0.f;
    float col[3] = {1.f, 1.f, 1.f};
    float D = 0.f;
    float dir[3] = {0.f, 1.f, 0.f};
    bool has_dir = false;
};
class RoomLightEstimator {
public:
    void add(const ProbeSample &p, double t_s = -1.0);
    bool has_probe() const { return probes_ > 0; }
    LightRig rig(const LookSettings &s, double t_s);
    LightRig target(const LookSettings &s) const;
    float abs_lum() const { return abs_lum_; }
    float neutral(const LookSettings &s) const { return s.neutral_lum > 0.f ? s.neutral_lum : first_lum_; }
    uint64_t probes() const { return probes_; }
    void set_table_from_probe(const float m[9]) { for (int k = 0; k < 9; k++) last_.table_from_probe[k] = m[k]; }
    // The table turned by q (new table axes from old): the current key, room-anchored, turns with it at once, so only a
    // new probe's direction is eased.
    void turn_current(const float q[4]);
    void turn_table(const float q[4]);
    const RoomReading &estimate() const { return est_; }
    const RoomReading &applied() const { return app_; }
    LightRig applied_target(const LookSettings &s) const { return rig_of(app_, s); }
    float env_scale() const { return env_scale_; }
    uint64_t applied_changes() const { return applied_changes_; }
    uint64_t fast_paths() const { return fast_paths_; }
    uint64_t fast_reverts() const { return fast_reverts_; }
    const RoomReading &last_revert() const { return reverted_to_; }
    RoomReading reading(const ProbeSample &p) const { return reading_of(p); }
    struct FastPath { double t = 0.0; float from_lum = 0.f, to_lum = 0.f; int probes = 0; };
    const FastPath &last_fast_path() const { return fast_; }
private:
    LightRig rig_of(const RoomReading &r, const LookSettings &s) const;
    RoomReading reading_of(const ProbeSample &p) const;
    LightRig rig_steady(const LookSettings &s, double t_s);
    void average_lum(float log_lum, float log_scale, float a);
    void fire(float log_lum, float log_scale, double t, int probes);
    // Steady state (kept in both responses, so a live switch to Steady starts from the room's history)
    RoomReading est_, app_;
    bool steady_was_ = false;
    double last_add_t_ = -1.0;
    bool stamp_ = false;
    struct Held { bool on = false, lone_ok = false; float log_lum = 0.f, log_scale = 0.f, a = 0.f; int sign = 0; double t = -1.0;
                  RoomReading room; bool room_pending = false; } held_;
    std::deque<float> recent_;
    struct Off { bool on = false; int sign = 0; float log_lum = 0.f, log_scale = 0.f, ref = 0.f; } off_;
    struct Provisional { bool on = false; float pre_lum = 0.f, pre_scale = 0.f, log_lum = 0.f, log_scale = 0.f; double t = -1.0;
                         RoomReading room; float a = 0.f; } prov_;
    struct Reverted { float log_lum; double t; };
    std::deque<Reverted> reverted_;
    bool revert_stamp_ = false;
    void average_outlier(float log_lum, float log_scale, float a);
    void average_room(const RoomReading &r, float a, float a_dir);
    void outlier_room(const RoomReading &r, float a_off);
    uint64_t fast_reverts_ = 0;
    RoomReading reverted_to_;
    bool tk_gain_ = false, tk_scale_ = false, tk_tint_ = false, tk_ambient_ = false;
    bool snap_app_ = false;
    uint32_t dir_n_ = 0;
    double fast_until_ = -1e30;
    bool fast_stamp_ = false;
    float env_scale_ = 0.f;
    uint64_t applied_changes_ = 0, fast_paths_ = 0;
    FastPath fast_;
    ProbeSample last_{};
    uint64_t probes_ = 0;
    double cur_t_ = 0.0;
    float abs_lum_ = 0.f, first_lum_ = 0.f;
    LightRig cur_{};
    bool have_cur_ = false;
};
struct RoomLightStatus {
    bool room = false;
    float abs_lum = 0.f, neutral = 0.f, gain = 1.f, ambient = 0.5f;
    float dir[3] = {0.f, 1.f, 0.f}, tint[3] = {1.f, 1.f, 1.f};
    uint64_t probes = 0;
    char reduce_error[24] = {0};
    uint8_t env = 0;
    float env_exposure = 0.f;
};
class RoomLightStatusBox {        // render thread writes, UI thread reads
public:
    void set(const RoomLightStatus &s) { std::lock_guard<std::mutex> g(m_); s_ = s; seen_ = true; }
    RoomLightStatus get() const { std::lock_guard<std::mutex> g(m_); return s_; }
    bool seen() const { std::lock_guard<std::mutex> g(m_); return seen_; }
private:
    mutable std::mutex m_;
    RoomLightStatus s_;
    bool seen_ = false;
};
std::string room_light_line(const RoomLightStatus &s);
}

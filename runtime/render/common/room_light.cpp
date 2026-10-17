#include "room_light.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
extern "C" {
#include "ar_math.h"
}
namespace ar {
namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;
const float kWarm[3] = {1.00f, 0.93f, 0.82f}, kCool[3] = {0.86f, 0.94f, 1.00f};
float lum709(const float t[3]) { return 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2]; }
}
LightRig fixed_light_rig(const LookSettings &s) {
    LightRig r;
    const double az = s.key_az_deg * kDeg, el = s.key_el_deg * kDeg;
    r.dir[0] = (float)(std::cos(el) * std::sin(az));
    r.dir[1] = (float)std::sin(el);
    r.dir[2] = (float)(std::cos(el) * std::cos(az));
    const float w = std::fabs(s.warmth);
    const float *to = s.warmth >= 0.f ? kWarm : kCool;
    for (int k = 0; k < 3; k++) r.tint[k] = 1.f + (to[k] - 1.f) * w;
    const float l = lum709(r.tint);
    for (float &c : r.tint) c /= l;
    r.gain = s.key_power;
    r.ambient = 1.f - std::clamp(s.directional, 0.05f, 0.9f);
    r.from_room = false;
    return r;
}
LightingFrame lighting_frame(const LookSettings &s, const LightRig &r) {
    LightingFrame f;
    f.on = s.roomlit;
    for (int k = 0; k < 3; k++) { f.dir[k] = r.dir[k]; f.tint[k] = r.tint[k] * r.gain; }
    f.ambient = r.ambient;
    f.contact = s.contact; f.contact_strength = s.contact_strength;
    f.shadows = s.shadows;
    f.hide_fake_shadows = s.shadows != 0 && s.blob == 1;
    f.softness = s.softness;
    f.relight = s.relight;
    f.relight_strength = s.relight_strength;
    f.ao = s.ao;
    f.ao_strength = s.ao_strength;
    f.table_cast = s.shadows != 0 ? s.table_cast : 0.f;
    f.toy = s.toy; f.gloss = s.gloss; f.rim = s.rim; f.reflections = s.reflections;
    return f;
}
bool KeyLightSender::changed(bool on, const float dir[3]) {
    const float dot = dir_[0] * dir[0] + dir_[1] * dir[1] + dir_[2] * dir[2];
    if (sent_ && on == on_ && (!on || dot > 0.99996f)) return false;
    sent_ = true; on_ = on;
    for (int k = 0; k < 3; k++) dir_[k] = dir[k];
    return true;
}
namespace {
float lumP3(const float c[3]) { return 0.2290f * c[0] + 0.6917f * c[1] + 0.0793f * c[2]; }   // Display P3 luminance row
void p3_to_srgb(const float p[3], float s[3]) {                                              // linear, D65
    s[0] = 1.2249f * p[0] - 0.2247f * p[1];
    s[1] = -0.0420f * p[0] + 1.0419f * p[1];
    s[2] = -0.0197f * p[0] - 0.0786f * p[1] + 1.0979f * p[2];
}
bool finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
constexpr float kMinAbsLum = 1e-4f;
}
void table_from_probe_rotation(ar_pose tracking_from_table, ar_pose tracking_from_probe, float out[9]) {
    const ar_pose t = ar_pose_mul(ar_pose_inverse(tracking_from_table), tracking_from_probe);
    const float ax[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int c = 0; c < 3; c++) {
        float col[3];
        ar_quat_rotate(t.quat, ax[c], col);
        for (int r = 0; r < 3; r++) out[r * 3 + c] = col[r];
    }
}
void cube_face_dir(int face, float u, float v, float o[3]) {
    switch (face) {
        case 0: o[0] = 1.f; o[1] = -v; o[2] = -u; break;
        case 1: o[0] = -1.f; o[1] = -v; o[2] = u; break;
        case 2: o[0] = u; o[1] = 1.f; o[2] = v; break;
        case 3: o[0] = u; o[1] = -1.f; o[2] = -v; break;
        case 4: o[0] = u; o[1] = -v; o[2] = 1.f; break;
        default: o[0] = -u; o[1] = -v; o[2] = -1.f; break;
    }
}
std::vector<float> fake_probe_texels(uint32_t size, const float dir[3], const float rgb[3], float ambient) {
    std::vector<float> t((size_t)6 * size * size * 4);
    const float cos_disc = std::cos(18.f * 3.14159265f / 180.f);
    size_t i = 0;
    for (int f = 0; f < 6; f++)
        for (uint32_t y = 0; y < size; y++)
            for (uint32_t x = 0; x < size; x++) {
                float d[3];
                cube_face_dir(f, (x + 0.5f) / size * 2.f - 1.f, (y + 0.5f) / size * 2.f - 1.f, d);
                const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                const bool disc = (d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) / n > cos_disc;
                for (int k = 0; k < 3; k++) t[i++] = ambient + (disc ? 20.f * rgb[k] : 0.f);
                t[i++] = 1.f;
            }
    return t;
}
void RoomLightEstimator::add(const ProbeSample &p, double t_s) {
    if (!finite3(p.l0) || !finite3(p.l1) || !std::isfinite(p.scale_ref) || !(p.scale_ref > 0.f)) return;
    const float abs_lum = lumP3(p.l0) * p.scale_ref;
    if (!(abs_lum > kMinAbsLum)) return;
    last_ = p; abs_lum_ = abs_lum; probes_++;
    if (first_lum_ == 0.f) first_lum_ = abs_lum;
    // the steady estimate (kept whatever the response, so a live switch to Steady has the room's history)
    const RoomReading r = reading_of(p);
    if (t_s < 0.0) stamp_ = true;
    const float x = std::log(r.lum), xs = std::log(r.scale), big = std::log(kFastRatio);
    const float a_outlier = 1.f - (float)std::exp(-kSteadyNominalGapS / SM64_LIGHT_TAU_S);
    // A lone fast path is provisional: a probe back within x1.8 of the estimate before it, within 30 s of the snap,
    // undoes it (fast ease); the outlier's colour, directionality and key never counted, its luminance as one outlier
    // probe (capped).
    if (prov_.on) {
        prov_.on = false;
        const bool in_time = t_s < 0.0 || prov_.t < 0.0 || t_s - prov_.t <= kProvisionalS;
        if (in_time && std::fabs(x - std::log(prov_.pre_lum)) <= big) {
            est_.lum = prov_.pre_lum; est_.scale = prov_.pre_scale;
            average_outlier(prov_.log_lum, prov_.log_scale, a_outlier);
            app_.lum = est_.lum; app_.scale = est_.scale;
            if (t_s >= 0.0) fast_until_ = t_s + kFastEaseS;
            else revert_stamp_ = true;
            if (!recent_.empty()) recent_.pop_back();
            snap_app_ = true;
            off_.on = false;
            reverted_.push_back(Reverted{prov_.log_lum, t_s});
            while (reverted_.size() > 8) reverted_.pop_front();
            reverted_to_ = est_;
            fast_reverts_++;
            applied_changes_++;
        } else {
            if (prov_.a > 0.f) outlier_room(prov_.room, prov_.a);
        }
    }
    bool lone_ok = true;
    for (const float q : recent_) lone_ok = lone_ok && std::fabs(x - q) > big;
    recent_.push_back(x);
    while (recent_.size() > 3) recent_.pop_front();
    if (probes_ == 1) { est_ = r; held_ = Held{}; last_add_t_ = t_s; dir_n_ = r.has_dir ? 1 : 0; return; }
    const double gap = t_s >= 0.0 && last_add_t_ >= 0.0 ? std::clamp(t_s - last_add_t_, (double)kSteadyGapMinS, (double)kSteadyGapMaxS)
                                                       : (double)kSteadyNominalGapS;
    if (t_s >= 0.0) last_add_t_ = t_s;
    const float a = 1.f - (float)std::exp(-gap / SM64_LIGHT_TAU_S);
    const float a_off = std::min(a, a_outlier);
    // a probe more than x1.8 off the estimate (an outlier until shown otherwise) moves colour,
// directionality and key only once it is decided, and then by at most the outlier share (a reverted one: not at all)
    bool room_pending = std::fabs(x - std::log(est_.lum)) > big;
    if (!room_pending) average_room(r, a, std::max(std::min(a, kSteadyKeyShare), 1.f / (float)(dir_n_ + 1)));
    auto room_now = [&] { if (room_pending) { outlier_room(r, a_off); room_pending = false; } };
    // the luminance: a probe more than x1.8 off the estimate is held until the next probe (or 4 s) decides
    if (!held_.on && off_.on) {
        const float dr = x - off_.ref;
        const bool agree = std::fabs(dr) > big && (dr > 0.f ? 1 : -1) == off_.sign && std::fabs(x - off_.log_lum) < std::log(kFastAgreeRatio);
        if (agree) { room_now(); fire(0.5f * (off_.log_lum + x), 0.5f * (off_.log_scale + xs), t_s, 2); return; }
    }
    float d = x - std::log(est_.lum);
    if (held_.on) {
        if (held_.room_pending) outlier_room(held_.room, held_.a);
        if (std::fabs(d) > big && (d > 0.f ? 1 : -1) == held_.sign) {
            room_now();
            fire(0.5f * (held_.log_lum + x), 0.5f * (held_.log_scale + xs), t_s, 2);
            return;
        }
        average_outlier(held_.log_lum, held_.log_scale, held_.a);
        held_.on = false;
        d = x - std::log(est_.lum);
    }
    if (std::fabs(d) > big) {
        off_ = Off{true, d > 0.f ? 1 : -1, x, xs, std::log(est_.lum)};
        // within x1.3 of an outlier reverted in the last 60 s: a recurring outlier, never a lone fast path
// (a match refreshes the remembered outlier's time)
        bool recurring = false;
        for (Reverted &q : reverted_)
            if ((t_s < 0.0 || q.t < 0.0 || t_s - q.t <= kRecurringS) && std::fabs(x - q.log_lum) < std::log(kFastAgreeRatio)) {
                recurring = true;
                q.t = t_s;
                if (t_s < 0.0) stamp_ = true;
            }
        if (recurring) { room_now(); average_outlier(x, xs, a_off); return; }
        held_ = Held{true, lone_ok, x, xs, a_off, d > 0.f ? 1 : -1, t_s, r, room_pending};
        return;
    }
    room_now();
    off_.on = false;
    average_lum(x, xs, a);
}
void RoomLightEstimator::outlier_room(const RoomReading &r, float a_off) {
    average_room(r, 0.5f * a_off, std::min(a_off, kSteadyKeyShare));
}
void RoomLightEstimator::average_room(const RoomReading &r, float a, float a_dir) {
    for (int k = 0; k < 3; k++) est_.col[k] += (r.col[k] - est_.col[k]) * a;
    est_.D += (r.D - est_.D) * a;
    if (!r.has_dir) return;
    dir_n_++;
    if (!est_.has_dir) { for (int k = 0; k < 3; k++) est_.dir[k] = r.dir[k]; est_.has_dir = true; return; }
    float v[3];
    for (int k = 0; k < 3; k++) v[k] = est_.dir[k] * (1.f - a_dir) + r.dir[k] * a_dir;
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (int k = 0; k < 3; k++) est_.dir[k] = v[k] / l;
}
void RoomLightEstimator::average_outlier(float log_lum, float log_scale, float a) {
    const float cap = std::log(kOutlierCapRatio), e = std::log(est_.lum), es = std::log(est_.scale);
    average_lum(std::clamp(log_lum, e - cap, e + cap), std::clamp(log_scale, es - cap, es + cap), a);
}
void RoomLightEstimator::average_lum(float log_lum, float log_scale, float a) {
    est_.lum = std::exp(std::log(est_.lum) + (log_lum - std::log(est_.lum)) * a);
    est_.scale = std::exp(std::log(est_.scale) + (log_scale - std::log(est_.scale)) * a);
}
void RoomLightEstimator::fire(float log_lum, float log_scale, double t, int probes) {
    fast_ = FastPath{t, est_.lum, std::exp(log_lum), probes};
    est_.lum = std::exp(log_lum);
    est_.scale = std::exp(log_scale);
    app_.lum = est_.lum;
    app_.scale = est_.scale;
    held_.on = false;
    off_.on = false;
    if (t >= 0.0) fast_until_ = t + kFastEaseS;
    else fast_stamp_ = true;
    fast_paths_++;
    applied_changes_++;
}
float room_light_ease(double dt_s) { return dt_s > 0.0 ? 1.f - (float)std::exp(-dt_s / kRoomLightTauS) : 0.f; }
float light_ease(double dt_s, float tau_s) { return dt_s > 0.0 ? 1.f - (float)std::exp(-dt_s / tau_s) : 0.f; }
RoomReading RoomLightEstimator::reading_of(const ProbeSample &p) const {
    RoomReading r;
    r.lum = lumP3(p.l0) * p.scale_ref;
    r.scale = p.scale_ref;
    float c[3];
    p3_to_srgb(p.l0, c);
    const float cl = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    for (int k = 0; k < 3; k++) r.col[k] = std::clamp(cl > 0.f ? c[k] / cl : 1.f, 0.6f, 1.4f);
    const float l1n = std::sqrt(p.l1[0] * p.l1[0] + p.l1[1] * p.l1[1] + p.l1[2] * p.l1[2]);
    r.D = l1n / std::max(lumP3(p.l0), 1e-9f);
    r.has_dir = r.D >= 0.05f;
    if (r.has_dir) {
        const float *m = p.table_from_probe;
        float d[3] = {m[0] * p.l1[0] + m[1] * p.l1[1] + m[2] * p.l1[2], m[3] * p.l1[0] + m[4] * p.l1[1] + m[5] * p.l1[2],
                      m[6] * p.l1[0] + m[7] * p.l1[1] + m[8] * p.l1[2]};
        d[1] = std::fabs(d[1]);                                       // a light under the table: mirrored above it
        const float h = std::sqrt(d[0] * d[0] + d[2] * d[2]), len = std::sqrt(h * h + d[1] * d[1]);
        const float min_el = std::sin(10.f * 3.14159265f / 180.f);
        if (d[1] / len < min_el && h > 0.f) {
            const float c10 = std::cos(10.f * 3.14159265f / 180.f);
            d[0] = d[0] / h * c10; d[2] = d[2] / h * c10; d[1] = min_el;
        }
        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int k = 0; k < 3; k++) r.dir[k] = d[k] / dl;
    }
    return r;
}
LightRig RoomLightEstimator::rig_of(const RoomReading &p, const LookSettings &s) const {
    LightRig r = fixed_light_rig(s);
    for (int k = 0; k < 3; k++) r.tint[k] = std::pow(1.f + (p.col[k] - 1.f) * s.match_colour, 1.f / 2.2f);
    const float tl = 0.2126f * r.tint[0] + 0.7152f * r.tint[1] + 0.0722f * r.tint[2];
    for (float &k : r.tint) k /= tl;
    const float n = neutral(s);
    const float g = n > 0.f ? std::clamp(std::pow(p.lum / n, 0.4f), 0.45f, 1.25f) : 1.f;
    r.gain = (1.f + (g - 1.f) * s.match_brightness) * s.key_power;
    const float K = std::clamp(1.2f * p.D, 0.1f, 0.8f);
    r.ambient = 1.f - K;
    if (p.has_dir) for (int k = 0; k < 3; k++) r.dir[k] = p.dir[k];
    r.from_room = true;
    r.directionality = p.D;
    return r;
}
LightRig RoomLightEstimator::target(const LookSettings &s) const {
    RoomReading r = reading_of(last_);
    r.lum = abs_lum_;
    return rig_of(r, s);
}
namespace {
void turn_towards(float d[3], const float to[3], float a, double max_rad) {
    const double c = std::clamp((double)d[0] * to[0] + (double)d[1] * to[1] + (double)d[2] * to[2], -1.0, 1.0);
    const double th = std::acos(c);
    if (th < 1e-6) { for (int k = 0; k < 3; k++) d[k] = to[k]; return; }
    double p[3];
    for (int k = 0; k < 3; k++) p[k] = to[k] - c * d[k];
    double pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (pl < 1e-9) {
        const double ax[3] = {std::fabs(d[0]) < 0.9f ? 1.0 : 0.0, std::fabs(d[0]) < 0.9f ? 0.0 : 1.0, 0.0};
        p[0] = ax[1] * d[2] - ax[2] * d[1]; p[1] = ax[2] * d[0] - ax[0] * d[2]; p[2] = ax[0] * d[1] - ax[1] * d[0];
        pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    }
    const double s = std::min(th * a, max_rad), cs = std::cos(s), sn = std::sin(s);
    double o[3], ol = 0.0;
    for (int k = 0; k < 3; k++) { o[k] = d[k] * cs + p[k] / pl * sn; ol += o[k] * o[k]; }
    ol = std::sqrt(ol);
    for (int k = 0; k < 3; k++) d[k] = (float)(o[k] / ol);
}
}
LightRig RoomLightEstimator::rig(const LookSettings &s, double t_s) {
    if (stamp_) {
        stamp_ = false;
        if (held_.on && held_.t < 0.0) held_.t = t_s;
        if (last_add_t_ < 0.0) last_add_t_ = t_s;
        if (fast_stamp_) { fast_stamp_ = false; fast_until_ = t_s + kFastEaseS; fast_.t = t_s; }
        if (revert_stamp_) { revert_stamp_ = false; fast_until_ = t_s + kFastEaseS; }
        for (Reverted &q : reverted_) if (q.t < 0.0) q.t = t_s;
    }
    if (held_.on && held_.t >= 0.0 && t_s - held_.t >= kFastHoldS) {
        if (held_.lone_ok) {
            prov_ = Provisional{true, est_.lum, est_.scale, held_.log_lum, held_.log_scale, t_s, held_.room,
                                held_.room_pending ? held_.a : 0.f};
            fire(held_.log_lum, held_.log_scale, t_s, 1);
            off_.on = false;
        }
        else {
            if (held_.room_pending) outlier_room(held_.room, held_.a);
            average_outlier(held_.log_lum, held_.log_scale, held_.a);
            held_.on = false;
        }
    }
    if (s.light_response == LookSettings::kLightSteady && s.source == 0 && has_probe()) return rig_steady(s, t_s);
    steady_was_ = false;
    const LightRig want = s.source == 0 && has_probe() ? target(s) : fixed_light_rig(s);
    if (!have_cur_) { cur_ = want; cur_t_ = t_s; have_cur_ = true; return cur_; }
    const float a = room_light_ease(t_s - cur_t_);
    const double prev_t = cur_t_;
    cur_t_ = t_s;
    for (int k = 0; k < 3; k++) cur_.tint[k] += (want.tint[k] - cur_.tint[k]) * a;
    const double dt = std::max(0.0, t_s - prev_t);
    const float w = want.from_room ? std::clamp(want.directionality / 0.5f, 0.2f, 1.f) : 1.f;
    turn_towards(cur_.dir, want.dir, room_light_ease(dt * w), dt * kRoomLightTurnDegS * 3.14159265358979323846 / 180.0);
    cur_.gain += (want.gain - cur_.gain) * a;
    cur_.ambient += (want.ambient - cur_.ambient) * a;
    cur_.from_room = want.from_room;
    return cur_;
}
LightRig RoomLightEstimator::rig_steady(const LookSettings &s, double t_s) {
    if (!steady_was_) {
        app_ = est_;
        env_scale_ = last_.scale_ref;
        steady_was_ = true;
        applied_changes_++;
    }
    if (snap_app_) {
        snap_app_ = false;
        app_.lum = est_.lum; app_.scale = est_.scale;
        tk_gain_ = tk_scale_ = true;
    }
    // the deadband: a change of the estimate is applied only once it is large enough. From then on the
// applied light tracks it: re-applied whenever it is a quarter of the band away, never released (the estimate is
// already low-passed; the leftover stays under a quarter band).
    const LightRig e = rig_of(est_, s), a = rig_of(app_, s);
    bool changed = false;
    auto track = [](bool &on, float diff, float b) {
        if (on) return diff >= b * kSteadyTrackFraction;
        if (diff >= b) { on = true; return true; }
        return false;
    };
    if (track(tk_gain_, std::fabs(e.gain / a.gain - 1.f), kSteadyGainBand)) {
        if (app_.lum != est_.lum) changed = true;
        app_.lum = est_.lum;
    }
    if (track(tk_scale_, std::fabs(est_.scale / app_.scale - 1.f), kSteadyGainBand)) {
        if (app_.scale != est_.scale) changed = true;
        app_.scale = est_.scale;
    }
    float dt_max = 0.f;
    for (int k = 0; k < 3; k++) dt_max = std::max(dt_max, std::fabs(e.tint[k] - a.tint[k]));
    if (track(tk_tint_, dt_max, kSteadyTintBand))
        for (int k = 0; k < 3; k++) { if (app_.col[k] != est_.col[k]) changed = true; app_.col[k] = est_.col[k]; }
    if (track(tk_ambient_, std::fabs(e.ambient - a.ambient), kSteadyAmbientBand)) {
        if (app_.D != est_.D) changed = true;
        app_.D = est_.D;
    }
    if (est_.has_dir) {
        if (!app_.has_dir) { for (int k = 0; k < 3; k++) app_.dir[k] = est_.dir[k]; app_.has_dir = true; changed = true; }
        const double c = std::clamp((double)est_.dir[0] * app_.dir[0] + (double)est_.dir[1] * app_.dir[1] + (double)est_.dir[2] * app_.dir[2], -1.0, 1.0);
        // the key keeps a plain 4 degree deadband: a residual that small does not show, and tracking it spread the key
        if ((float)(std::acos(c) / kDeg) >= kSteadyDirBandDeg) {
            for (int k = 0; k < 3; k++) { if (app_.dir[k] != est_.dir[k]) changed = true; app_.dir[k] = est_.dir[k]; }
        }
    }
    if (changed) applied_changes_++;
    const LightRig want = rig_of(app_, s);
    if (!have_cur_) { cur_ = want; cur_t_ = t_s; have_cur_ = true; env_scale_ = app_.scale; return cur_; }
    const double dt = std::max(0.0, t_s - cur_t_);
    cur_t_ = t_s;
    const float an = light_ease(dt, kSteadyEaseS / 3.f);
    const float ag = t_s < fast_until_ ? light_ease(dt, kFastEaseS / 3.f) : an;
    for (int k = 0; k < 3; k++) cur_.tint[k] += (want.tint[k] - cur_.tint[k]) * an;
    const float w = std::clamp(want.directionality / 0.5f, 0.2f, 1.f);
    turn_towards(cur_.dir, want.dir, light_ease(dt * w, kSteadyEaseS / 3.f), dt * kSteadyTurnDegS * kDeg);
    cur_.gain += (want.gain - cur_.gain) * ag;
    cur_.ambient += (want.ambient - cur_.ambient) * an;
    cur_.from_room = true;
    env_scale_ += (app_.scale - env_scale_) * ag;
    return cur_;
}
void RoomLightEstimator::turn_table(const float q[4]) {
    for (RoomReading *r : {&est_, &app_, &held_.room, &prov_.room}) {
        if (!r->has_dir) continue;
        float d[3];
        ar_quat_rotate(q, r->dir, d);
        for (int k = 0; k < 3; k++) r->dir[k] = d[k];
    }
}
void RoomLightEstimator::turn_current(const float q[4]) {
    if (!have_cur_) return;
    float d[3];
    ar_quat_rotate(q, cur_.dir, d);
    for (int k = 0; k < 3; k++) cur_.dir[k] = d[k];
}
std::string room_light_line(const RoomLightStatus &s) {
    char b[256];
    char env[24];
    if (s.env == 0) std::snprintf(env, sizeof env, "off");
    else std::snprintf(env, sizeof env, "%.4g%s", (double)s.env_exposure, s.env == 2 ? "(fallback)" : "");
    std::snprintf(b, sizeof b, "source=%s probes=%llu lum=%.4g neutral=%.4g gain=%.3f ambient=%.2f dir=%.2f,%.2f,%.2f tint=%.2f,%.2f,%.2f env=%s",
                  s.room ? "room" : "fixed", (unsigned long long)s.probes, (double)s.abs_lum, (double)s.neutral, (double)s.gain,
                  (double)s.ambient, (double)s.dir[0], (double)s.dir[1], (double)s.dir[2], (double)s.tint[0], (double)s.tint[1],
                  (double)s.tint[2], env);
    return b;
}
}

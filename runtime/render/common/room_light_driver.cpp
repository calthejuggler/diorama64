#include "room_light_driver.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
extern "C" {
#include "ar_math.h"
}
namespace ar {
LightingFrame RoomLightDriver::begin(IRenderDevice &dev, const LookSettings &look, const ar_render_frame &rf) {
    EnvStats st;
    uint32_t gen = 0;
    const bool have = dev.environment_stats(&st, &gen);
    LightingFrame lf = step(look, rf, have ? &st : nullptr, gen);
    const char *err = dev.environment_error();
    std::snprintf(status_.reduce_error, sizeof status_.reduce_error, "%s", err ? err : "");
    return lf;
}
LightingFrame RoomLightDriver::step(const LookSettings &look, const ar_render_frame &rf, const EnvStats *reduced, uint32_t gen) {
    const double t = (double)rf.display_time_ns * 1e-9;
    // the steady estimate's key is the room's, whatever the source: turned with the table before this frame's
// probe (in this frame's table axes) is averaged in
    if (have_table_) {
        ar_pose was{}, now{};
        for (int k = 0; k < 4; k++) { was.quat[k] = table_quat_[k]; now.quat[k] = rf.tracking_from_table.quat[k]; }
        const ar_pose d = ar_pose_mul(ar_pose_inverse(now), was);
        if (d.quat[0] != 0.f || d.quat[1] != 0.f || d.quat[2] != 0.f) est_.turn_table(d.quat);
    }
    if (reduced && gen != used_) {
        const EnvStats &st = *reduced;
        used_ = gen;
        for (const Pending &p : pending_) {
            if (p.generation != gen) continue;
            const uint64_t before = est_.probes();
            ProbeSample s{};
            for (int k = 0; k < 3; k++) { s.l0[k] = st.l0[k]; s.l1[k] = st.l1[k]; }
            s.scale_ref = p.scale_ref;
            table_from_probe_rotation(rf.tracking_from_table, p.tracking_from_probe, s.table_from_probe);
            est_.add(s, t);
            if (est_.probes() != before) {
                have_probe_ = true; probe_pose_ = p.tracking_from_probe;
                log_probe(look, p, t);
                adopt_cube(p, s, look, rf.tracking_from_table);
            }
            break;
        }
    }
    if (have_probe_) {
        float m[9];
        table_from_probe_rotation(rf.tracking_from_table, probe_pose_, m);
        est_.set_table_from_probe(m);
        if (have_table_ && look.source == 0) {
            ar_pose was{}, now{};
            for (int k = 0; k < 4; k++) { was.quat[k] = table_quat_[k]; now.quat[k] = rf.tracking_from_table.quat[k]; }
            const ar_pose d = ar_pose_mul(ar_pose_inverse(now), was);
            // gate on the vector part, not w: a slow turn's per-frame w rounds to 1 in float and would be lost
            if (d.quat[0] != 0.f || d.quat[1] != 0.f || d.quat[2] != 0.f) est_.turn_current(d.quat);
        }
    }
    for (int k = 0; k < 4; k++) table_quat_[k] = rf.tracking_from_table.quat[k];
    have_table_ = true;
    const LightRig rig = est_.rig(look, t);
    if (look.light_response == LookSettings::kLightSteady) log_steady(look, t);
    else { fast_logged_ = est_.fast_paths(); reverts_logged_ = est_.fast_reverts(); est_logged_changes_ = est_.applied_changes(); }
    LightingFrame lf = lighting_frame(look, rig);
    // A steering is marked sent only when there is a callback to send it to: a look set before the callback
// is registered is still sent by the first begin() after it.
    if (key_fn_ && key_.changed(lf.on, lf.dir)) key_fn_(key_ctx_, lf.on ? 1 : 0, lf.dir);
    status_.room = rig.from_room;
    status_.abs_lum = est_.abs_lum();
    status_.neutral = est_.neutral(look);
    status_.gain = rig.gain;
    status_.ambient = rig.ambient;
    for (int k = 0; k < 3; k++) { status_.dir[k] = rig.dir[k]; status_.tint[k] = rig.tint[k]; }
    status_.probes = est_.probes();
    const double dt = have_env_t_ ? std::max(0.0, t - env_t_) : 0.0;
    env_t_ = t; have_env_t_ = true;
    if (env_prev_ >= 0) {
        env_fade_ = (float)std::min(1.0, env_fade_ + dt / (look.light_response == LookSettings::kLightSteady ? kEnvFadeSteadyS : kEnvFadeS));
        if (env_fade_ >= 1.f) {
            env_prev_ = -1;
            if (env_wait_ >= 0) { env_prev_ = env_cur_; env_cur_ = env_wait_; env_wait_ = -1; env_fade_ = 0.f; }
        }
    }
    status_.env = 0;
    status_.env_exposure = 0.f;
    lf.env_mix = 0.f;
    if (env_cur_ < 0) return lf;
    env_mix_ += ((look.source == 0 ? 1.f : 0.f) - env_mix_) * room_light_ease(dt);
    if (look.source != 0 && env_mix_ < 0.004f) return lf;
    auto place = [&](const EnvCube &c, float out[9]) {
        float tp[9];
        table_from_probe_rotation(rf.tracking_from_table, c.pose, tp);
        for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) out[r * 3 + k] = tp[k * 3 + r];
    };
    bool ok = false;
    const EnvCube &cur = env_[env_cur_];
    place(cur, lf.env_from_level);
    lf.env_cube = cur.cube;
    lf.env_exposure = exposure_of(cur, look, &ok);
    lf.env_mix = env_mix_;
    lf.env_fade = 1.f;
    if (env_prev_ >= 0) {
        const EnvCube &prev = env_[env_prev_];
        bool prev_ok = false;
        place(prev, lf.env_prev_from_level);
        lf.env_prev_cube = prev.cube;
        lf.env_prev_exposure = exposure_of(prev, look, &prev_ok);
        lf.env_fade = env_fade_;
    }
    status_.env = ok ? 1 : 2;
    status_.env_exposure = lf.env_exposure;
    return lf;
}
float RoomLightDriver::exposure_of(const EnvCube &c, const LookSettings &look, bool *ok) const {
    const float n = est_.neutral(look) > 0.f ? est_.neutral(look) : est_.abs_lum();
    const float scale = look.light_response == LookSettings::kLightSteady && est_.env_scale() > 0.f ? est_.env_scale() : c.scale;
    *ok = n > 0.f && scale > 0.f;
    return *ok ? scale / n : 1.f;
}
void RoomLightDriver::adopt_cube(const Pending &p, const ProbeSample &s, const LookSettings &look, const ar_pose &tracking_from_table) {
    if (p.slot < 0 || !p.cube) return;
    EnvCube c{p.cube, p.tracking_from_probe, p.scale_ref};
    const RoomReading r = est_.reading(s);
    c.lum = r.lum;
    c.directional = r.has_dir;
    if (c.directional) ar_quat_rotate(tracking_from_table.quat, r.dir, c.dir_track);
    // Steady: the newest shown or waiting cube is replaced only when it is off the room's
// estimate (luminance x1.25, or the key 20 degrees) and the new cube is nearer the estimate: noisy probes do not chain
// cross-fades, a real change (the fast path's estimate) brings its cube
    const int newest = env_wait_ >= 0 ? env_wait_ : env_cur_;
    if (look.light_response == LookSettings::kLightSteady && newest >= 0 && env_[newest].lum > 0.f && c.lum > 0.f) {
        const RoomReading &e = est_.estimate();
        float e_dir[3] = {0.f, 1.f, 0.f};
        if (e.has_dir) ar_quat_rotate(tracking_from_table.quat, e.dir, e_dir);
        auto far = [&](const EnvCube &k) {
            float f = std::fabs(std::log(k.lum / e.lum)) / std::log(kEnvCubeLumRatio);
            if (k.directional && e.has_dir) {
                const float cd = std::clamp(k.dir_track[0] * e_dir[0] + k.dir_track[1] * e_dir[1] + k.dir_track[2] * e_dir[2], -1.f, 1.f);
                f = std::max(f, std::acos(cd) * 180.f / 3.14159265f / kEnvCubeDirDeg);
            }
            return f;
        };
        const float shown = far(env_[newest]);
        if (shown < 1.f || far(c) >= shown) return;
    }
    env_[p.slot] = c;
    if (env_cur_ < 0) { env_cur_ = p.slot; env_fade_ = 1.f; }
    else if (env_prev_ < 0) { env_prev_ = env_cur_; env_cur_ = p.slot; env_fade_ = 0.f; }
    else env_wait_ = p.slot;
}
void RoomLightDriver::log_steady(const LookSettings &look, double t) {
    if (!log_fn_) return;
    char b[384];
    if (est_.fast_paths() != fast_logged_) {
        fast_logged_ = est_.fast_paths();
        const RoomLightEstimator::FastPath &f = est_.last_fast_path();
        std::snprintf(b, sizeof b, "AR_LIGHT fast_path t=%.2f probes=%d from_lum=%.4g to_lum=%.4g ratio=%.3f", f.t, f.probes,
                      (double)f.from_lum, (double)f.to_lum, f.from_lum > 0.f ? (double)(f.to_lum / f.from_lum) : 0.0);
        log_fn_(log_ctx_, b);
    }
    if (est_.fast_reverts() != reverts_logged_) {
        reverts_logged_ = est_.fast_reverts();
        std::snprintf(b, sizeof b, "AR_LIGHT fast_path revert t=%.2f to_lum=%.4g", t, (double)est_.last_revert().lum);
        log_fn_(log_ctx_, b);
    }
    if (est_.applied_changes() == est_logged_changes_) return;
    if (est_logged_ && t - est_logged_t_ >= 0.0 && t - est_logged_t_ < kEstimateLogMinS) return;
    est_logged_changes_ = est_.applied_changes();
    est_logged_t_ = t; est_logged_ = true;
    const LightRig r = est_.applied_target(look);
    const float n = est_.neutral(look);
    std::snprintf(b, sizeof b,
                  "AR_LIGHT estimate t=%.2f probes=%llu lum=%.4g est_lum=%.4g gain=%.3f ambient=%.2f dir=%.2f,%.2f,%.2f "
                  "tint=%.2f,%.2f,%.2f env=%.4g",
                  t, (unsigned long long)est_.probes(), (double)est_.applied().lum, (double)est_.estimate().lum, (double)r.gain,
                  (double)r.ambient, (double)r.dir[0], (double)r.dir[1], (double)r.dir[2], (double)r.tint[0], (double)r.tint[1],
                  (double)r.tint[2], n > 0.f ? (double)(est_.applied().scale / n) : 1.0);
    log_fn_(log_ctx_, b);
}
void RoomLightDriver::log_probe(const LookSettings &look, const Pending &p, double t) {
    const double dt = probes_seen_ ? t - last_probe_t_ : 0.0;
    last_probe_t_ = t;
    probes_seen_ = true;
    if (!log_fn_) return;
    if (logged_ && t - logged_t_ >= 0.0 && t - logged_t_ < kProbeLogMinS) { log_skipped_++; return; }
    logged_t_ = t; logged_ = true;
    const LightRig r = est_.target(look);
    const float n = est_.neutral(look);
    char b[384];
    std::snprintf(b, sizeof b,
                  "AR_LIGHT_PROBE t=%.2f n=%llu skipped=%u gen=%u dt=%.2f scale=%.5g abs_lum=%.4g neutral=%.4g target_gain=%.3f "
                  "target_ambient=%.2f target_dir=%.2f,%.2f,%.2f target_env=%.4g at=%.2f,%.2f,%.2f",
                  t, (unsigned long long)est_.probes(), log_skipped_, p.generation, dt, (double)p.scale_ref, (double)est_.abs_lum(),
                  (double)n, (double)r.gain, (double)r.ambient, (double)r.dir[0], (double)r.dir[1], (double)r.dir[2],
                  n > 0.f && p.scale_ref > 0.f ? (double)(p.scale_ref / n) : 1.0, (double)p.tracking_from_probe.pos[0],
                  (double)p.tracking_from_probe.pos[1], (double)p.tracking_from_probe.pos[2]);
    log_skipped_ = 0;
    log_fn_(log_ctx_, b);
}
void RoomLightDriver::encode(IRenderDevice &dev, const ar_render_frame &rf) {
    const int slot = note_environment(rf);
    if (slot < 0) return;
    dev.encode_environment_reduce(rf.environment.cube_texture, submitted_);
    set_copied(dev.copy_environment(rf.environment.cube_texture, (uint32_t)slot));
}
int RoomLightDriver::note_environment(const ar_render_frame &rf) {
    if (!rf.has_environment || !rf.environment.cube_texture || rf.environment.generation == submitted_) return -1;
    submitted_ = rf.environment.generation;
    // the copy's slot: one neither shown nor fading out. With both in use that is the waiting probe's (or a probe's still
    // being reduced): the newer probe replaces it, so neither may adopt the overwritten slot any more.
    int slot = 0;
    while (slot == env_cur_ || slot == env_prev_) slot++;
    if (slot == env_wait_) env_wait_ = -1;
    for (Pending &q : pending_) if (q.slot == slot) q.slot = -1;
    Pending p{};
    p.generation = submitted_;
    p.scale_ref = rf.environment.camera_scale_reference;
    p.tracking_from_probe = rf.environment.tracking_from_probe;
    p.slot = slot;
    p.cube = nullptr;
    pending_.push_back(p);
    while (pending_.size() > 4) pending_.pop_front();
    return slot;
}
void RoomLightDriver::set_copied(void *cube) {
    if (!pending_.empty()) pending_.back().cube = cube;
}
}

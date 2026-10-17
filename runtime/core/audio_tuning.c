/* Double-precision maths: the game's atan2f is not libm's. */
#include "ar_audio_tuning.h"
#include <math.h>
#include "ar_math.h"
#define AR_DEG_PER_RAD 57.29577951308232
float ar_audio_head_focus_exponent(float s) {
    if (!(s > 0.f)) { return 1.f; }
    return 1.f + (AR_AUDIO_HEAD_FOCUS_MAX_EXPONENT - 1.f) * (s < 1.f ? s : 1.f);
}
static float clampf(float v, float lo, float hi, float nan_value) { return v != v ? nan_value : v < lo ? lo : v > hi ? hi : v; }
void ar_audio_tuning_clamp(ar_audio_tuning *t) {
    const float e = t->distance_exponent;
    t->distance_exponent = !(e >= 1.f) ? 1.f : e > 3.f ? 3.f : e;
    t->spotlight_floor_db = clampf(t->spotlight_floor_db, -30.f, 0.f, 0.f);
    const float c = t->spotlight_cone_deg;
    t->spotlight_cone_deg = !(c > 0.f) ? AR_AUDIO_SPOTLIGHT_CONE_DEFAULT_DEG : clampf(c, 5.f, 90.f, AR_AUDIO_SPOTLIGHT_CONE_DEFAULT_DEG);
    const float w = t->widen;
    t->widen = !(w >= 1.f) ? 1.f : w > 2.f ? 2.f : w;
    t->sfx_db = clampf(t->sfx_db, -12.f, 6.f, 0.f);
    t->music_db = clampf(t->music_db, -24.f, 6.f, 0.f);
}
float ar_db_to_gain(float db) { return (float)pow(10.0, (double)db / 20.0); }
float ar_spotlight_gain(float a, float cone, float floor_db) {
    if (!(floor_db < 0.f) || !(a > cone)) { return 1.f; }
    const float f = (a - cone) / AR_AUDIO_SPOTLIGHT_RAMP_DEG;
    return ar_db_to_gain(floor_db * (f < 1.f ? f : 1.f));
}
float ar_widen_azimuth_deg(float a, float f) {
    if (!(f > 1.f) || !(fabsf(a) < 180.f)) { return a; }
    return (float)(2.0 * atan((double)f * tan((double)a / (2.0 * AR_DEG_PER_RAD))) * AR_DEG_PER_RAD);
}
static void head_forward(const ar_pose *head, float fwd[3]) {
    const float mz[3] = { 0.f, 0.f, -1.f };
    ar_quat_rotate(head->quat, mz, fwd);
}
void ar_audio_widen(const ar_pose *head, const float src[3], float f, float out[3]) {
    out[0] = src[0]; out[1] = src[1]; out[2] = src[2];
    if (!(f > 1.f)) { return; }
    float fwd[3];
    head_forward(head, fwd);
    const double fl = sqrt((double)fwd[0] * fwd[0] + (double)fwd[2] * fwd[2]);
    double k = (fl - AR_AUDIO_WIDEN_FADE_LO) / (AR_AUDIO_WIDEN_FADE_HI - AR_AUDIO_WIDEN_FADE_LO);
    k = !(k > 0.0) ? 0.0 : k > 1.0 ? 1.0 : k;
    const double fe = 1.0 + ((double)f - 1.0) * k * k * (3.0 - 2.0 * k);
    const double vx = (double)src[0] - head->pos[0], vz = (double)src[2] - head->pos[2], vl = sqrt(vx * vx + vz * vz);
    if (!(fe > 1.0) || !(vl > 1e-6)) { return; }
    const double fx = fwd[0] / fl, fz = fwd[2] / fl, rx = -fz, rz = fx;
    const double a = atan2(vx * rx + vz * rz, vx * fx + vz * fz) * AR_DEG_PER_RAD;
    const double w = (double)ar_widen_azimuth_deg((float)a, (float)fe) / AR_DEG_PER_RAD;
    const double s = sin(w), c = cos(w);
    out[0] = (float)(head->pos[0] + vl * (s * rx + c * fx));
    out[2] = (float)(head->pos[2] + vl * (s * rz + c * fz));
}
float ar_distance_extra_gain(float d, float e) {
    if (!(e > 1.f)) { return 1.f; }
    return (float)pow((double)ar_audio_distance_gain(d), (double)e - 1.0);
}
float ar_audio_spotlight_angle_deg(const ar_pose *head, const float p[3]) {
    const float mz[3] = { 0.f, 0.f, -1.f }, px[3] = { 1.f, 0.f, 0.f }, py[3] = { 0.f, 1.f, 0.f };
    float f[3], r[3], u[3];
    ar_quat_rotate(head->quat, mz, f);
    ar_quat_rotate(head->quat, px, r);
    ar_quat_rotate(head->quat, py, u);
    const double v[3] = { (double)p[0] - head->pos[0], (double)p[1] - head->pos[1], (double)p[2] - head->pos[2] };
    const double vf = v[0] * f[0] + v[1] * f[1] + v[2] * f[2], vr = v[0] * r[0] + v[1] * r[1] + v[2] * r[2];
    const double vu = v[0] * u[0] + v[1] * u[1] + v[2] * u[2], h = sqrt(vf * vf + vr * vr);
    if (!(h > 1e-9) && !(fabs(vu) > 1e-9)) { return 0.f; }
    const double yaw = h > 1e-9 ? atan2(vr, vf) * AR_DEG_PER_RAD : 0.0;
    const double pitch = atan2(vu, h) * AR_DEG_PER_RAD * (double)AR_AUDIO_SPOTLIGHT_PITCH_WEIGHT;
    return (float)sqrt(yaw * yaw + pitch * pitch);
}
float ar_audio_lane_gain(const ar_audio_tuning *t, const ar_pose *head, const ar_audio_source *lane, float out[3]) {
    out[0] = lane->pos[0]; out[1] = lane->pos[1]; out[2] = lane->pos[2];
    if (!(lane->flags & AR_SRC_ACTIVE)) { return 0.f; }
    const float g = lane->gain != lane->gain ? 1.f : lane->gain < 0.f ? 0.f : lane->gain;
    if (lane->flags & AR_SRC_MUSIC) { return g * ar_db_to_gain(t->music_db); }
    const float sfx = ar_db_to_gain(t->sfx_db);
    if (lane->flags & AR_SRC_FOCUS) { return g * sfx; }
    const float spot = ar_spotlight_gain(ar_audio_spotlight_angle_deg(head, lane->pos), t->spotlight_cone_deg, t->spotlight_floor_db);
    ar_audio_widen(head, lane->pos, t->widen, out);
    if (lane->flags & AR_SRC_NO_DISTANCE_LOSS) { return g * spot * sfx; }
    const float dx = out[0] - head->pos[0], dy = out[1] - head->pos[1], dz = out[2] - head->pos[2];
    return g * spot * ar_distance_extra_gain(sqrtf(dx * dx + dy * dy + dz * dz), t->distance_exponent) * sfx;
}
float ar_audio_ring_focus_gain(float d, float d_near, float strength) {
    if (!(strength > 0.f) || !isfinite(d) || !isfinite(d_near)) { return 1.f; }
    const double k = (double)AR_AUDIO_RING_FOCUS_MAX_EXPONENT * (strength < 1.f ? strength : 1.f);
    const double dn = d_near > 0.25f ? d_near : 0.25, dd = d > 0.25f ? d : 0.25;
    if (dd <= dn) { return 1.f; }
    const double g = pow(dn / dd, k), floor_g = pow(10.0, (double)AR_AUDIO_RING_FOCUS_FLOOR_DB / 20.0);
    return (float)(g > floor_g ? g : floor_g);
}
float ar_audio_glide(float y, float target, float dt_s, float tau_s) {
    if (y != y) { return target; }
    if (!isfinite(target) || !(dt_s > 0.f)) { return y; }
    if (!(tau_s > 0.f)) { return target; }
    return (float)((double)y + ((double)target - y) * (1.0 - exp(-(double)dt_s / tau_s)));
}
int ar_audio_object_focus_eligible(uint32_t flags) {
    return (flags & AR_SRC_ACTIVE) && !(flags & (AR_SRC_FOCUS | AR_SRC_MUSIC | AR_SRC_NO_DISTANCE_LOSS));
}
static double object_distance(const ar_audio_source *l, const float head[3]) {
    const double dx = (double)l->pos[0] - head[0], dy = (double)l->pos[1] - head[1], dz = (double)l->pos[2] - head[2];
    const double d = sqrt(dx * dx + dy * dy + dz * dz);
    if (!isfinite(d)) { return NAN; }
    return d > AR_AUDIO_OBJECT_FOCUS_MIN_DISTANCE_M ? d : AR_AUDIO_OBJECT_FOCUS_MIN_DISTANCE_M;
}
int ar_audio_object_focus_apply(ar_audio_source *lanes, int count, const float head[3], float strength) {
    int n = 0;
    double near_d = INFINITY;
    for (int i = 0; i < count; i++) {
        if (!ar_audio_object_focus_eligible(lanes[i].flags)) { continue; }
        n++;
        const double d = object_distance(&lanes[i], head);
        if (d < near_d) { near_d = d; }
    }
    if (!(strength > 0.f) || !isfinite(head[0]) || !isfinite(head[1]) || !isfinite(head[2]) || !isfinite(near_d)) { return n; }
    for (int i = 0; i < count; i++) {
        if (!ar_audio_object_focus_eligible(lanes[i].flags)) { continue; }
        lanes[i].gain *= ar_audio_ring_focus_gain((float)object_distance(&lanes[i], head), (float)near_d, strength);
    }
    return n;
}

#include <math.h>
#include <stddef.h>
#include "sm64_eyes.h"
static int finite3(const float v[3]) { return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
const char *sm64_eyes_mode_name(int mode) {
    switch (mode) {
    case SM64_EYES_BOTH: return "both";
    case SM64_EYES_FINGER: return "finger";
    case SM64_EYES_OFF: return "off";
    default: return NULL;
    }
}
const char *sm64_eyes_follow_name(int follow) {
    switch (follow) {
    case SM64_EYES_AT_NONE: return "none";
    case SM64_EYES_AT_FINGER: return "finger";
    case SM64_EYES_AT_VIEWER: return "viewer";
    default: return NULL;
    }
}
static int counts(const sm64_hand_tips *h, const float viewer[3], const float head[3], float range_m, float below_m,
                  float *d2_out) {
    if (!h->tracked || !finite3(h->index_tip)) { return 0; }
    if (finite3(viewer) && h->index_tip[1] < viewer[1] - below_m) { return 0; }
    float d2 = 0.f;
    for (int k = 0; k < 3; k++) { const float d = h->index_tip[k] - head[k]; d2 += d * d; }
    *d2_out = d2;
    return d2 <= range_m * range_m;
}
void sm64_eyes_pick_reset(sm64_eyes_pick *p) { p->hand = -1; p->hold = 0; p->tip[0] = p->tip[1] = p->tip[2] = 0.f; }
int sm64_eyes_pick_step(sm64_eyes_pick *p, int mode, const sm64_hand_tips hands[2], const float viewer[3], const float head[3],
                        float out[3]) {
    if ((mode != SM64_EYES_BOTH && mode != SM64_EYES_FINGER) || !finite3(head)) { sm64_eyes_pick_reset(p); return SM64_EYES_AT_NONE; }
    float d2;
    if (p->hand >= 0 && hands && counts(&hands[p->hand], viewer, head, SM64_EYES_HAND_LEAVE_M, SM64_EYES_RAISED_LEAVE_M, &d2)) {
        p->hold = 0;
        for (int k = 0; k < 3; k++) { p->tip[k] = out[k] = hands[p->hand].index_tip[k]; }
        return SM64_EYES_AT_FINGER;
    }
    int best = -1; float best_d2 = INFINITY;
    for (int i = 0; hands && i < 2; i++) {
        if (counts(&hands[i], viewer, head, SM64_EYES_HAND_RANGE_M, SM64_EYES_RAISED_M, &d2) && d2 < best_d2) { best = i; best_d2 = d2; }
    }
    if (best >= 0) {
        p->hand = best; p->hold = 0;
        for (int k = 0; k < 3; k++) { p->tip[k] = out[k] = hands[best].index_tip[k]; }
        return SM64_EYES_AT_FINGER;
    }
    if (p->hand >= 0 && p->hold < SM64_EYES_HOLD_TICKS) {
        p->hold++;
        for (int k = 0; k < 3; k++) { out[k] = p->tip[k]; }
        return SM64_EYES_AT_FINGER;
    }
    p->hand = -1; p->hold = 0;
    if (mode == SM64_EYES_BOTH && finite3(viewer)) {
        for (int k = 0; k < 3; k++) { out[k] = viewer[k]; }
        return SM64_EYES_AT_VIEWER;
    }
    return SM64_EYES_AT_NONE;
}
int sm64_eyes_aim(const float target[3], const float eyes[3], float radius, float out[3]) {
    if (!finite3(target) || !finite3(eyes) || !(radius > 0.f) || !isfinite(radius)) { return 0; }
    float d[3], n = 0.f;
    for (int k = 0; k < 3; k++) { d[k] = target[k] - eyes[k]; n += d[k] * d[k]; }
    n = sqrtf(n);
    const float s = n > radius ? radius / n : 1.f;
    for (int k = 0; k < 3; k++) { out[k] = eyes[k] + d[k] * s; }
    return 1;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float sm64_eyes_angle_deg(const float target[3], const float eyes[3], const float fwd[3]) {
    if (!finite3(target) || !finite3(eyes) || !finite3(fwd)) { return NAN; }
    float d[3], nd = 0.f, nf = 0.f, dot = 0.f;
    for (int k = 0; k < 3; k++) { d[k] = target[k] - eyes[k]; nd += d[k] * d[k]; nf += fwd[k] * fwd[k]; dot -= d[k] * fwd[k]; }
    if (!(nd > 0.f) || !(nf > 0.f)) { return NAN; }
    return acosf(clampf(dot / sqrtf(nd * nf), -1.f, 1.f)) * (180.f / 3.14159265358979f);
}
int sm64_eyes_follows(int following, float angle_deg) {
    if (!isfinite(angle_deg)) { return 0; }
    return angle_deg >= (following ? SM64_EYES_REST_DEG : SM64_EYES_FOLLOW_DEG);
}
int sm64_eyes_cursor(const float mid_c[2], const float toward_c[2], float out[2]) {
    if (!isfinite(mid_c[0]) || !isfinite(mid_c[1]) || !isfinite(toward_c[0]) || !isfinite(toward_c[1])) { return 0; }
    const float dx = toward_c[0] - mid_c[0], dy0 = toward_c[1] - mid_c[1], n0 = sqrtf(dx * dx + dy0 * dy0);
    const float dy = fminf(dy0, 0.f), n = sqrtf(dx * dx + dy * dy);
    if (!(n0 > 1e-4f) || !(n >= 0.5f * n0)) { return 0; }
    out[0] = mid_c[0] + dx * (SM64_EYES_CSR_R / n); out[1] = mid_c[1] + dy * (SM64_EYES_CSR_R / n);
    return 1;
}
void sm64_eyes_clamp(float c[2]) {
    c[0] = clampf(c[0], SM64_EYES_CSR_MIN_X, SM64_EYES_CSR_MAX_X);
    c[1] = clampf(c[1], SM64_EYES_CSR_MIN_Y, SM64_EYES_CSR_MAX_Y);
}
void sm64_eyes_reset(sm64_eyes *e) { e->valid = 0; e->c[0] = e->c[1] = e->left[0] = e->left[1] = 0.f; }
void sm64_eyes_jump(sm64_eyes *e, const float at[2]) {
    if (!isfinite(at[0]) || !isfinite(at[1])) { return; }
    e->valid = 1;
    for (int k = 0; k < 2; k++) { e->c[k] = at[k]; e->left[k] = truncf(at[k]); }
}
void sm64_eyes_ease(sm64_eyes *e, const float cur[2], const float target[2], float dt_s, float out[2]) {
    out[0] = cur[0]; out[1] = cur[1];
    if (!isfinite(target[0]) || !isfinite(target[1]) || !isfinite(cur[0]) || !isfinite(cur[1]) || !(dt_s > 0.f) || !isfinite(dt_s)) {
        return;
    }
    if (!e->valid || fabsf(cur[0] - e->left[0]) > 0.5f || fabsf(cur[1] - e->left[1]) > 0.5f) {
        e->c[0] = cur[0]; e->c[1] = cur[1]; e->valid = 1;
    }
    float t[2] = { target[0], target[1] };
    sm64_eyes_clamp(t);
    const float a = 1.f - expf(-dt_s / SM64_EYES_EASE_S);
    for (int k = 0; k < 2; k++) {
        e->c[k] += (t[k] - e->c[k]) * a;
        out[k] = e->c[k];
        e->left[k] = truncf(e->c[k]);   /* what gd keeps ((s32) x) */
    }
}

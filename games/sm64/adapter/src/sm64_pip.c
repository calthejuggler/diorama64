#include "sm64_pip.h"
#include <math.h>
#include <string.h>
#include "ar_math.h"
#include "ar_float_panel.h"
int sm64_pip_wanted(int setting_on, int screen_is_level, int intro_cutscene, int shot, int dialog_open, int paused) {
    return setting_on && screen_is_level && intro_cutscene && shot >= SM64_PIP_FIRST_SHOT && shot < SM64_PIP_END_SHOT &&
           !dialog_open && !paused;
}
void sm64_pip_fade_step(sm64_pip_fade *f, int want) {
    if (f->level < 0) { f->level = 0; }
    if (f->level > SM64_PIP_FADE_TICKS) { f->level = SM64_PIP_FADE_TICKS; }
    if (want && f->level < SM64_PIP_FADE_TICKS) { f->level++; }
    else if (!want && f->level > 0) { f->level--; }
}
float sm64_pip_alpha(const sm64_pip_fade *f) {
    const int l = f->level < 0 ? 0 : f->level > SM64_PIP_FADE_TICKS ? SM64_PIP_FADE_TICKS : f->level;
    return (float)l / (float)SM64_PIP_FADE_TICKS;
}
static int all_finite(const float *m, int n) { for (int i = 0; i < n; i++) { if (!isfinite(m[i])) { return 0; } } return 1; }
static void mul_rows(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++) { s += a[i * 4 + k] * b[k * 4 + j]; }
            r[i * 4 + j] = s;
        }
    }
    memcpy(out, r, sizeof r);
}
int sm64_pip_clip_from_level(const float view[16], const float roll[16], const float proj[16], float out[16]) {
    if (!all_finite(view, 16) || !all_finite(roll, 16) || !all_finite(proj, 16)) { return 0; }
    float m[16];
    mul_rows(view, roll, m);
    mul_rows(m, proj, m);
    /* v' = v * M (row-major) is v' = M^T * v: M^T stored column-major is M's row-major array as is */
    float c[16];
    memcpy(c, m, sizeof c);
    /* GL z (-w at near, +w at far) -> reverse-Z (w at near, 0 at far): z' = (w - z) / 2, row 2 of the column-major matrix */
    for (int col = 0; col < 4; col++) { c[col * 4 + 2] = 0.5f * (m[col * 4 + 3] - m[col * 4 + 2]); }
    if (!all_finite(c, 16)) { return 0; }
    memcpy(out, c, sizeof c);
    return 1;
}
int sm64_pip_camera_pose(const float view[16], const float roll[16], float upm, float ground_y, ar_pose *out) {
    if (!all_finite(view, 16) || !all_finite(roll, 16) || !(upm > 0.f) || !isfinite(upm) || !isfinite(ground_y)) { return 0; }
    float r[3], u[3], b[3];
    for (int k = 0; k < 3; k++) { r[k] = view[k * 4 + 0]; u[k] = view[k * 4 + 1]; b[k] = view[k * 4 + 2]; }
    const float lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (!(lb > 1e-6f)) { return 0; }
    const float c = roll[0], s = roll[1];
    float rr[3], uu[3];
    for (int k = 0; k < 3; k++) { rr[k] = c * r[k] - s * u[k]; uu[k] = s * r[k] + c * u[k]; }
    const float t0 = view[12], t1 = view[13], t2 = view[14];
    float eye[3];
    for (int k = 0; k < 3; k++) { eye[k] = -(t0 * r[k] + t1 * u[k] + t2 * b[k]); }
    ar_mat4 m = ar_mat4_identity();
    for (int k = 0; k < 3; k++) {
        m.m[0 + k] = rr[k]; m.m[4 + k] = uu[k]; m.m[8 + k] = b[k];
        m.m[12 + k] = (k == 1 ? eye[k] - ground_y : eye[k]) / upm;
    }
    *out = ar_pose_from_mat4(m);
    return isfinite(out->quat[0]) && isfinite(out->quat[1]) && isfinite(out->quat[2]) && isfinite(out->quat[3]);
}
int sm64_pip_panels_overlap(const float a[3], float aw, float ah, const float b[3], float bw, float bh, float upm) {
    if (!(upm > 0.f)) { upm = 1.f; }
    const float ay0 = a[1] / upm, ay1 = ay0 + ah, by0 = b[1] / upm, by1 = by0 + bh;
    if (!(ay0 < by1 && by0 < ay1)) { return 0; }
    const float dx = (a[0] - b[0]) / upm, dz = (a[2] - b[2]) / upm;
    return sqrtf(dx * dx + dz * dz) < 0.5f * (aw + bw);
}
int sm64_pip_anchor(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], float out[3]) {
    const ar_float_panel_params p = { 1e6f, SM64_PIP_CENTRE_M - 0.5f * SM64_PIP_WIDTH_M / SM64_PIP_ASPECT, SM64_PIP_EDGE_GAP_M };
    return ar_float_panel_anchor(&p, mn, mx, ground_y, upm, head, out) == AR_FLOAT_PANEL_BESIDE;
}
int sm64_pip_hides_score(const float pip[3], const float score[3], float score_w_m, float score_h_m, float upm) {
    return sm64_pip_panels_overlap(pip, SM64_PIP_WIDTH_M, SM64_PIP_WIDTH_M / SM64_PIP_ASPECT, score, score_w_m, score_h_m, upm);
}
int sm64_pip_follow_target(const float lakitu[3], float upm, float out[3]) {
    if (!all_finite(lakitu, 3) || !isfinite(upm) || !(upm > 0.f)) { return 0; }
    out[0] = lakitu[0]; out[1] = lakitu[1] + SM64_PIP_FOLLOW_ABOVE_M * upm; out[2] = lakitu[2];
    return 1;
}
void sm64_pip_follow_reset(sm64_pip_follow *f) { f->have = 0; f->pos[0] = f->pos[1] = f->pos[2] = 0.f; }
int sm64_pip_follow_step(sm64_pip_follow *f, const float lakitu[3], float upm, float dt_s, float out[3]) {
    float t[3];
    if (sm64_pip_follow_target(lakitu, upm, t) && isfinite(dt_s) && dt_s >= 0.f) {
        const float d = sqrtf((t[0] - f->pos[0]) * (t[0] - f->pos[0]) + (t[1] - f->pos[1]) * (t[1] - f->pos[1]) +
                              (t[2] - f->pos[2]) * (t[2] - f->pos[2]));
        const float a = f->have && d <= SM64_PIP_FOLLOW_SNAP_M * upm ? 1.f - expf(-dt_s / SM64_PIP_FOLLOW_TAU_S) : 1.f;
        for (int k = 0; k < 3; k++) { f->pos[k] += (t[k] - f->pos[k]) * a; }
        f->have = 1;
    }
    if (!f->have) { return 0; }
    for (int k = 0; k < 3; k++) { out[k] = f->pos[k]; }
    return 1;
}

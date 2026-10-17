#include "sm64_ar_view.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "ar_math.h"
#include "ar_view.h"
static float sHead[3], sQuat[4];
static float sCtl[3], sCtlQuat[4];
static int sInit;
static ar_stick_state sStick;
void sm64_ar_view_reset(void) {
    const float eye[3] = { 0.f, 6400.f, 15200.f }, target[3] = { 0.f, 0.f, 0.f };
    ar_pose p = ar_pose_look_at(eye, target);
    memcpy(sHead, p.pos, sizeof sHead);
    memcpy(sQuat, p.quat, sizeof sQuat);
    memcpy(sCtl, p.pos, sizeof sCtl);
    memcpy(sCtlQuat, p.quat, sizeof sCtlQuat);
    memset(&sStick, 0, sizeof sStick);
    sInit = 1;
}
static void ensure(void) { if (!sInit) { sm64_ar_view_reset(); } }
void sm64_ar_view_set_head(const float pos[3], const float q[4]) {
    memcpy(sHead, pos, sizeof sHead);
    memcpy(sQuat, q, sizeof sQuat);
    memcpy(sCtl, pos, sizeof sCtl);
    memcpy(sCtlQuat, q, sizeof sCtlQuat);
    sInit = 1;
}
void sm64_ar_view_set_control_frame(const float pos[3], const float q[4]) {
    ensure();
    memcpy(sCtl, pos, sizeof sCtl);
    memcpy(sCtlQuat, q, sizeof sCtlQuat);
}
void sm64_ar_view_get_head(float pos[3], float q[4]) {
    ensure();
    memcpy(pos, sHead, sizeof sHead);
    memcpy(q, sQuat, sizeof sQuat);
}
static int16_t control_yaw(const float mario[3], ar_stick_state *st) {
    const ar_stick_params p = { SM64_AR_STICK_BLEND_START_DEG, SM64_AR_STICK_BLEND_END_DEG };
    float fwd[2];
    ar_stick_forward(sCtl, sCtlQuat, mario, &p, st, fwd);
    /* SM64 yaw a has (sins(a), coss(a)) = (x, z); camera->yaw points from Mario toward the viewer, i.e. -forward.
     * Plain libm atan2 (double): the game headers replace atan2f with an SM64 lookup that has swapped arguments. */
    const long a = lrint(atan2((double)-fwd[0], (double)-fwd[1]) * (32768.0 / 3.14159265358979));
    return (int16_t)(uint16_t)(a & 0xFFFF);
}
int16_t sm64_ar_control_yaw(const float mario[3]) { ensure(); return control_yaw(mario, &sStick); }
int16_t sm64_ar_control_yaw_peek(const float mario[3]) { ensure(); ar_stick_state copy = sStick; return control_yaw(mario, &copy); }
void sm64_ar_billboard(float dest[4][4], const float mtx[4][4], const float pos[3], int yaw_only) {
    ensure();
    float t[3];
    for (int k = 0; k < 3; k++) { t[k] = mtx[0][k] * pos[0] + mtx[1][k] * pos[1] + mtx[2][k] * pos[2] + mtx[3][k]; }
    const float py[3] = { 0.f, 1.f, 0.f }, mz[3] = { 0.f, 0.f, -1.f };
    float eye_up[3], eye_fwd[3], r[3], u[3], b[3];
    ar_quat_rotate(sQuat, py, eye_up);
    ar_quat_rotate(sQuat, mz, eye_fwd);
    if (yaw_only) { ar_billboard_axes_yaw(t, sHead, eye_fwd, r, u, b); }
    else { ar_billboard_axes_upright(t, sHead, eye_up, r, u, b); }
    for (int k = 0; k < 3; k++) {
        dest[0][k] = r[k];
        dest[1][k] = u[k];
        dest[2][k] = b[k];
        dest[3][k] = t[k];
    }
    dest[0][3] = 0.f; dest[1][3] = 0.f; dest[2][3] = 0.f; dest[3][3] = 1.f;
}
static float sListenerScale = SM64_AR_LISTENER_SCALE;
float sm64_ar_listener_scale(float upm) {
    return upm > 0.f && isfinite(upm) && upm != 8000.f ? SM64_AR_LISTENER_SCALE * 8000.f / upm : SM64_AR_LISTENER_SCALE;
}
void sm64_ar_view_set_units_per_meter(float upm) { sListenerScale = sm64_ar_listener_scale(upm); }
void sm64_ar_listener_vector(const float p[3], float out[3]) {
    ensure();
    ar_listener_vector(sHead, sQuat, p, sListenerScale, out);
}
static _Atomic int sBillboardStyle = SM64_BILLBOARDS_UPRIGHT;
static const char *const kBillboardStyles[] = { "full", "upright" };
void sm64_ar_set_billboard_style(int style) {
    if (style == SM64_BILLBOARDS_FULL || style == SM64_BILLBOARDS_UPRIGHT) { sBillboardStyle = style; }
}
int sm64_ar_billboard_style(void) { return sBillboardStyle; }
const char *sm64_ar_billboard_style_name(int style) { return style >= 0 && style < 2 ? kBillboardStyles[style] : NULL; }
int sm64_ar_billboard_style_parse(const char *name) {
    for (int i = 0; name && i < 2; i++) { if (strcmp(name, kBillboardStyles[i]) == 0) { return i; } }
    return -1;
}
int sm64_ar_billboard_yaw_only(int cylinder, int upright_behavior) {
    return cylinder || (upright_behavior && sBillboardStyle == SM64_BILLBOARDS_UPRIGHT);
}

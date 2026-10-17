#include "ar_float_panel.h"
#include <math.h>
#include "ar_math.h"
#include "ar_view.h"

int ar_float_panel_anchor(const ar_float_panel_params *p, const float mn[3], const float mx[3], float ground_y, float upm,
                          const float head[3], float out[3]) {
    out[0] = out[1] = out[2] = 0.f;
    int finite = isfinite(ground_y);
    for (int k = 0; k < 3; k++) { finite = finite && isfinite(mn[k]) && isfinite(mx[k]); }
    if (!finite) { return AR_FLOAT_PANEL_HIDDEN; }
    const float u = isfinite(upm) && upm > 0.f ? upm : 1.f;
    const int flat = !(mx[0] > mn[0]) || !(mx[2] > mn[2]);
    const float cx = flat ? 0.f : 0.5f * (mn[0] + mx[0]), cz = flat ? 0.f : 0.5f * (mn[2] + mx[2]);
    out[0] = cx; out[2] = cz;
    const float top = mx[1] > mn[1] && mx[1] > ground_y ? mx[1] : ground_y;
    const float y = top + p->above_top_m * u, cap = ground_y + p->max_above_ground_m * u;
    if (!(y > cap)) { out[1] = y; return AR_FLOAT_PANEL_ABOVE; }
    out[1] = cap;
    float dx = 0.f, dz = 1.f;
    if (head && isfinite(head[0]) && isfinite(head[2])) {
        const float hx = head[0] - cx, hz = head[2] - cz, l = sqrtf(hx * hx + hz * hz);
        if (l > 1e-3f * u) { dx = hx / l; dz = hz / l; }
    }
    const float ex = flat ? 0.f : 0.5f * (mx[0] - mn[0]), ez = flat ? 0.f : 0.5f * (mx[2] - mn[2]);
    const float tx = fabsf(dx) > 1e-6f ? ex / fabsf(dx) : 1e30f, tz = fabsf(dz) > 1e-6f ? ez / fabsf(dz) : 1e30f;
    const float t = (tx < tz ? tx : tz) + p->edge_gap_m * u;
    out[0] = cx + dx * t; out[2] = cz + dz * t;
    return AR_FLOAT_PANEL_BESIDE;
}

float ar_yaw_billboard_axes(const float a[3], const ar_pose *head, float right[3], float up[3], float back[3]) {
    const float mz[3] = { 0.f, 0.f, -1.f };
    float fwd[3];
    ar_quat_rotate(head->quat, mz, fwd);
    ar_billboard_axes_yaw(a, head->pos, fwd, right, up, back);
    /* double atan2: a binary that links the SM64 game gets the game's argument-swapped atan2f instead */
    return (float)atan2((double)back[0], (double)back[2]);
}

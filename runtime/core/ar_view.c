#include "ar_view.h"
#include <math.h>
#include "ar_math.h"
#include "ar_audio_distance.h"

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float a[3], const float b[3], float o[3]) {
    const float r[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    o[0] = r[0]; o[1] = r[1]; o[2] = r[2];
}
static int norm3(float v[3]) {
    const float l = sqrtf(dot3(v, v));
    if (!(l > 1e-6f)) { return 0; }
    v[0] /= l; v[1] /= l; v[2] /= l;
    return 1;
}

void ar_billboard_axes(const float obj[3], const float eye[3], const float eye_up[3],
                       float right[3], float up[3], float back[3]) {
    float vu[3] = { eye_up[0], eye_up[1], eye_up[2] };
    back[0] = eye[0] - obj[0]; back[1] = eye[1] - obj[1]; back[2] = eye[2] - obj[2];
    if (!norm3(back)) { back[0] = 0.f; back[1] = 0.f; back[2] = 1.f; }
    if (!norm3(vu)) { vu[0] = 0.f; vu[1] = 1.f; vu[2] = 0.f; }
    cross3(vu, back, right);
    if (!norm3(right)) {
        const float wy[3] = { 0.f, 1.f, 0.f };
        cross3(wy, back, right);
        if (!norm3(right)) { right[0] = 1.f; right[1] = 0.f; right[2] = 0.f; }
    }
    cross3(back, right, up);
}

static int perp_to(const float v[3], const float n[3], float out[3]) {
    const float d = dot3(v, n);
    out[0] = v[0] - d * n[0]; out[1] = v[1] - d * n[1]; out[2] = v[2] - d * n[2];
    const float l = sqrtf(dot3(out, out));
    if (!(l > 1e-3f)) { return 0; }   /* within ~0.06 degrees of n: too unstable to steer by */
    out[0] /= l; out[1] /= l; out[2] /= l;
    return 1;
}

void ar_billboard_axes_upright(const float obj[3], const float eye[3], const float eye_up[3],
                               float right[3], float up[3], float back[3]) {
    back[0] = eye[0] - obj[0]; back[1] = eye[1] - obj[1]; back[2] = eye[2] - obj[2];
    if (!norm3(back)) { back[0] = 0.f; back[1] = 0.f; back[2] = 1.f; }
    const float wy[3] = { 0.f, 1.f, 0.f }, nz[3] = { 0.f, 0.f, -1.f };
    float vu[3] = { eye_up[0], eye_up[1], eye_up[2] };
    if (!perp_to(wy, back, up) && !(norm3(vu) && perp_to(vu, back, up)) && !perp_to(nz, back, up)) {
        up[0] = 0.f; up[1] = 1.f; up[2] = 0.f;
    }
    cross3(up, back, right);
    norm3(right);
}

void ar_billboard_axes_yaw(const float obj[3], const float eye[3], const float eye_fwd[3],
                           float right[3], float up[3], float back[3]) {
    const float d[3] = { eye[0] - obj[0], eye[1] - obj[1], eye[2] - obj[2] };
    const float full = sqrtf(dot3(d, d));
    back[0] = d[0]; back[1] = 0.f; back[2] = d[2];
    const float h = sqrtf(back[0] * back[0] + back[2] * back[2]);
    if (!(h > 1e-3f * full) || !norm3(back)) {
        back[0] = -eye_fwd[0]; back[1] = 0.f; back[2] = -eye_fwd[2];
        if (!norm3(back)) { back[0] = 0.f; back[1] = 0.f; back[2] = 1.f; }
    }
    up[0] = 0.f; up[1] = 1.f; up[2] = 0.f;
    cross3(up, back, right);
}

static float smooth01(float t) {
    if (t <= 0.f) { return 0.f; }
    if (t >= 1.f) { return 1.f; }
    return t * t * (3.f - 2.f * t);
}

void ar_stick_forward(const float head[3], const float q[4], const float focus[3], const ar_stick_params *p,
                      ar_stick_state *s, float out[2]) {
    const float dx = focus[0] - head[0], dz = focus[2] - head[2], h = sqrtf(dx * dx + dz * dz);
    /* double atan2: a binary that links the SM64 game gets the game's low-precision, argument-swapped atan2f instead. */
    const float elev = (float)(atan2((double)(head[1] - focus[1]), (double)h) * 57.29577951308232);
    const float span = p->blend_end_deg - p->blend_start_deg;
    const float w_view = span > 0.f ? smooth01((elev - p->blend_start_deg) / span) : (elev >= p->blend_start_deg ? 1.f : 0.f);
    float vx = 0.f, vz = 0.f;
    if (h > 1e-4f) { vx = (1.f - w_view) * dx / h; vz = (1.f - w_view) * dz / h; }
    const float mz[3] = { 0.f, 0.f, -1.f }, py[3] = { 0.f, 1.f, 0.f };
    float f[3], u[3];
    ar_quat_rotate(q, mz, f);
    ar_quat_rotate(q, py, u);
    const float sx = f[0] + u[0], sz = f[2] + u[2], sl = sqrtf(sx * sx + sz * sz);
    if (sl > 1e-4f) { vx += w_view * sx / sl; vz += w_view * sz / sl; }
    const float l = sqrtf(vx * vx + vz * vz);
    if (l > 1e-4f) { s->fwd[0] = vx / l; s->fwd[1] = vz / l; s->valid = 1; }
    else if (!s->valid) { s->fwd[0] = 0.f; s->fwd[1] = -1.f; s->valid = 1; }
    out[0] = s->fwd[0]; out[1] = s->fwd[1];
}

void ar_listener_vector(const float head[3], const float q[4], const float src[3], float scale, float out[3]) {
    const float inv[4] = { -q[0], -q[1], -q[2], q[3] };
    const float d[3] = { (src[0] - head[0]) * scale, (src[1] - head[1]) * scale, (src[2] - head[2]) * scale };
    ar_quat_rotate(inv, d, out);
}
void ar_listener_point(const float head[3], const float q[4], const float v[3], float scale, float out[3]) {
    const float s = scale != 0.f ? 1.f / scale : 0.f;
    const float d[3] = { v[0] * s, v[1] * s, v[2] * s };
    float r[3];
    ar_quat_rotate(q, d, r);
    for (int k = 0; k < 3; k++) { out[k] = head[k] + r[k]; }
}

float ar_music_box_gain(const float h[3], const float mn[3], const float mx[3], float falloff) {
    float d2 = 0.f;
    for (int k = 0; k < 3; k++) {
        const float lo = mn[k] < mx[k] ? mn[k] : mx[k], hi = mn[k] < mx[k] ? mx[k] : mn[k];
        const float e = h[k] < lo ? lo - h[k] : (h[k] > hi ? h[k] - hi : 0.f);
        d2 += e * e;
    }
    return 1.f / (1.f + falloff * sqrtf(d2));
}

static float wrap_pi(float a) {
    while (a > 3.14159265f) { a -= 6.28318531f; }
    while (a < -3.14159265f) { a += 6.28318531f; }
    return a;
}
void ar_panel_update(ar_panel_state *s, const ar_panel_params *p, const float head[3], float half_x, float half_z,
                     float dt_s, float origin[3], float right[3], float up[3], float normal[3]) {
    /* Azimuth around the table centre; 0 = the +Z side. double atan2: the game's global atan2f differs. */
    float h = (float)atan2((double)head[0], (double)head[2]);
    if (!(h == h)) { h = 0.f; }
    const float hyst = p->hysteresis_deg * 0.0174532925f;
    if (!s->valid) { s->azimuth = s->target = h; s->valid = 1; }
    else if (fabsf(wrap_pi(h - s->target)) > hyst) {
        /* Leaning over the middle of the table makes the azimuth meaningless: hold the current edge there. */
        const float centre = 0.5f * (half_x < half_z ? half_x : half_z);
        if (hypot((double)head[0], (double)head[2]) >= (double)centre) { s->target = h; }
    }
    const float k = p->ease_s > 0.f ? 1.f - expf(-(dt_s > 0.f ? dt_s : 0.f) / p->ease_s) : 1.f;
    s->azimuth = wrap_pi(s->azimuth + wrap_pi(s->target - s->azimuth) * k);
    const float dx = sinf(s->azimuth), dz = cosf(s->azimuth);
    const float tx = fabsf(dx) > 1e-6f ? half_x / fabsf(dx) : 1e30f, tz = fabsf(dz) > 1e-6f ? half_z / fabsf(dz) : 1e30f;
    const float t = (tx < tz ? tx : tz) + p->margin_m, tilt = p->tilt_deg * 0.0174532925f;
    origin[0] = dx * t; origin[1] = p->lift_m; origin[2] = dz * t;
    right[0] = dz; right[1] = 0.f; right[2] = -dx;
    normal[0] = dx * cosf(tilt); normal[1] = sinf(tilt); normal[2] = dz * cosf(tilt);
    cross3(normal, right, up);
}
float ar_overlay_width(float width_m, float min_angle_rad, float dist_m) {
    const float w = dist_m * tanf(min_angle_rad);
    return w > width_m ? w : width_m;
}

float ar_audio_distance_gain(float d) {
    const float (*c)[2] = ar_audio_distance_curve;
    if (!(d > c[0][0])) { return c[0][1]; }
    for (int i = 1; i < AR_AUDIO_DISTANCE_POINTS; i++) {
        if (d <= c[i][0]) { return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * (d - c[i - 1][0]) / (c[i][0] - c[i - 1][0]); }
    }
    return c[AR_AUDIO_DISTANCE_POINTS - 1][1];
}

int ar_world_point_hit(const float a[3], int yaw_only, const ar_pose *head, float w_m, const float box[4], float screen_w,
                       float screen_h, const float o[3], const float d[3], float ndc[2]) {
    const float bw = box[2] - box[0], bh = box[3] - box[1];
    if (!(bw > 0.f) || !(bh > 0.f) || !(w_m > 0.f) || !(screen_w > 0.f) || !(screen_h > 0.f)) { return 0; }
    const float py[3] = { 0.f, 1.f, 0.f }, mz[3] = { 0.f, 0.f, -1.f };
    float up[3], fwd[3], r[3], u[3], b[3];
    ar_quat_rotate(head->quat, py, up);
    ar_quat_rotate(head->quat, mz, fwd);
    if (yaw_only) { ar_billboard_axes_yaw(a, head->pos, fwd, r, u, b); } else { ar_billboard_axes(a, head->pos, up, r, u, b); }
    const float h = w_m * (bh * 0.5f * screen_h) / (bw * 0.5f * screen_w), sx = w_m / bw, sy = h / bh;
    const float n[3] = { r[1] * u[2] - r[2] * u[1], r[2] * u[0] - r[0] * u[2], r[0] * u[1] - r[1] * u[0] };
    const float dn = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
    if (fabsf(dn) < 1e-9f) { return 0; }
    const float t = ((a[0] - o[0]) * n[0] + (a[1] - o[1]) * n[1] + (a[2] - o[2]) * n[2]) / dn;
    if (!(t > 0.f)) { return 0; }
    const float q[3] = { o[0] + t * d[0] - a[0], o[1] + t * d[1] - a[1], o[2] + t * d[2] - a[2] };
    const float x = (q[0] * r[0] + q[1] * r[1] + q[2] * r[2]) / sx + 0.5f * (box[0] + box[2]);
    const float y = (q[0] * u[0] + q[1] * u[1] + q[2] * u[2]) / sy + box[1];
    if (x < box[0] || x > box[2] || y < box[1] || y > box[3]) { return 0; }
    ndc[0] = x; ndc[1] = y;
    return 1;
}

#include "sm64_orchestra.h"
#include <math.h>
#include "ar_audio_tuning.h"
void sm64_orchestra_reset(sm64_orchestra *o) { o->seq_id = -1; o->mask = 0; o->layout = 0; }
int sm64_orchestra_update(sm64_orchestra *o, int seq_id, uint16_t in_use) {
    if (seq_id < 0) { seq_id = -1; }
    if (seq_id != o->seq_id) { o->seq_id = seq_id; o->mask = seq_id < 0 ? 0 : in_use; o->layout++; return 1; }
    if (seq_id < 0) { return 0; }
    const uint16_t m = (uint16_t)(o->mask | in_use);
    if (m != o->mask) { o->mask = m; o->layout++; return 1; }
    return 0;
}
void sm64_orchestra_new_layout(sm64_orchestra *o) { o->layout++; }
int sm64_orchestra_seat(const sm64_orchestra *o, int c, int *count) {
    int n = 0, seat = -1;
    for (int k = 0; k < 16; k++) {
        if (!(o->mask & (1u << k))) { continue; }
        if (k == c) { seat = n; }
        n++;
    }
    *count = n;
    return seat;
}
uint32_t sm64_orchestra_lane_id(const sm64_orchestra *o, int c) {
    return 0x80000000u | ((o->layout & 0x7FFFFFFu) << 4) | (uint32_t)(c & 15);
}
float sm64_ring_radius(const float mn[3], const float mx[3], float size) {
    const double hx = 0.5 * fabs((double)mx[0] - mn[0]), hz = 0.5 * fabs((double)mx[2] - mn[2]);
    const double s = size > 0.f ? size : 1.0;
    const double r = (sqrt(hx * hx + hz * hz) + SM64_RING_MARGIN_M) * s;
    return isfinite(r) ? (float)r : SM64_RING_MARGIN_M;
}
void sm64_ring_point(int seat, int count, float radius, float angle_deg, float cx, float cz, float out[3]) {
    out[0] = cx; out[1] = SM64_RING_HEIGHT_M; out[2] = cz;
    if (count <= 0 || seat < 0 || seat >= count || !(radius >= 0.f) || !isfinite(radius)) { return; }
    const double a0 = isfinite(angle_deg) ? angle_deg : 0.0;
    const double a = (a0 + 360.0 * seat / count) * 0.017453292519943295;
    out[0] = (float)(cx + radius * sin(a));
    out[2] = (float)(cz - radius * cos(a));
}
float sm64_ring_angle_step(float angle_deg, float rotate_dps) {
    const double a = isfinite(angle_deg) ? angle_deg : 0.0;
    const double r = rotate_dps >= 0.f && rotate_dps <= 30.f ? rotate_dps : 0.0;
    double n = fmod(a + r / 30.0, 360.0);
    if (n < 0.0) { n += 360.0; }
    return n < 360.0 ? (float)n : 0.f;
}
float sm64_ring_nearest_seat(int count, float radius, float angle_deg, float cx, float cz, float y, const float head[3]) {
    if (count <= 0 || !isfinite(head[0]) || !isfinite(head[1]) || !isfinite(head[2])) { return NAN; }
    double best = INFINITY;
    for (int k = 0; k < count; k++) {
        float p[3];
        sm64_ring_point(k, count, radius, angle_deg, cx, cz, p);
        const double dx = (double)p[0] - head[0], dy = (double)y - head[1], dz = (double)p[2] - head[2];
        const double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best) { best = d; }
    }
    return (float)best;
}
float sm64_ring_height_step(float y, int at_head, const float head[3]) {
    float target = SM64_RING_HEIGHT_M;
    if (at_head) {
        const int unknown = head[0] == 0.f && head[1] == 0.f && head[2] == 0.f;
        target = unknown || !isfinite(head[1]) ? NAN : head[1];
    }
    if (!isfinite(target)) { return y; }
    return ar_audio_glide(y, target, 1.f / 30.f, AR_AUDIO_RING_GLIDE_S);
}

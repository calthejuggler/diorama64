#include "vos_planes.h"
#include <algorithm>
#include <cmath>
#include "vos_placement.h"
namespace ar {
namespace {
void unit(const float *in, float out[3]) {
    const float l = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    for (int k = 0; k < 3; k++) out[k] = l > 0.f ? in[k] / l : 0.f;
}
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}
PlaneInfo plane_info_from_matrix(const float m[16], float width, float height, bool is_table, bool is_floor) {
    PlaneInfo p;
    float x[3], y[3], z[3];
    unit(m + 0, x); unit(m + 4, y); unit(m + 8, z);
    const bool y_up = std::fabs(y[1]) >= std::fabs(z[1]);
    const float *n = y_up ? y : z, *v = y_up ? z : y;
    const float s = n[1] < 0.f ? -1.f : 1.f;
    for (int k = 0; k < 3; k++) { p.center[k] = m[12 + k]; p.normal[k] = s * n[k]; p.u[k] = x[k]; p.v[k] = v[k]; }
    p.half_u = 0.5f * width; p.half_v = 0.5f * height;
    p.is_table = is_table;
    p.is_floor = is_floor;
    return p;
}
bool vos_floor_y(const ar_pose &head, const std::vector<PlaneInfo> &planes, float *y) {
    float best_area = -1.f;
    for (const PlaneInfo &p : planes) {
        if (!p.is_floor || p.normal[1] < 0.9f || p.center[1] > head.pos[1] - 0.5f) continue;
        const float area = p.half_u * p.half_v;
        if (area > best_area) { best_area = area; *y = p.center[1]; }
    }
    return best_area >= 0.f;
}
FallbackTable vos_fallback_table(const ar_pose &head, const PlaceParams &p, const std::vector<PlaneInfo> &planes) {
    FallbackTable f;
    f.table = vos_table_from_head(head, p);
    f.floor_known = vos_floor_y(head, planes, &f.floor_y);
    if (p.desk_height) f.table.pos[1] = vos_fallback_top_y(head.pos[1], f.floor_known, f.floor_y);
    return f;
}
float vos_fallback_top_y(float eye_y, bool floor_known, float floor_y) {
    return floor_known ? floor_y + kDeskHeightM : eye_y - (kStandingEyeHeightM - kDeskHeightM);
}
bool vos_plane_is_table_candidate(const PlaneInfo &p, bool floor_known, float floor_y) {
    if (p.normal[1] < 0.9f) return false;
    if (p.is_floor) return false;                                       // never the floor (device feedback)
    return !(floor_known && p.center[1] < floor_y + kMinTableAboveFloorM);
}
PlaneHit vos_pick_plane(const ar_pose &head, const std::vector<PlaneInfo> &planes, float max_m) {
    const float fwd_local[3] = {0.f, 0.f, -1.f};
    float d[3];
    ar_quat_rotate(head.quat, fwd_local, d);
    return vos_pick_ray(head.pos, d, planes, max_m);
}
PlaneHit vos_pick_ray(const float origin[3], const float dir[3], const std::vector<PlaneInfo> &planes, float max_m) {
    if (std::sqrt(dot(dir, dir)) < 1e-6f) return PlaneHit{};
    float d[3];
    unit(dir, d);
    const ar_pose eye{{origin[0], origin[1], origin[2]}, {0.f, 0.f, 0.f, 1.f}};
    PlaneHit best;
    float best_t = max_m, floor_y = 0.f;
    const bool floor_known = vos_floor_y(eye, planes, &floor_y);
    for (int i = 0; i < (int)planes.size(); i++) {
        const PlaneInfo &p = planes[i];
        if (!vos_plane_is_table_candidate(p, floor_known, floor_y)) continue;
        if (p.center[1] > eye.pos[1] - 0.2f) continue;
        const float denom = dot(p.normal, d);
        if (denom > -1e-4f) continue;
        const float w[3] = {p.center[0] - origin[0], p.center[1] - origin[1], p.center[2] - origin[2]};
        const float t = dot(w, p.normal) / denom;
        if (!(t > 0.f) || t > max_m) continue;
        const float q[3] = {origin[0] + d[0] * t, origin[1] + d[1] * t, origin[2] + d[2] * t};
        const float r[3] = {q[0] - p.center[0], q[1] - p.center[1], q[2] - p.center[2]};
        if (std::fabs(dot(r, p.u)) > p.half_u || std::fabs(dot(r, p.v)) > p.half_v) continue;
        const bool better = !best.hit || (p.is_table && !best.is_table) || (p.is_table == best.is_table && t < best_t);
        if (!better) continue;
        best.hit = true; best.plane = i; best.is_table = p.is_table; best_t = t;
        std::copy(q, q + 3, best.point);
    }
    return best;
}
ar_pose vos_table_at_hit(const ar_pose &head, const float point[3], float yaw_deg) {
    ar_pose t = vos_table_from_head(head, PlaceParams{0.f, 0.f, yaw_deg});
    std::copy(point, point + 3, t.pos);
    return t;
}
bool PlaneSettle::update(const PlaneHit &h, double t, bool confirm, float out[3]) {
    if (!h.hit) { have_ = false; return false; }
    const float dx = h.point[0] - p_[0], dy = h.point[1] - p_[1], dz = h.point[2] - p_[2];
    if (!have_ || std::sqrt(dx * dx + dy * dy + dz * dz) > tol_) {
        have_ = true;
        std::copy(h.point, h.point + 3, p_);
        since_ = t;
    }
    if (confirm || t - since_ >= settle_) { std::copy(p_, p_ + 3, out); return true; }
    return false;
}
}

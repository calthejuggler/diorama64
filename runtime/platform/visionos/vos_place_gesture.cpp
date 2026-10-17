#include "vos_place_gesture.h"
#include <algorithm>
#include <cmath>
namespace ar {
namespace {
float hdist(const float a[3], const float b[3]) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
bool is_pinch(const SpatialEvent &e) { return e.kind == SpatialKind::indirect_pinch || e.kind == SpatialKind::direct_pinch; }
}
Reticle vos_reticle(const float o[3], const float d[3], float head_y, const std::vector<PlaneInfo> &planes, const GestureConfig &c) {
    Reticle r;
    const PlaneHit h = vos_pick_ray(o, d, planes);
    if (h.hit) { r.valid = true; r.on_plane = true; std::copy(h.point, h.point + 3, r.point); return r; }
    const float hl = std::hypot(d[0], d[2]);
    if (!(hl > 1e-4f)) return r;
    const ar_pose from{{o[0], o[1], o[2]}, {0.f, 0.f, 0.f, 1.f}};
    r.floor_known = vos_floor_y(from, planes, &r.floor_y);
    const float y = c.desk_height ? vos_fallback_top_y(head_y, r.floor_known, r.floor_y) : head_y - c.below_m;
    float dist = c.forward_m;
    if (d[1] < -1e-4f) {
        const float meet = (y - o[1]) / d[1] * hl;
        if (meet <= c.max_dist_m) dist = std::max(c.min_dist_m, meet);
    }
    r.valid = true;
    r.point[0] = o[0] + d[0] / hl * dist; r.point[1] = y; r.point[2] = o[2] + d[2] / hl * dist;
    return r;
}
PlaceGesture::PlaceGesture(const GestureConfig &c) : c_(c) {}
void PlaceGesture::drop_active() {
    if (have_active_) { held_id_ = active_id_; have_held_ = true; }
    have_active_ = false; moved_ = false;
}
void PlaceGesture::seek() { drop_active(); state_ = GestureState::seeking; }
void PlaceGesture::placed(const ar_pose &t) { drop_active(); table_ = t; state_ = GestureState::placed; }
GestureState PlaceGesture::state() const { return state_; }
const ar_pose &PlaceGesture::table() const { return table_; }
GestureResult PlaceGesture::on_event(const SpatialEvent &e, const ar_pose &head, const std::vector<PlaneInfo> &planes) {
    GestureResult out;
    out.table = table_;
    if (!is_pinch(e)) return out;
    const bool ends = e.phase != SpatialPhase::active;
    if (have_held_ && e.id == held_id_) {
        if (ends) have_held_ = false;
        return out;
    }
    if (have_active_ && e.id != active_id_) return out;
    if (!have_active_) {
        if (ends) return out;
        have_active_ = true; active_id_ = e.id; moved_ = false;
        if (state_ == GestureState::seeking) {
            float o[3], d[3];
            if (e.has_ray) { std::copy(e.ray_o, e.ray_o + 3, o); std::copy(e.ray_d, e.ray_d + 3, d); }
            else { const float mz[3] = {0.f, 0.f, -1.f}; std::copy(head.pos, head.pos + 3, o); ar_quat_rotate(head.quat, mz, d); }
            const Reticle r = vos_reticle(o, d, head.pos[1], planes, c_);
            if (!r.valid) return out;
            table_ = vos_table_at_hit(head, r.point, c_.yaw_deg);
            state_ = GestureState::placed;
            out.action = GestureAction::place; out.table = table_; out.on_plane = r.on_plane;
            out.floor_known = r.floor_known; out.floor_y = r.floor_y;
            return out;
        }
        if (state_ == GestureState::placed && e.has_pose) {
            start_ = table_;
            std::copy(e.pose_pos, e.pose_pos + 3, hand0_);
            const float hh = hdist(hand0_, head.pos);
            gain_ = hh > 0.05f ? std::min(c_.max_gain, std::max(1.f, hdist(table_.pos, head.pos) / hh)) : 1.f;
            state_ = GestureState::dragging;
        }
        return out;
    }
    if (state_ != GestureState::dragging) {
        if (ends) have_active_ = false;
        return out;
    }
    if (e.phase == SpatialPhase::cancelled) {
        have_active_ = false; state_ = GestureState::placed;
        const bool was = moved_; moved_ = false;
        table_ = start_; out.table = table_;
        out.action = was ? GestureAction::restore : GestureAction::none;
        return out;
    }
    if (e.has_pose) {
        const float dx = e.pose_pos[0] - hand0_[0], dz = e.pose_pos[2] - hand0_[2];
        if (moved_ || std::hypot(dx, dz) >= c_.dead_zone_m) {
            ar_pose t = start_;
            t.pos[0] = start_.pos[0] + gain_ * dx; t.pos[2] = start_.pos[2] + gain_ * dz;
            float floor_y = 0.f, best = c_.snap_m + 1.f;
            const bool floor_known = vos_floor_y(head, planes, &floor_y);
            for (const PlaneInfo &p : planes) {
                if (!vos_plane_is_table_candidate(p, floor_known, floor_y)) continue;
                const float r[3] = {t.pos[0] - p.center[0], 0.f, t.pos[2] - p.center[2]};
                if (std::fabs(r[0] * p.u[0] + r[2] * p.u[2]) > p.half_u || std::fabs(r[0] * p.v[0] + r[2] * p.v[2]) > p.half_v) continue;
                const float dy = std::fabs(p.center[1] - start_.pos[1]);
                if (dy <= c_.snap_m && dy < best) { best = dy; t.pos[1] = p.center[1]; }
            }
            if (!moved_ || t.pos[0] != table_.pos[0] || t.pos[1] != table_.pos[1] || t.pos[2] != table_.pos[2]) {
                moved_ = true; table_ = t; out.action = GestureAction::move; out.table = t;
            }
        }
    }
    if (e.phase == SpatialPhase::ended) {
        have_active_ = false; state_ = GestureState::placed;
        out.action = moved_ ? GestureAction::commit : GestureAction::none; out.table = table_;
        moved_ = false;
    }
    return out;
}
}

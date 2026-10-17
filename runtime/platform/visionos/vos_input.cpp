#include "vos_input.h"
#include <algorithm>
#include <cmath>
namespace ar {
void SpatialEventQueue::push(const SpatialEvent &e) {
    std::lock_guard<std::mutex> g(m_);
    if (q_.size() >= kCap) { q_.pop_front(); dropped_++; }
    q_.push_back(e);
}
size_t SpatialEventQueue::drain(std::vector<SpatialEvent> *out) {
    std::lock_guard<std::mutex> g(m_);
    const size_t n = q_.size();
    out->insert(out->end(), q_.begin(), q_.end());
    q_.clear();
    return n;
}
uint64_t SpatialEventQueue::dropped() const { std::lock_guard<std::mutex> g(m_); return dropped_; }
GestureContinuity::Result GestureContinuity::filter(std::vector<SpatialEvent> *events, uint64_t dropped_total, bool discard) {
    Result r;
    std::vector<uint64_t> ended, held;
    if (discard) {
        r.discarded = events->size();
        for (const SpatialEvent &e : *events) {
            std::vector<uint64_t> &to = e.phase == SpatialPhase::active ? held : ended, &from = e.phase == SpatialPhase::active ? ended : held;
            from.erase(std::remove(from.begin(), from.end(), e.id), from.end());
            if (std::find(to.begin(), to.end(), e.id) == to.end()) to.push_back(e.id);
        }
        events->clear();
    }
    std::vector<SpatialEvent> out;
    if (discard || dropped_total != dropped_seen_) {
        for (SpatialEvent c : open_) {
            c.phase = SpatialPhase::cancelled;
            out.push_back(c);
            if (std::find(ended.begin(), ended.end(), c.id) == ended.end() && std::find(held.begin(), held.end(), c.id) == held.end())
                ignored_.push_back(c.id);
            r.cancelled++;
        }
        open_.clear();
    }
    for (uint64_t id : held)
        if (std::find(ignored_.begin(), ignored_.end(), id) == ignored_.end()) ignored_.push_back(id);
    if (ignored_.size() > kMaxIgnored) ignored_.erase(ignored_.begin(), ignored_.end() - kMaxIgnored);
    dropped_seen_ = dropped_total;
    for (const SpatialEvent &e : *events) {
        const bool ends = e.phase != SpatialPhase::active;
        auto ig = std::find(ignored_.begin(), ignored_.end(), e.id);
        if (ig != ignored_.end()) { if (ends) ignored_.erase(ig); continue; }
        auto op = std::find_if(open_.begin(), open_.end(), [&](const SpatialEvent &o) { return o.id == e.id; });
        if (ends) { if (op != open_.end()) open_.erase(op); }
        else if (op != open_.end()) *op = e;
        else open_.push_back(e);
        out.push_back(e);
    }
    events->swap(out);
    return r;
}
std::vector<SpatialEvent> scripted_pinch(double t0, const ar_pose &head, float dx, float dz) {
    const float mz[3] = {0.f, 0.f, -1.f};
    float fwd[3];
    ar_quat_rotate(head.quat, mz, fwd);
    float hand0[3] = {head.pos[0] + 0.35f * fwd[0], head.pos[1] - 0.3f, head.pos[2] + 0.35f * fwd[2]};
    std::vector<SpatialEvent> v;
    for (int k = 0; k <= 30; k++) {
        SpatialEvent e;
        e.id = 0x5C21u; e.kind = SpatialKind::indirect_pinch; e.phase = SpatialPhase::active; e.t_s = t0 + k / 30.0;
        e.has_ray = true;
        for (int i = 0; i < 3; i++) { e.ray_o[i] = head.pos[i]; e.ray_d[i] = fwd[i]; }
        e.has_pose = true;
        const float f = k / 30.f;
        e.pose_pos[0] = hand0[0] + dx * f; e.pose_pos[1] = hand0[1]; e.pose_pos[2] = hand0[2] + dz * f;
        e.chirality = 2;
        v.push_back(e);
    }
    SpatialEvent end = v.back();
    end.phase = SpatialPhase::ended; end.t_s = t0 + 1.0;
    v.push_back(end);
    return v;
}
size_t PointerRouter::route(std::vector<SpatialEvent> *events, bool placed, const ar_pose &tracking_from_table,
                            const ar_pose &head_tracking, const PointerClaim &claim, size_t *edges, bool pose_only) {
    if (!claim) return 0;
    const ar_pose table_from_tracking = ar_pose_inverse(tracking_from_table);
    const ar_pose head = ar_pose_mul(table_from_tracking, head_tracking);
    auto has = [](const std::vector<uint64_t> &v, uint64_t id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    auto closes = [](const SpatialEvent &e) { return e.phase == SpatialPhase::ended || e.phase == SpatialPhase::cancelled; };
    auto forget = [](std::vector<uint64_t> &v, uint64_t id) { auto it = std::find(v.begin(), v.end(), id); if (it != v.end()) v.erase(it); };
    // A placer gesture is remembered while it is open (until its end or cancel), never evicted by newer gestures.
    auto leave = [&](const SpatialEvent &e) {
        if (closes(e)) { forget(offered_, e.id); return; }
        if (!has(offered_, e.id)) { offered_.push_back(e.id); if (offered_.size() > kMaxOpen) offered_.erase(offered_.begin()); }
    };
    size_t routed = 0;
    std::vector<SpatialEvent> keep;
    for (const SpatialEvent &e : *events) {
        const bool mine = has(claimed_, e.id);
        if (!mine && has(offered_, e.id)) { leave(e); keep.push_back(e); continue; }
        if (!mine && (!placed || !(e.has_ray || (pose_only && e.has_pose)) || e.phase != SpatialPhase::active)) {
            leave(e);
            keep.push_back(e);
            continue;
        }
        PointerEvent p;
        p.id = e.id;
        p.phase = !mine ? PointerEvent::begin : e.phase == SpatialPhase::ended ? PointerEvent::end
                : e.phase == SpatialPhase::cancelled ? PointerEvent::cancel : PointerEvent::move;
        float o[3], d[3], h[3];
        const ar_pose ray_pose{{e.ray_o[0], e.ray_o[1], e.ray_o[2]}, {0.f, 0.f, 0.f, 1.f}};
        const ar_pose ro = ar_pose_mul(table_from_tracking, ray_pose);
        for (int k = 0; k < 3; k++) o[k] = ro.pos[k];
        ar_quat_rotate(table_from_tracking.quat, e.ray_d, d);
        for (int k = 0; k < 3; k++) { p.ray_o[k] = o[k]; p.ray_d[k] = d[k]; }
        p.has_ray = e.has_ray;
        if (e.has_pose) {
            const ar_pose hp{{e.pose_pos[0], e.pose_pos[1], e.pose_pos[2]}, {0.f, 0.f, 0.f, 1.f}};
            const ar_pose ht = ar_pose_mul(table_from_tracking, hp);
            for (int k = 0; k < 3; k++) h[k] = ht.pos[k];
            p.has_hand = true; for (int k = 0; k < 3; k++) p.hand[k] = h[k];
        }
        p.head = head;
        if (!mine) {
            if (!claim(p)) { leave(e); keep.push_back(e); continue; }
            claimed_.push_back(e.id);   // open claimed gestures: removed at their end or cancel (the claim needs every one)
            if (claimed_.size() > kMaxOpen) claimed_.erase(claimed_.begin());
            if (edges) ++*edges;
        } else {
            claim(p);
            if (p.phase == PointerEvent::end || p.phase == PointerEvent::cancel) {
                forget(claimed_, e.id);
                if (edges) ++*edges;
            }
        }
        routed++;
    }
    events->swap(keep);
    return routed;
}
HandTips hand_tips_in_table(const ar_pose &tracking_from_table, const float origin_from_anchor[16],
                            const float anchor_from_index_tip[16]) {
    HandTips h;
    ar_mat4 oa, ai;
    std::copy(origin_from_anchor, origin_from_anchor + 16, oa.m);
    std::copy(anchor_from_index_tip, anchor_from_index_tip + 16, ai.m);
    const ar_mat4 table_from_anchor = ar_mat4_mul(ar_mat4_inverse_rigid(ar_mat4_from_pose(tracking_from_table)), oa);
    const float zero[3] = {0.f, 0.f, 0.f};
    float pi[4];
    ar_mat4_apply(ar_mat4_mul(table_from_anchor, ai), zero, pi);
    for (int k = 0; k < 3; k++) { h.index_tip[k] = pi[k]; }
    h.tracked = true;
    for (int k = 0; k < 3; k++) if (!std::isfinite(h.index_tip[k])) h.tracked = false;
    return h;
}
}

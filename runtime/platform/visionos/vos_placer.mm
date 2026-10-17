#import <ARKit/ARKit.h>
#include "vos_placer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <TargetConditionals.h>
#include <cstdio>
#include <cstring>
#include <uuid/uuid.h>
#include "vos_log.h"
extern "C" {
#include "ar_math.h"
}
namespace ar {
namespace {
constexpr uint64_t kSettleFrames = 30;            // ARKit's first answers are near-identity
constexpr uint64_t kUntrackedPlaceFrames = 120;
constexpr uint64_t kAnchorWaitFrames = 450;
dispatch_queue_t anchor_requests() {        // serial: a stale anchor's removal is issued before the new anchor's add
    static dispatch_queue_t q = dispatch_queue_create("sm64ar.anchor-requests", DISPATCH_QUEUE_SERIAL);
    return q;
}
std::string uuid_string(ar_world_anchor_t a) {
    uuid_t u;
    ar_world_anchor_get_identifier(a, u);
    char s[37];
    uuid_unparse_upper(u, s);
    return s;
}
ar_pose anchor_pose(ar_world_anchor_t a) {
    const simd_float4x4 m = ar_world_anchor_get_origin_from_anchor_transform(a);
    ar_mat4 r;
    std::memcpy(r.m, &m, sizeof r.m);
    return ar_pose_from_mat4(r);
}
void log_placed(const char *source, const PlacementRecord &r, const ar_pose &t) {
    vos_log("AR_PLACED source=%s anchor=%s pos=%.3f,%.3f,%.3f yaw=%.1f", source, r.anchor.c_str(), (double)t.pos[0],
            (double)t.pos[1], (double)t.pos[2], (double)r.yaw_deg);
}
void log_fallback(bool floor_known, float floor_y, float top) {
    char floor[32] = "unknown";
    if (floor_known) std::snprintf(floor, sizeof floor, "%.3f", (double)floor_y);
    vos_log("AR_PLACED source=fallback floor=%s top=%.3f", floor, (double)top);
}
GestureConfig gesture_config(const PlaceParams &p) {
    GestureConfig c;
    c.yaw_deg = p.yaw_deg;
    c.desk_height = p.desk_height;
    c.below_m = p.below_m;
    c.forward_m = p.forward_m;
    return c;
}
}

VosPlacer::VosPlacer(const Config &c) : cfg_(c), gesture_cfg_(gesture_config(c.place)), gesture_(gesture_cfg_) {
    if (cfg_.record_path.empty()) return;
    std::string err;
    if (cfg_.reset) {
        if (!placement_reset(cfg_.record_path, &stale_anchor_, &err)) vos_log("AR_ERROR %s", err.c_str());
        vos_log("AR_PLACEMENT reset stale_anchor=%s", stale_anchor_.empty() ? "none" : stale_anchor_.c_str());
        return;
    }
    PlacementRecord r;
    if (!placement_load(cfg_.record_path, &r, &err)) { vos_log("AR_PLACEMENT none (%s)", err.c_str()); return; }
    if (r.game != cfg_.game) {
        stale_anchor_ = placement_stale_anchor(r);
        vos_log("AR_PLACEMENT ignored: recorded for %s", r.game.c_str());
        return;
    }
    record_ = r;
    if (r.anchor == "fixed") {
        table_ = r.tracking_from_table;
        valid_ = true;
        gesture_.placed(table_);
        log_placed("json", record_, table_);
    } else {
        waiting_anchor_ = true;                   // ARKit re-adds persisted anchors after the provider starts
        vos_log("AR_PLACEMENT waiting for world anchor %s", r.anchor.c_str());
    }
}

void VosPlacer::attach_world(void *w) {
    {
        std::lock_guard<std::mutex> g(m_);
        world_ = w;
    }
    if (!w) return;
    ar_world_tracking_provider_t world = (__bridge ar_world_tracking_provider_t)w;
    dispatch_queue_t q = dispatch_queue_create("sm64ar.anchors", DISPATCH_QUEUE_SERIAL);
    ar_world_tracking_provider_set_anchor_update_handler(world, q, ^(ar_world_anchors_t added, ar_world_anchors_t updated, ar_world_anchors_t) {
        ar_world_anchors_enumerator_t each = ^bool(ar_world_anchor_t a) {
            if (ar_world_anchor_is_tracked(a)) this->on_anchor(uuid_string(a), anchor_pose(a));
            return true;
        };
        ar_world_anchors_enumerate_anchors(added, each);
        ar_world_anchors_enumerate_anchors(updated, each);
    });
}

void VosPlacer::on_anchor(const std::string &uuid, const ar_pose &pose) {
    std::lock_guard<std::mutex> g(m_);
    if (!placement_anchor_applies(record_, uuid)) return;
    if (gesture_.state() == GestureState::dragging) return;
    table_ = pose;                                // relocalization may move it: the runtime accepts the jump
    valid_ = true;
    gesture_.placed(table_);
    waiting_anchor_ = false;
    if (!anchor_seen_) { anchor_seen_ = true; log_placed("anchor", record_, table_); }
}

void VosPlacer::on_anchor_added(bool ok, const std::string &uuid, long code, uint64_t placement) {
    void *world = nullptr;
    {
        std::lock_guard<std::mutex> g(m_);
        if (!ok) { vos_log("AR_ANCHOR add ok=0 code=%ld (keeping the fixed pose)", code); return; }
        if (placement == placements_) {
            record_.anchor = uuid;
            anchor_seen_ = true;
            save();
            vos_log("AR_ANCHOR add ok=1 uuid=%s", uuid.c_str());
            return;
        }
        world = world_;
        vos_log("AR_ANCHOR add ok=1 uuid=%s for a replaced placement: removing it", uuid.c_str());
    }
    if (world) dispatch_async(anchor_requests(), ^{ remove_anchor(world, uuid); });   // never call ARKit with m_ held
}

void VosPlacer::save() {   // m_ held
    std::string err;
    if (!placement_save(cfg_.record_path, record_, &err)) vos_log("AR_ERROR %s", err.c_str());
}

void VosPlacer::place_fresh(const ar_pose &head, const char *source) {   // m_ held
    const FallbackTable f = vos_fallback_table(head, cfg_.place, planes_);
    place_at(f.table, source);
    log_fallback(f.floor_known, f.floor_y, f.table.pos[1]);
}

void VosPlacer::place_at(const ar_pose &table, const char *source) {   // m_ held
    if (stale_anchor_.empty()) stale_anchor_ = placement_stale_anchor(record_);
    table_ = table;
    valid_ = true;
    gesture_.placed(table);                       // a pinch still held (the placing one) neither drags nor places again
    placements_++;
    record_ = PlacementRecord{};
    record_.game = cfg_.game;
    record_.yaw_deg = cfg_.place.yaw_deg;
    record_.tracking_from_table = table_;
    log_placed(source, record_, table_);
    if (!cfg_.record_path.empty()) save();
}

void VosPlacer::request_anchor(void *world, const ar_pose &at, uint64_t placement) {   // m_ not held
    ar_mat4 m = ar_mat4_from_pose(at);
    simd_float4x4 s;
    std::memcpy(&s, m.m, sizeof s);
    ar_world_anchor_t anchor = ar_world_anchor_create_with_origin_from_anchor_transform(s);
    ar_world_tracking_provider_add_anchor((__bridge ar_world_tracking_provider_t)world, anchor,
                                          ^(ar_world_anchor_t a, bool ok, ar_error_t e) {
        this->on_anchor_added(ok, ok ? uuid_string(a) : std::string(), e ? (long)ar_error_get_error_code(e) : 0L, placement);
    });
}

void VosPlacer::remove_anchor(void *world, const std::string &uuid) {   // m_ not held
    uuid_t u;
    if (uuid_parse(uuid.c_str(), u) != 0) { vos_log("AR_ANCHOR remove ok=0 uuid=%s (not a UUID)", uuid.c_str()); return; }
    const std::string id = uuid;
    ar_world_tracking_provider_remove_anchor_with_identifier((__bridge ar_world_tracking_provider_t)world, u,
                                                             ^(ar_world_anchor_t, bool ok, ar_error_t e) {
        if (ok) vos_log("AR_ANCHOR remove ok=1 uuid=%s", id.c_str());
        else vos_log("AR_ANCHOR remove ok=0 uuid=%s code=%ld", id.c_str(), e ? (long)ar_error_get_error_code(e) : 0L);
    });
}

void VosPlacer::set_planes(std::vector<PlaneInfo> planes) {
    std::lock_guard<std::mutex> g(m_);
    planes_ = std::move(planes);
    planes_ready_ = true;
}

void VosPlacer::planes_authorized() {
    std::lock_guard<std::mutex> g(m_);
    planes_ready_ = true;
}

void VosPlacer::planes_unavailable() {
    std::lock_guard<std::mutex> g(m_);
    if (!cfg_.planes) return;
    cfg_.planes = false;
    vos_log("AR_PLACEMENT no plane detection: placing from the head");
}

void VosPlacer::request_replace() {
    std::lock_guard<std::mutex> g(m_);
    // The old record goes, anchor included: otherwise ARKit's next update of that anchor would put the table back
    // (on_anchor) and end the search. Its anchor is removed from ARKit at the new placement.
    placement_forget(&record_, &stale_anchor_);
    anchor_seen_ = false;
    valid_ = false;
    waiting_anchor_ = false;
    seeking_ = false;
    gesture_.seek();
    reticle_ = Reticle{};
    frames_ = tracked_frames_ = kSettleFrames;
    if (!cfg_.record_path.empty()) {
        std::string ignored, err;
        if (!placement_reset(cfg_.record_path, &ignored, &err)) vos_log("AR_ERROR %s", err.c_str());
    }
    vos_log("AR_PLACEMENT replace requested");
}

std::string VosPlacer::hint() const {
    std::lock_guard<std::mutex> g(m_);
    return seeking_ && !valid_ ? "look at a table and pinch (or press A)" : "";
}

bool VosPlacer::apply_gesture(const GestureResult &r) {   // m_ held
    switch (r.action) {
    case GestureAction::place: {
        place_at(r.table, r.on_plane ? "pinch-plane" : "pinch-fallback");
        if (!r.on_plane) log_fallback(r.floor_known, r.floor_y, r.table.pos[1]);
        return true;
    }
    case GestureAction::move: table_ = r.table; return false;
    case GestureAction::restore:
        table_ = r.table;
        vos_log("AR_PLACEMENT drag cancelled: table back at %.3f,%.3f,%.3f", (double)table_.pos[0], (double)table_.pos[1],
                (double)table_.pos[2]);
        return false;
    case GestureAction::commit: place_at(r.table, "drag"); return true;
    case GestureAction::none: return false;
    }
    return false;
}

void VosPlacer::update(const ar_pose &head, bool tracked, double now_s, bool confirm, const std::vector<SpatialEvent> &events) {
    std::unique_lock<std::mutex> g(m_);
    frames_++;
    if (tracked) tracked_frames_++;
    last_head_ = head;
    if (valid_) {
        bool due = false;
        for (const SpatialEvent &e : events) due = apply_gesture(gesture_.on_event(e, head, planes_)) || due;
        if (due) request_anchor_async(g);
        return;
    }
    if (waiting_anchor_) {
        if (frames_ < kAnchorWaitFrames) return;
        waiting_anchor_ = false;
#if TARGET_OS_SIMULATOR
        table_ = record_.tracking_from_table;     // the simulator's tracking origin is the same every launch
        valid_ = true;
        gesture_.placed(table_);
        log_placed("json-fallback", record_, table_);
        return;
#else
        vos_log("AR_PLACEMENT world anchor %s not found here: placing again", record_.anchor.c_str());
#endif
    }
    const bool settled = tracked_frames_ >= kSettleFrames;
#if TARGET_OS_SIMULATOR
    if (!settled && frames_ < kUntrackedPlaceFrames) return;
#else
    if (!settled) return;                 // a device waits for tracking: the identity head would put the table at
                                          // the tracking origin, near the floor
#endif
    if (cfg_.planes && settled) {
        if (!seeking_) { seeking_ = true; vos_log("AR_PLACEMENT seeking planes=%zu ready=%d", planes_.size(), (int)planes_ready_); }
        const float mz[3] = {0.f, 0.f, -1.f};
        float fwd[3];
        ar_quat_rotate(head.quat, mz, fwd);
        reticle_ = vos_reticle(head.pos, fwd, head.pos[1], planes_, gesture_cfg_);
        if (last_probe_s_ < 0.0 || now_s - last_probe_s_ >= 1.0) {
            last_probe_s_ = now_s;
            vos_log("AR_PLACEMENT probe planes=%zu reticle=%d on_plane=%d point=%.3f,%.3f,%.3f pitch=%.1f", planes_.size(),
                    (int)reticle_.valid, (int)reticle_.on_plane, (double)reticle_.point[0], (double)reticle_.point[1],
                    (double)reticle_.point[2], std::asin((double)std::max(-1.f, std::min(1.f, fwd[1]))) * 57.29578);
        }
        bool due = false;
        for (const SpatialEvent &e : events) due = apply_gesture(gesture_.on_event(e, head, planes_)) || due;
        if (!due && confirm && reticle_.valid) {
            place_at(vos_table_at_hit(head, reticle_.point, cfg_.place.yaw_deg), "reticle-a");
            if (!reticle_.on_plane) log_fallback(reticle_.floor_known, reticle_.floor_y, reticle_.point[1]);
            due = true;
        } else if (!due && confirm) {
            vos_log("AR_PLACEMENT A ignored: no reticle (looking straight %s)", fwd[1] < 0.f ? "down" : "up");
        }
        if (!due) return;
    } else {
        place_fresh(head, settled ? "fresh" : "untracked");
    }
    request_anchor_async(g);
}

void VosPlacer::request_anchor_async(std::unique_lock<std::mutex> &held) {   // m_ held; unlocked here
    void *world = world_;
    const ar_pose at = table_;
    const uint64_t placement = placements_;
    std::string stale;
    stale.swap(stale_anchor_);
    held.unlock();
    if (!world) return;                      // the completions take m_: never call ARKit with it held
    // Off the layer thread: on the simulator add_anchor returns only after its completion has run, which
// took 0.15-1.1 s and stopped the frame loop for as long. The placer lives for the whole process, so `this` outlives
// the block; ARKit's own completions still take m_ as before.
    dispatch_async(anchor_requests(), ^{
        const auto t0 = std::chrono::steady_clock::now();
        if (!stale.empty()) remove_anchor(world, stale);
        request_anchor(world, at, placement);
        vos_log("AR_ANCHOR requested ms=%.1f (anchor queue)", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    });
}

PlacerPreview VosPlacer::preview() const {
    std::lock_guard<std::mutex> g(m_);
    PlacerPreview p;
    if (valid_) {
        if (gesture_.state() == GestureState::dragging) { p.state = PreviewState::dragging; p.pose = gesture_.table(); }
        return p;
    }
    if (!seeking_) return p;
    p.state = PreviewState::seeking;
    p.planes = planes_;
    if (reticle_.valid) {
        p.reticle = true;
        p.on_plane = reticle_.on_plane;
        p.pose = vos_table_at_hit(last_head_, reticle_.point, cfg_.place.yaw_deg);
    }
    return p;
}

bool VosPlacer::valid() const { std::lock_guard<std::mutex> g(m_); return valid_; }
uint64_t VosPlacer::placements() const { std::lock_guard<std::mutex> g(m_); return placements_; }
ar_pose VosPlacer::tracking_from_table() const { std::lock_guard<std::mutex> g(m_); return table_; }
}

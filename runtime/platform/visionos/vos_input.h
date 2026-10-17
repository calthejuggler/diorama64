#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>
extern "C" {
#include "ar_math.h"
}
namespace ar {
enum class SpatialKind : uint8_t { indirect_pinch = 0, direct_pinch = 1, pointer = 2, touch = 3, other = 4 };
enum class SpatialPhase : uint8_t { active = 0, ended = 1, cancelled = 2 };
// In the immersive space's coordinates (metres; assumed = ARKit tracking space). The ray is the selection ray at pinch
// start; the pose is the pinching hand's.
struct SpatialEvent {
    uint64_t id = 0; SpatialKind kind = SpatialKind::other; SpatialPhase phase = SpatialPhase::active; double t_s = 0.0;
    bool has_ray = false; float ray_o[3] = {0, 0, 0}, ray_d[3] = {0, 0, -1};
    bool has_pose = false; float pose_pos[3] = {0, 0, 0}, pose_quat[4] = {0, 0, 0, 1};
    uint8_t chirality = 0;
};
class SpatialEventQueue {        // main thread pushes, the layer thread drains; at most kCap waiting, the oldest dropped
public:
    static constexpr size_t kCap = 256;
    void push(const SpatialEvent &e);
    size_t drain(std::vector<SpatialEvent> *out);
    uint64_t dropped() const;
private: mutable std::mutex m_; std::deque<SpatialEvent> q_; uint64_t dropped_ = 0;
};
// On a queue drop or a stale batch (after an attach or a resume) each open gesture gets a synthesized cancel and its
// remaining events are ignored. Layer thread.
class GestureContinuity {
public:
    struct Result { size_t discarded = 0, cancelled = 0; };
    Result filter(std::vector<SpatialEvent> *events, uint64_t dropped_total, bool discard);
private:
    std::vector<SpatialEvent> open_;
    std::vector<uint64_t> ignored_;
    uint64_t dropped_seen_ = 0;
    static constexpr size_t kMaxIgnored = 16;
};
std::vector<SpatialEvent> scripted_pinch(double t0, const ar_pose &head, float dx_m, float dz_m);
// Each gesture is seen once, at its first event: it is offered to the claim only if that event is active, has a ray and
// the table is placed; otherwise it belongs to the placer for good, so the placer always sees the end of what it began.
struct PointerEvent {
    uint64_t id = 0;
    enum Phase : uint8_t { begin = 0, move = 1, end = 2, cancel = 3 } phase = begin;
    float ray_o[3] = {0.f, 0.f, 0.f}, ray_d[3] = {0.f, 0.f, -1.f};
    bool has_ray = true;
    bool has_hand = false; float hand[3] = {0.f, 0.f, 0.f};
    ar_pose head{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
};
using PointerClaim = std::function<bool(const PointerEvent &)>;   // begin: true claims; later phases: the result is ignored
class PointerRouter {
public:
    // Layer thread, once per frame before the placer.
    size_t route(std::vector<SpatialEvent> *events, bool placed, const ar_pose &tracking_from_table, const ar_pose &head_tracking,
                 const PointerClaim &claim, size_t *edges = nullptr, bool pose_only = false);
private:
    std::vector<uint64_t> claimed_, offered_;
    static constexpr size_t kMaxOpen = 256;
};
// Table space (metres), from ARKit's hand anchor (tracking space = the immersive space, as the pinches) and the joint's
// anchor-from-joint transform (column-major 4x4, simd layout).
struct HandTips { bool tracked = false; float index_tip[3] = {0.f, 0.f, 0.f}; };
HandTips hand_tips_in_table(const ar_pose &tracking_from_table, const float origin_from_anchor[16],
                            const float anchor_from_index_tip[16]);
using HandSink = std::function<void(const HandTips hands[2])>;
class TablePlacedGate {
public:
    bool step(uint64_t placements, bool head_tracked) {
        if (!head_tracked || placements == seen_) return false;
        seen_ = placements;
        return true;
    }
private:
    uint64_t seen_ = UINT64_MAX;
};
}

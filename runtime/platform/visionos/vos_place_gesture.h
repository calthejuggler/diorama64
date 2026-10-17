#pragma once
#include <cstdint>
#include <vector>
#include "vos_input.h"
#include "vos_planes.h"
namespace ar {
struct GestureConfig {
    float dead_zone_m = 0.01f;
    bool desk_height = true;
    float below_m = 0.5f;
    float min_dist_m = 0.3f, max_dist_m = 2.f;
    float forward_m = 1.2f;
    float max_gain = 4.f;
    float snap_m = 0.15f;
    float yaw_deg = 0.f;
};
struct Reticle {
    bool valid = false; bool on_plane = false; float point[3] = {0.f, 0.f, 0.f};
    bool floor_known = false; float floor_y = 0.f;
};
Reticle vos_reticle(const float origin[3], const float dir[3], float head_y, const std::vector<PlaneInfo> &planes, const GestureConfig &c);
enum class GestureState { seeking, placed, dragging };
enum class GestureAction { none, place, move, commit, restore };
struct GestureResult {
    GestureAction action = GestureAction::none; ar_pose table{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}; bool on_plane = false;
    bool floor_known = false; float floor_y = 0.f;
};
class PlaceGesture {
public:
    explicit PlaceGesture(const GestureConfig &c = GestureConfig{});
    // Both drop a gesture in progress; the rest of that pinch is ignored until it ends.
    void seek();
    void placed(const ar_pose &table);
    GestureState state() const;
    const ar_pose &table() const;
    GestureResult on_event(const SpatialEvent &e, const ar_pose &head, const std::vector<PlaneInfo> &planes);
private:
    GestureConfig c_;
    GestureState state_ = GestureState::seeking;
    ar_pose table_{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}, start_ = table_;
    void drop_active();
    uint64_t active_id_ = 0; bool have_active_ = false, moved_ = false;
    uint64_t held_id_ = 0; bool have_held_ = false;
    float hand0_[3] = {0.f, 0.f, 0.f}, gain_ = 1.f;
};
}

#pragma once
#include <vector>
#include "vos_placement.h"
extern "C" {
#include "ar_math.h"
}
namespace ar {
struct PlaneInfo {
    float center[3] = {0.f, 0.f, 0.f};
    float normal[3] = {0.f, 1.f, 0.f};
    float u[3] = {1.f, 0.f, 0.f}, v[3] = {0.f, 0.f, 1.f};
    float half_u = 0.f, half_v = 0.f;
    bool is_table = false;
    bool is_floor = false;
};
// The normal is whichever of local Y and Z is nearer world up (ARKit's convention unverified on device).
PlaneInfo plane_info_from_matrix(const float m[16], float width, float height, bool is_table, bool is_floor = false);
struct PlaneHit { bool hit = false; float point[3] = {0.f, 0.f, 0.f}; int plane = -1; bool is_table = false; };
constexpr float kMinTableAboveFloorM = 0.4f;
bool vos_plane_is_table_candidate(const PlaneInfo &p, bool floor_known, float floor_y);
PlaneHit vos_pick_plane(const ar_pose &tracking_from_head, const std::vector<PlaneInfo> &planes, float max_m = 4.f);
PlaneHit vos_pick_ray(const float origin[3], const float dir[3], const std::vector<PlaneInfo> &planes, float max_m = 4.f);
ar_pose vos_table_at_hit(const ar_pose &tracking_from_head, const float point[3], float yaw_deg);
bool vos_floor_y(const ar_pose &tracking_from_head, const std::vector<PlaneInfo> &planes, float *y);
struct FallbackTable { ar_pose table; bool floor_known = false; float floor_y = 0.f; };
FallbackTable vos_fallback_table(const ar_pose &tracking_from_head, const PlaceParams &p, const std::vector<PlaneInfo> &planes);
float vos_fallback_top_y(float eye_y, bool floor_known, float floor_y);
class PlaneSettle {
public:
    explicit PlaneSettle(float tol_m = 0.05f, double settle_s = 1.5) : tol_(tol_m), settle_(settle_s) {}
    bool update(const PlaneHit &h, double t_s, bool confirm, float out[3]);
    void reset() { have_ = false; }
private:
    float tol_; double settle_;
    bool have_ = false;
    float p_[3] = {0.f, 0.f, 0.f};
    double since_ = 0.0;
};
}

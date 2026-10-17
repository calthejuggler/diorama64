#include "free_fly.h"
#include <cmath>
#include <cstring>
namespace ar {
const float kTableCenter[3] = {0.f, 0.75f, 0.f};
void FreeFly::move(float f, float r, float u, float dt, float speed) {
    float fx = -std::sin(yaw), fz = -std::cos(yaw), rx = std::cos(yaw), rz = -std::sin(yaw);
    pos[0] += (f * fx + r * rx) * speed * dt;
    pos[1] += u * speed * dt;
    pos[2] += (f * fz + r * rz) * speed * dt;
}
void FreeFly::look(float dyaw, float dpitch) {
    const float lim = 3.14159265f / 2.f;
    yaw += dyaw;
    pitch = std::fmax(-lim, std::fmin(lim, pitch + dpitch));
}
ar_pose FreeFly::pose() const {
    ar_pose a{{pos[0], pos[1], pos[2]}, {0.f, std::sin(yaw / 2), 0.f, std::cos(yaw / 2)}};
    ar_pose b{{0.f, 0.f, 0.f}, {std::sin(pitch / 2), 0.f, 0.f, std::cos(pitch / 2)}};
    return ar_pose_mul(a, b);
}
ar_pose preset_pose(CameraPreset p) {
    float eye[3] = {0.f, 1.55f, 1.9f};
    float tgt[3] = {kTableCenter[0], kTableCenter[1], kTableCenter[2]};
    switch (p) {
        case CameraPreset::Near: break;
        case CameraPreset::Far: eye[2] = -1.9f; break;
        case CameraPreset::Top: {
            // Straight down with an explicit screen-up of -Z (yaw 0, pitch -90 degrees), not look_at's degenerate
            // branch. -Z is also the up ar_basis_from_forward picks for straight down, so view and lighting agree.
            const float s = std::sqrt(0.5f);
            return ar_pose{{0.f, 3.6f, 0.f}, {-s, 0.f, 0.f, s}};
        }
        case CameraPreset::Low: eye[0] = 1.3f; eye[1] = 0.95f; eye[2] = 0.9f; tgt[0] = 0.2f; tgt[1] = 0.85f; tgt[2] = -0.3f; break;
    }
    return ar_pose_look_at(eye, tgt);
}
ar_pose orbit_pose(double t, float radius, float height, double period, const float center[3]) {
    double a = 2.0 * 3.14159265358979 * t / period;
    float eye[3] = {center[0] + radius * (float)std::sin(a), height, center[2] + radius * (float)std::cos(a)};
    return ar_pose_look_at(eye, center);
}
bool parse_preset(const char *n, CameraPreset *out) {
    if (!std::strcmp(n, "near")) { *out = CameraPreset::Near; return true; }
    if (!std::strcmp(n, "far")) { *out = CameraPreset::Far; return true; }
    if (!std::strcmp(n, "top")) { *out = CameraPreset::Top; return true; }
    if (!std::strcmp(n, "low")) { *out = CameraPreset::Low; return true; }
    return false;
}
}

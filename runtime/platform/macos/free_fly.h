#pragma once
extern "C" {
#include "ar_math.h"
}
namespace ar {
// Free-fly head in tracking space (metres): yaw 0 looks along -Z, pitch < 0 looks down.
struct FreeFly {
    float pos[3] = {0.f, 1.55f, 1.9f};
    float yaw = 0.f, pitch = -0.4f;
    void move(float forward, float right, float up, float dt, float speed = 1.0f);
    void look(float dyaw, float dpitch);
    ar_pose pose() const;
};
enum class CameraPreset { Near, Far, Top, Low };
extern const float kTableCenter[3];
ar_pose preset_pose(CameraPreset p);
ar_pose orbit_pose(double t_seconds, float radius, float height, double period_s, const float center[3] = kTableCenter);
bool parse_preset(const char *name, CameraPreset *out);
}

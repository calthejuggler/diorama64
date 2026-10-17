#pragma once
#include <cstdint>
namespace ar {
struct RateMapShape {
    uint32_t maps = 0, layers = 0;
    uint32_t screen[2] = {0, 0};
    const uint32_t (*physical)[2] = nullptr;
    uint32_t texture[2] = {0, 0};
};
struct FoveationPlan {
    bool use = false;
    const char *reason = "";
    uint32_t target_w = 0, target_h = 0, target_layers = 0;
    uint32_t view_w = 0, view_h = 0;
};
FoveationPlan vos_foveation_plan(const RateMapShape &m, bool layered, uint32_t view_count);
}

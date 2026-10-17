#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_CANNON_ARC_MAX_TICKS 300
#define SM64_CANNON_ARC_MAX_POINTS (4 * SM64_CANNON_ARC_MAX_TICKS + 1)
#define SM64_CANNON_ARC_WIDTH_M 0.002f
typedef float (*sm64_floor_fn)(void *user, float x, float y, float z);
/* Exactly as act_in_cannon's launch and act_shot_from_cannon + perform_air_step move Mario; walls, ceilings, wind and
 * objects are ignored. */
uint32_t sm64_cannon_arc(const float start[3], float sin_pitch, float cos_pitch, float sin_yaw, float cos_yaw,
                         sm64_floor_fn floor, void *user, float *xyz, uint32_t cap, uint32_t max_ticks);
#ifdef __cplusplus
}
#endif

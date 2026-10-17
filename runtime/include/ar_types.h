#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Metres, right-handed, +Y up, -Z forward; quaternion xyzw. */
typedef struct ar_pose { float pos[3]; float quat[4]; } ar_pose;
/* Tangents of the half-angles; left and down are negative. Backends build their own projection. */
typedef struct ar_fov { float left, right, up, down; } ar_fov;
/* Standard gamepad layout. Sticks in [-1, 1] with +y up; triggers in [0, 1]. */
enum {
    AR_BTN_A = 1u << 0, AR_BTN_B = 1u << 1, AR_BTN_X = 1u << 2, AR_BTN_Y = 1u << 3,
    AR_BTN_LB = 1u << 4, AR_BTN_RB = 1u << 5, AR_BTN_START = 1u << 6, AR_BTN_SELECT = 1u << 7,
    AR_BTN_DPAD_UP = 1u << 8, AR_BTN_DPAD_DOWN = 1u << 9, AR_BTN_DPAD_LEFT = 1u << 10, AR_BTN_DPAD_RIGHT = 1u << 11,
    AR_BTN_LSTICK = 1u << 12, AR_BTN_RSTICK = 1u << 13
};
typedef struct ar_pad { uint32_t buttons; float lx, ly, rx, ry, lt, rt; } ar_pad;
/* v5 reference_units_per_meter: the scale the game's own sizes were tuned at (its default world scale). The AR layer's metric
 * sizes (table shadow, decal slope bias, debug dots) never grow past their size at that scale: at more units per metre (the
 * world drawn smaller) they shrink with it, at fewer they stay metric. 0 = none: always metric. */
typedef struct ar_level_info { float units_per_meter; float ground_y; float bounds_min[3], bounds_max[3]; float reference_units_per_meter; } ar_level_info;
#ifdef __cplusplus
}
#endif

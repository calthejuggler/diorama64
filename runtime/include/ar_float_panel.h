#pragma once
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ar_float_panel_params { float above_top_m, max_above_ground_m, edge_gap_m; } ar_float_panel_params;
#define AR_FLOAT_PANEL_DEFAULTS { 0.50f, 1.50f, 0.30f }
enum { AR_FLOAT_PANEL_HIDDEN = -1, AR_FLOAT_PANEL_ABOVE = 0, AR_FLOAT_PANEL_BESIDE = 1 };
int ar_float_panel_anchor(const ar_float_panel_params *p, const float mn[3], const float mx[3], float ground_y, float upm,
                          const float head[3], float out[3]);
float ar_yaw_billboard_axes(const float a[3], const ar_pose *head, float right[3], float up[3], float back[3]);
#ifdef __cplusplus
}
#endif

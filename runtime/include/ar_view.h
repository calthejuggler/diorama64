#pragma once
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
void ar_billboard_axes(const float obj[3], const float eye[3], const float eye_up[3],
                       float right[3], float up[3], float back[3]);
void ar_billboard_axes_upright(const float obj[3], const float eye[3], const float eye_up[3],
                               float right[3], float up[3], float back[3]);

void ar_billboard_axes_yaw(const float obj[3], const float eye[3], const float eye_fwd[3],
                           float right[3], float up[3], float back[3]);

typedef struct ar_stick_params { float blend_start_deg, blend_end_deg; } ar_stick_params;
typedef struct ar_stick_state { float fwd[2]; int valid; } ar_stick_state;
void ar_stick_forward(const float head[3], const float head_quat[4], const float focus[3],
                      const ar_stick_params *p, ar_stick_state *s, float out_xz[2]);

/* Listener-space vector of a source: R_head^T (src - head) * scale; -Z ahead, +X right. */
void ar_listener_vector(const float head[3], const float head_quat[4], const float src[3], float scale, float out[3]);
void ar_listener_point(const float head[3], const float head_quat[4], const float v[3], float scale, float out[3]);

float ar_music_box_gain(const float head_m[3], const float box_min_m[3], const float box_max_m[3], float falloff_per_m);

typedef struct ar_panel_params { float width_m, height_m, margin_m, lift_m, tilt_deg, hysteresis_deg, ease_s; } ar_panel_params;
#define AR_PANEL_DEFAULTS { 0.20f, 0.15f, 0.03f, 0.02f, 15.f, 35.f, 0.4f }
typedef struct ar_panel_state { float azimuth, target; int valid; } ar_panel_state;
void ar_panel_update(ar_panel_state *s, const ar_panel_params *p, const float head_table[3], float half_x, float half_z,
                     float dt_s, float origin[3], float right[3], float up[3], float normal[3]);
float ar_overlay_width(float width_m, float min_angle_rad, float dist_m);
/* Must match the quad the renderer draws (renderer.cpp draw_overlays). */
int ar_world_point_hit(const float a[3], int yaw_only, const ar_pose *head, float w_m, const float box[4], float screen_w,
                       float screen_h, const float o[3], const float d[3], float ndc[2]);
#ifdef __cplusplus
}
#endif

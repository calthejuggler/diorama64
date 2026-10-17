/* Set by the adapter before each game tick, read by the AR game hooks (stick, billboards, listener). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sm64_ar_view_set_head(const float pos_level[3], const float quat[4]);
void sm64_ar_view_get_head(float pos_level[3], float quat[4]);
void sm64_ar_view_set_control_frame(const float pos_level[3], const float quat[4]);
void sm64_ar_view_reset(void);
#define SM64_AR_STICK_BLEND_START_DEG 65.f
#define SM64_AR_STICK_BLEND_END_DEG 80.f
int16_t sm64_ar_control_yaw(const float mario_level[3]);
int16_t sm64_ar_control_yaw_peek(const float mario_level[3]);
#define SM64_AR_LISTENER_SCALE 0.5f   /* the head is ~2-4x farther from the action than Lakitu was */
/* Sound is metric: SM64's own attenuation and panning hear a source d metres from the head as at 1:8000. */
float sm64_ar_listener_scale(float units_per_meter);
void sm64_ar_view_set_units_per_meter(float units_per_meter);
void sm64_ar_billboard(float dest[4][4], const float mtx[4][4], const float pos[3], int yaw_only);
int sm64_ar_upright_behavior(int behavior_id);
enum { SM64_BILLBOARDS_FULL = 0, SM64_BILLBOARDS_UPRIGHT = 1 };
void sm64_ar_set_billboard_style(int style);
int sm64_ar_billboard_style(void);
const char *sm64_ar_billboard_style_name(int style);
int sm64_ar_billboard_style_parse(const char *name);
int sm64_ar_billboard_yaw_only(int cylinder, int upright_behavior);
void sm64_ar_listener_vector(const float level_pos[3], float out[3]);
#ifdef __cplusplus
}
#endif

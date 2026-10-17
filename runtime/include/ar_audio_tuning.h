#pragma once
#include "ar_audio_types.h"
#include "ar_types.h"
#include "ar_audio_distance.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AR_AUDIO_HEAD_FOCUS_MAX_EXPONENT 1.75f
/* Every field's zero means "off" (or the default), so an aggregate init that names only the first fields changes nothing
 * else. ar_audio_tuning_clamp gives the ranges below; ar_audio_lane_gain expects a clamped set. */
typedef struct ar_audio_tuning {
    float distance_exponent;
    float spotlight_floor_db;
    float spotlight_cone_deg;
    float widen;
    float sfx_db;
    float music_db;
} ar_audio_tuning;
#define AR_AUDIO_SPOTLIGHT_CONE_DEFAULT_DEG 30.f
#define AR_AUDIO_SPOTLIGHT_RAMP_DEG 20.f
float ar_audio_head_focus_exponent(float strength);
void  ar_audio_tuning_clamp(ar_audio_tuning *t);
float ar_db_to_gain(float db);
float ar_spotlight_gain(float angle_deg, float cone_deg, float floor_db);
float ar_widen_azimuth_deg(float azimuth_deg, float factor);
/* Pitch is weighted down: on a tabletop the eyes look down further than the head pitches; turning (yaw) is what aims
 * the spotlight. */
#define AR_AUDIO_SPOTLIGHT_PITCH_WEIGHT 0.3f
float ar_audio_spotlight_angle_deg(const ar_pose *head, const float p[3]);
#define AR_AUDIO_WIDEN_FADE_LO 0.25f
#define AR_AUDIO_WIDEN_FADE_HI 0.5f
void  ar_audio_widen(const ar_pose *head, const float src[3], float factor, float out[3]);
float ar_distance_extra_gain(float distance_m, float exponent);
#define AR_AUDIO_RING_FOCUS_MAX_EXPONENT 3.0f
#define AR_AUDIO_RING_FOCUS_FLOOR_DB (-30.0f)
float ar_audio_ring_focus_gain(float d, float d_near, float strength);
#define AR_AUDIO_RING_GLIDE_S 0.5f
float ar_audio_glide(float y, float target, float dt_s, float tau_s);
#define AR_AUDIO_OBJECT_FOCUS_MIN_DISTANCE_M 0.1f
int ar_audio_object_focus_eligible(uint32_t flags);
int ar_audio_object_focus_apply(ar_audio_source *lanes, int count, const float head[3], float strength);
float ar_audio_lane_gain(const ar_audio_tuning *t, const ar_pose *head, const ar_audio_source *lane, float out_pos[3]);
#ifdef __cplusplus
}
#endif

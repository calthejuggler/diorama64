#pragma once
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The intro's camera cutscene (CUTSCENE_INTRO_PEACH, camera.c sCutsceneIntroPeach) plays these shots (sCutsceneShot): 0 the
 * letter (until it closes), 1 the field of view reset (35 frames), 2 the flight to the pipe (820 frames: Lakitu flies the
 * camera over the grounds), 3 Mario appears (270 frames: the pipe rises, Mario jumps out and lands; Lakitu's dialog opens
 * once he has landed, ~19 frames before the shot ends), 4 Lakitu's dialog. The panel is wanted from shot SM64_PIP_FIRST_SHOT
 * until Mario has appeared: until shot SM64_PIP_END_SHOT begins or a dialog opens, whichever is first (on the story
 * recording: wanted from tick 1003, the flight's first, to 2073; Lakitu's dialog opens at 2074, shot 4 begins at 2093). */
#define SM64_PIP_FIRST_SHOT 2
#define SM64_PIP_END_SHOT 4
#define SM64_PIP_WIDTH_M 0.48f
#define SM64_PIP_MIN_DEG 20.f
#define SM64_PIP_ASPECT (4.f / 3.f)
#define SM64_PIP_FADE_TICKS 9
#define SM64_PIP_SKY_RGBA { 110, 160, 225, 255 }
#define SM64_PIP_CENTRE_M 0.45f
#define SM64_PIP_EDGE_GAP_M 0.30f
#define SM64_PIP_FOLLOW_WIDTH_M (0.5f * SM64_PIP_WIDTH_M)
#define SM64_PIP_FOLLOW_MIN_DEG (0.5f * SM64_PIP_MIN_DEG)
#define SM64_PIP_FOLLOW_ABOVE_M 0.06f
#define SM64_PIP_FOLLOW_TAU_S 0.15f
#define SM64_PIP_FOLLOW_SNAP_M 0.10f
int   sm64_pip_follow_target(const float lakitu[3], float upm, float out[3]);
typedef struct sm64_pip_follow { int have; float pos[3]; } sm64_pip_follow;
void  sm64_pip_follow_reset(sm64_pip_follow *f);
int   sm64_pip_follow_step(sm64_pip_follow *f, const float lakitu[3], float upm, float dt_s, float out[3]);
/* Not while paused: vanilla cannot pause during the intro, but coopdx's pauseAnywhere option can. */
int   sm64_pip_wanted(int setting_on, int screen_is_level, int intro_cutscene, int shot, int dialog_open, int paused);
typedef struct sm64_pip_fade { int level; } sm64_pip_fade;
void  sm64_pip_fade_step(sm64_pip_fade *f, int want);
float sm64_pip_alpha(const sm64_pip_fade *f);
/* SM64's row-vector matrices multiplied as the game does (v * view * roll * proj), returned column-major in Metal
 * reverse-Z clip space. */
int   sm64_pip_clip_from_level(const float view[16], const float roll[16], const float proj[16], float out[16]);
int   sm64_pip_camera_pose(const float view[16], const float roll[16], float upm, float ground_y, ar_pose *table_from_eye);
int   sm64_pip_anchor(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], float out[3]);
int   sm64_pip_hides_score(const float pip[3], const float score[3], float score_w_m, float score_h_m, float upm);
int   sm64_pip_panels_overlap(const float a[3], float aw_m, float ah_m, const float b[3], float bw_m, float bh_m, float upm);
#ifdef __cplusplus
}
#endif

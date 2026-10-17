#pragma once
#include "sm64_touch.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_EYES_BOTH = 0, SM64_EYES_FINGER = 1, SM64_EYES_OFF = 2 };
enum { SM64_EYES_AT_NONE = 0, SM64_EYES_AT_FINGER = 1, SM64_EYES_AT_VIEWER = 2 };
#define SM64_EYES_HAND_RANGE_M 1.0f
#define SM64_EYES_EASE_S 0.12f
/* gd's camera sits ~0.25 m in front of the eyes (table metres at the title's scale), so the viewer and most
 * fingertips are behind it or near its plane, where a projection fails or blows up. The point projected is the target's
 * direction from the eyes, at most this far from them (it gives the direction on gd's screen only). */
#define SM64_EYES_AIM_M 0.035f
/* gd's cursor range (renderer.c's clamp at its 4:3 view, 320x240: 16 px left and top, the hand sprite's 48 and 32) */
#define SM64_EYES_CSR_MIN_X 16.f
#define SM64_EYES_CSR_MAX_X 272.f
#define SM64_EYES_CSR_MIN_Y 16.f
#define SM64_EYES_CSR_MAX_Y 208.f
const char *sm64_eyes_mode_name(int mode);
const char *sm64_eyes_follow_name(int follow);
#define SM64_EYES_HAND_LEAVE_M 1.1f
#define SM64_EYES_RAISED_M 0.30f
#define SM64_EYES_RAISED_LEAVE_M 0.35f
#define SM64_EYES_HOLD_TICKS 6
typedef struct sm64_eyes_pick { int hand, hold; float tip[3]; } sm64_eyes_pick;
void sm64_eyes_pick_reset(sm64_eyes_pick *p);
int sm64_eyes_pick_step(sm64_eyes_pick *p, int mode, const sm64_hand_tips hands[2], const float viewer[3], const float head[3],
                        float out[3]);
int sm64_eyes_aim(const float target[3], const float eyes[3], float radius, float out[3]);
float sm64_eyes_angle_deg(const float target[3], const float eyes[3], const float fwd[3]);
/* gd's eye maths are vanilla (0012-Title-screen-head-splash-logo-and-game-over-backdrop.patch); the input only chooses
 * where gd's cursor is. Each eye's screen place is 16 px from their midpoint, so a cursor near the eyes crosses them. */
#define SM64_EYES_CSR_R 80.f
#define SM64_EYES_FOLLOW_DEG 10.f
#define SM64_EYES_REST_DEG 7.f
int sm64_eyes_follows(int following, float angle_deg);
/* Never below the eyes' line: from a viewer below, gd's eyes looking down go behind the nose. */
int sm64_eyes_cursor(const float mid_c[2], const float toward_c[2], float out[2]);
void sm64_eyes_clamp(float c[2]);
/* gd keeps whole pixels: the ease restarts from the cursor when it is not where it was left (a grab's or the stick's
 * move). */
typedef struct sm64_eyes { int valid; float c[2], left[2]; } sm64_eyes;
void sm64_eyes_reset(sm64_eyes *e);
void sm64_eyes_jump(sm64_eyes *e, const float at[2]);
void sm64_eyes_ease(sm64_eyes *e, const float cur[2], const float target[2], float dt_s, float out[2]);
#ifdef __cplusplus
}
#endif

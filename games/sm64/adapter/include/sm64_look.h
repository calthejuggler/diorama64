#pragma once
#include <stddef.h>
#include <stdint.h>
#include "fc_frame.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Geometry below the table goes, a floor Mario can stand on never does (the castle basement, VCUTM's lower floor, BoB's
 * dip). */
#define SM64_CLIP_FLOOR_MARGIN_M 0.005f
#define SM64_CLIP_BOX_MARGIN_M 0.01f
void sm64_look_clip(int floor_on, int box_on, float ground_y, float lowest_floor, const float mn[3], const float mx[3],
                    float upm, fc_look *out);
/* The game's clock (frame / 30 s) is wrapped hourly so the shader's sines stay precise. */
void sm64_look_water(const float *boxes5, int n, uint32_t frame, fc_look *out);
#define SM64_CUT_RADIUS_M 0.06f
#define SM64_CUT_MID_UNITS 80.f
void sm64_look_cut(int on, int has_mario, const float mario[3], float upm, fc_look *out);
enum { SM64_BEHIND_SILHOUETTE = 0, SM64_BEHIND_CUTAWAY = 1, SM64_BEHIND_BOTH = 2, SM64_BEHIND_OFF = 3 };
void sm64_behind_walls(int choice, int *silhouette, int *cut);
#define SM64_CUT_SNAP_UNITS 400.f
typedef struct sm64_cut_state { uint32_t frame; int have, have_prev; float target[3], prev[3]; } sm64_cut_state;
void sm64_look_cut_prev(sm64_cut_state *s, uint32_t frame, fc_look *out);
typedef struct sm64_warp_sample { int active, type, time, count, delay, paused; uint8_t rgb[3]; int ready, key; } sm64_warp_sample;
typedef struct sm64_trans_state {
    float cover; int kind; uint8_t rgb[3]; int held;
    float prev; int latch, ready_seen, ready_key; uint32_t frame; int stepped;
} sm64_trans_state;
#define SM64_TRANS_HOLD_TICKS 60
#define SM64_TRANS_JUMP 0.25f
/* A vanilla delay shows the scene (as the N64), except when a new area became ready while the transition waited out its
 * delay (Level mode loaded the course before the painting's delayed fade ran): the leftover delay is latched and the
 * cover ramps over it. */
void sm64_trans_step(sm64_trans_state *s, const sm64_warp_sample *w);
int sm64_trans_due(sm64_trans_state *s, uint32_t frame);
void sm64_look_transition(int on, const sm64_trans_state *s, const float centre[3], const float mn[3], const float mx[3],
                          float upm, fc_look *out);
void sm64_look_trans_fade(int clear, int transitions_on, const sm64_trans_state *s, fc_look *out);
/* the act selector's uncollected star (actors/transparent_star: G_CC_SHADEFADEA with env alpha 120) in the MENU
 * group: is_pale finds its draws (in the menu group, blended, env alpha SM64_ACT_STAR_PALE_ALPHA); the capture then draws
 * them with env alpha SM64_ACT_STAR_SOLID_ALPHA and SM64_ACT_STAR_SHADE of their shade (render data only). */
#define SM64_ACT_STAR_PALE_ALPHA 120
#define SM64_ACT_STAR_SOLID_ALPHA 235
#define SM64_ACT_STAR_SHADE 0.68f
int  sm64_act_star_is_pale(int in_menu, uint8_t render_flags, const uint8_t env[4]);
void sm64_act_star_shade(fc_vertex *v, uint32_t n);
#ifdef __cplusplus
}
#endif

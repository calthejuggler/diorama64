/* sm64_host.c asserts SM64_SCREEN_* against gfx_ar_hooks.h's GFX_AR_SCREEN_*. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_SCREEN_LEVEL = 0, SM64_SCREEN_TITLE = 1, SM64_SCREEN_FILE_SELECT = 2, SM64_SCREEN_ACT_SELECT = 3,
       SM64_SCREEN_CAKE = 4, SM64_SCREEN_CREDITS = 5 };
const char *sm64_screen_name(int screen);
const char *sm64_screen_title(int screen);
void sm64_file_button_cursor(int file, float out[2]);
#define SM64_MENU_WIDTH_M 0.50f
#define SM64_MENU_LIFT_M 0.10f
#define SM64_MENU_LARGE_WIDTH_M 0.70f
float sm64_menu_width_m(int size_choice);
int  sm64_screen_is_menu(int screen);
/* Level x = z = 0 is the table centre: ar_table_from_level is scale plus ground offset only. */
void sm64_menu_anchor(float ground_y, float upm, float out[3]);
#define SM64_CREDITS_WIDTH_M 0.80f
#define SM64_CREDITS_ABOVE_M 0.30f
void sm64_credits_anchor(float ground_y, float upm, float out[3]);
#define SM64_TITLE_HEIGHT_M 0.28f
#define SM64_TITLE_SETTLE_TICKS 15
#define SM64_TITLE_SETTLE_FRAC 0.02f
#define SM64_TITLE_FIT_MAX_TICKS 150
#define SM64_TITLE_FIT_TICKS 60
#define SM64_TITLE_TEXT_WIDTH_M 0.25f
#define SM64_GAMEOVER_WIDTH_M 0.56f
#define SM64_GAMEOVER_BEHIND_M 0.03f
void sm64_gameover_anchor(const float mn[3], const float mx[3], float upm, float out[3]);
/* 0012-Title-screen-head-splash-logo-and-game-over-backdrop.patch tags the tiles only with gArStory, so a Level start
 * never has a panel to show. */
int  sm64_gameover_panel_shown(int story, int screen, int title_gameover, int gameover_text_choice);
/* The hold box is the logo model at scale 1 in its own camera (geo_intro_super_mario_64_logo between
 * INTRO_STEPS_ZOOM_IN and _HOLD_1): measured, level units. */
#define SM64_SPLASH_HOLD_MIN { -1284.f, -359.f, -685.f }
#define SM64_SPLASH_HOLD_MAX { 1462.f, 841.f, 209.f }
#define SM64_TITLE_EYE_OUT_M 0.30f
#define SM64_TITLE_EYE_MIN_DIST_M 0.55f
#define SM64_TITLE_EYE_TAU_S 0.33f
#define SM64_TITLE_EYE_DEFAULT_HEAD { 0.f, 0.8f, 1.9f }
void sm64_title_eye_target(const float head[3], float out[3]);
typedef struct sm64_title_place { int have, pending; float pos[3]; } sm64_title_place;
void sm64_title_place_reset(sm64_title_place *p);
void sm64_title_place_request(sm64_title_place *p);
int  sm64_title_place_step(sm64_title_place *p, const float head[3], float out[3]);
typedef struct sm64_title_eye { int have; float pos[3]; } sm64_title_eye;
void sm64_title_eye_reset(sm64_title_eye *e);
void sm64_title_eye_seed(sm64_title_eye *e, const float at[3]);
void sm64_title_eye_step_to(sm64_title_eye *e, const float place[3], float dt_s, float out[3]);
#define SM64_LETTER_WIDTH_M 0.30f
#define SM64_LETTER_BOX { 26.f / 160.f - 1.f, 1.f - 203.f / 120.f, 169.f / 160.f - 1.f, 1.f - 40.f / 120.f }
#define SM64_LETTER_LIFT_M 0.06f
#define SM64_LETTER_MAX_ABOVE_M 0.45f
void sm64_letter_anchor(const float focus[3], const float mn[3], const float mx[3], float ground_y, float upm, float out[3]);
typedef struct sm64_solid { float x0, z0, x1, z1, top; } sm64_solid;
#define SM64_LETTER_CLEAR_M 0.02f
#define SM64_LETTER_PUSH_STEP_M 0.01f
#define SM64_LETTER_PUSH_MAX_M 1.5f
enum { SM64_LETTER_CLEARED = 0, SM64_LETTER_FALLBACK = 1 };
float sm64_letter_clear(const float start[3], const float toward_xz[2], float upm, const float mn[3], const float mx[3],
                        const sm64_solid *s, int n, float out[3], int *how);
int sm64_ray_box(const float o[3], const float d[3], const float mn[3], const float mx[3], float hit[3]);
/* The act selector reads A only once its timer reaches 11 (star_select.c, levels/menu/script.c): measured, a star set
 * 17 ticks after the screen opens is taken, 14 is not. The file select takes no click within the cursor's click
 * animation (sCursorClickingTimer). */
#define SM64_ACT_TAP_READY_TICKS 20
#define SM64_FILE_TAP_GAP_TICKS 6
#define SM64_TAP_WAIT_MAX_TICKS 300
int sm64_menu_tap_ready(int screen, unsigned screen_ticks, unsigned since_tap_a);
typedef struct sm64_tap_queue {
    int pending, staged, screen;
    unsigned wait, since_a;
    float ndc[2];
} sm64_tap_queue;
enum { SM64_TAP_IDLE = 0, SM64_TAP_PRESS_A = 1, SM64_TAP_PICK = 2 };
void sm64_tap_queue_reset(sm64_tap_queue *q);
int sm64_tap_queue_step(sm64_tap_queue *q, int tapped, const float ndc[2], int screen, unsigned screen_ticks,
                        const char *drops[2], int *ndrops);
void sm64_tap_queue_staged(sm64_tap_queue *q);
void sm64_splash_level(float mn[3], float mx[3], float *ground_y, float *upm);
typedef struct sm64_title_fit {
    int run, seen, frozen, last;
    float mn[3], mx[3];
    float last_mn[3], last_mx[3];
} sm64_title_fit;
void sm64_title_fit_reset(sm64_title_fit *f);
void sm64_title_fit_step(sm64_title_fit *f, int have, const float mn[3], const float mx[3]);
int sm64_title_level(const sm64_title_fit *f, float mn[3], float mx[3], float *ground_y, float *upm);
/* A title frame counts for the fit only once its WORLD AABB covers level x = z = 0 (the table centre): the Mario head flies
 * in from z = -20000 over its first ~20 ticks (measured) and the fit is the head standing there, not its flight. */
int sm64_title_box_over_centre(const float mn[3], const float mx[3]);
/* sCursorPos: x*160, y*120, clamped to its own +-132 / +-90 range (file_select.c). */
void sm64_menu_cursor_from_ndc(const float ndc[2], float out[2]);
/* The act selector's star `index` of `visible` (star_select.c:167: x = 75 - 75*visible + 152*index, y 248, z -300; the
 * menu camera at z 1000, fovy 45, 320x240), as screen NDC; and the star under a tap (within +-16 px across, +-24 px up and
 * down of its centre; the pitch is 33.87 px, so neighbours never overlap), -1 for none. */
void sm64_act_star_ndc(int index, int visible, float out[2]);
int  sm64_act_star_at(const float ndc[2], int visible);
#ifdef __cplusplus
}
#endif

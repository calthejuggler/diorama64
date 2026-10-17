#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_POPUP_NONE = 0, SM64_POPUP_COINS = 1, SM64_POPUP_LIVES = 2 };
#define SM64_POPUP_TICKS 45
#define SM64_POPUP_FADE_TICKS 15
#define SM64_POPUP_X 140
#define SM64_POPUP_Y 120
#define SM64_HUD_METER_HEIGHT 230.f    /* level units above Mario's feet (Mario is ~160 units, 2 cm, tall) */
#define SM64_HUD_METER_WIDTH_M 0.024f
#define SM64_HUD_METER_MIN_DEG 1.5f
#define SM64_HUD_POPUP_HEIGHT 330.f
#define SM64_HUD_POPUP_RISE 120.f
#define SM64_HUD_POPUP_WIDTH_M 0.03f
#define SM64_HUD_POPUP_MIN_DEG 1.8f
#define SM64_HUD_GROUP_TIMER 7          /* == GFX_AR_GROUP_TIMER (0010-HUD-menus-and-screen-scenes.patch) */
/* == GFX_AR_GROUP_* (ar_hud.c asserts them), for code and tests without game headers. */
enum { SM64_HUD_GROUP_MISC = 1, SM64_HUD_GROUP_COUNTERS = 2, SM64_HUD_GROUP_POWER_METER = 3, SM64_HUD_GROUP_DIALOG = 4,
       SM64_HUD_GROUP_PAUSE = 5, SM64_HUD_GROUP_LABELS = 6, SM64_HUD_GROUP_MENU = 8, SM64_HUD_GROUP_CURSOR = 10,
       SM64_HUD_GROUP_GAMEOVER = 11 };
/* == GFX_AR_GROUP_PIP (0010-HUD-menus-and-screen-scenes.patch) */
enum { SM64_HUD_GROUP_PIP = 12 };
float sm64_hud_size_factor(float units_per_meter);
float sm64_hud_height(float units_at_8000, float units_per_meter);
#define SM64_HUD_TIMER_HEIGHT 560.f
#define SM64_HUD_TIMER_WIDTH_M 0.04f
#define SM64_HUD_TIMER_MIN_DEG 2.0f
#define SM64_METER_HOLD_TICKS 90
#define SM64_METER_FADE_TICKS 15
#define SM64_METER_LOW_WEDGES 2
typedef struct sm64_meter_state { int wedges, hold, primed; } sm64_meter_state;
void  sm64_meter_step(sm64_meter_state *s, int wedges);
float sm64_meter_alpha(const sm64_meter_state *s);
#define SM64_METER_BOX_UNITS 220.f
#define SM64_METER_CLEAR_GAP 20.f
float sm64_dialog_lift_above_meter(float lift, int meter_shown, float units_per_meter);
#define SM64_SCORE_WIDTH_M 0.50f
#define SM64_SCORE_MIN_DEG 20.f
#define SM64_SCORE_BOX { -1.f, 0.70f, 1.f, 1.f }   /* NDC: vanilla's HUD row (y 204..240 of 240: icons and digits 209..225) */
int   sm64_score_anchor(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], float out[3],
                        int *capped);
#define SM64_SCORE_SEATED_WIDTH_M 0.70f
#define SM64_SCORE_SEATED_LIFT_M 0.40f
#define SM64_SCORE_SEATED_GAP_M 0.40f
#define SM64_SCORE_SEATED_HEAD_CLEAR_M 0.55f
#ifdef __cplusplus   /* checked by every C++ includer (C has no float constant asserts) */
static_assert(SM64_SCORE_SEATED_GAP_M > 0.5f * SM64_SCORE_SEATED_WIDTH_M, "the seated score's gap must exceed its half width");
#endif
typedef struct sm64_score_place { float anchor[3]; float width_m; int capped; int fell_back; } sm64_score_place;
int   sm64_score_placement(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], int seated,
                           sm64_score_place *out);
#define SM64_SCORE_GLIDE_TAU_S 0.17f
#define SM64_SCORE_FALLBACK_HYST_M 0.10f
typedef struct sm64_score_glide { int have, seated, above; float pos[3], mn[3], mx[3], upm; } sm64_score_glide;
void  sm64_score_glide_reset(sm64_score_glide *g);
int   sm64_score_glide_step(sm64_score_glide *g, const float mn[3], const float mx[3], float ground_y, float upm,
                            const float head[3], int seated, float dt_s, sm64_score_place *out);
int   sm64_dialog_hides_score(const float dlg[3], float dlg_w_m, float dlg_h_m, const float score[3], float score_w_m,
                              float score_h_m, float upm);
float sm64_panel_height_m(float width_m, const float box[4]);
void  sm64_ar_hud_reset(void);
typedef struct sm64_popup { int kind, delta, total, age; } sm64_popup;
typedef struct sm64_hud_state { int coins, lives, primed; sm64_popup popup; } sm64_hud_state;
void  sm64_hud_step(sm64_hud_state *s, int coins, int lives);
/* print_text string in the HUD font: '$' is the plus glyph, '+' the coin, '(' Mario's head. 0 if nothing is shown. */
int   sm64_popup_text(const sm64_popup *p, char *buf, int cap);
float sm64_popup_alpha(const sm64_popup *p);
float sm64_popup_rise(const sm64_popup *p);
/* Steps the pop-ups when the HUD hook did not run this tick (HUD_DISPLAY_NONE), so gains during cutscenes start on
 * time. */
void sm64_ar_hud_end_tick(void);
int  sm64_ar_hud_popup(sm64_popup *out);
/* Mario's action constants (sm64.h; checked by _Static_assert in ar_hud.c). */
#define SM64_ACT_READING_AUTOMATIC_DIALOG 0x20001305u
#define SM64_ACT_READING_NPC_DIALOG 0x20001306u
#define SM64_ACT_READING_SIGN 0x00001308u
#define SM64_ACT_WAITING_FOR_DIALOG 0x0000130Au
enum { SM64_SPEAKER_MARIO = 0, SM64_SPEAKER_USED_OBJ = 1, SM64_SPEAKER_CUTSCENE_FOCUS = 2, SM64_SPEAKER_CONTINUE_OBJ = 3 };
int sm64_dialog_speaker(uint32_t mario_action, int used_obj_active, int cutscene_focus_active, int continue_obj_active);
/* The speaker is latched when a dialog opens. Object slots are reused (warps, forced level picks), so the latch keeps the
 * speaker's behaviour too: the latched object counts only while it is active with that same behaviour. */
typedef struct sm64_dialog_latch { int open, kind; const void *obj, *behavior; } sm64_dialog_latch;
int sm64_dialog_latch_step(sm64_dialog_latch *l, int dialog_open, int kind, const void *obj, const void *behavior,
                           int latched_active, const void *latched_behavior);
void sm64_dialog_latch_reset(sm64_dialog_latch *l);
void sm64_dialog_box_ndc(int left_offset, int width, int lines_per_box, int min_width, float out[4]);
#define SM64_DIALOG_MARGIN 60.f
#define SM64_DIALOG_BOX_MARGIN_PX 6.f
#define SM64_DIALOG_WIDTH_M 0.20f
#define SM64_DIALOG_MIN_DEG 9.f
#define SM64_DIALOG_LARGE_FACTOR 1.5f
#define SM64_SPEAKER_TOP_MARIO 180.f
#define SM64_SPEAKER_TOP_SIGN 150.f
#define SM64_SPEAKER_TOP_KING 500.f
#define SM64_SPEAKER_TOP_DEFAULT 160.f
#define SM64_DIALOG_MAX_ABOVE 800.f
#define SM64_DIALOG_OFF_TABLE 1600.f
float sm64_dialog_anchor_lift(float speaker_top, float units_per_meter);
int sm64_ar_hud_dialog_speaker(const void **obj);
float sm64_ar_hud_letter_slide(int *how);
#ifdef __cplusplus
}
#endif

#include <string.h>
#include "sm64.h"
#include "game/level_update.h"
#include "game/game_init.h"
#include "game/hud.h"
#include "game/print.h"
#include "game/camera.h"
#include "game/object_helpers.h"
#include "game/ingame_menu.h"
#include "pc/dialog_table.h"
#include "behavior_table.h"
#include "dialog_ids.h"
#include "pc/gfx/gfx_ar_hooks.h"
#include "fc_builder.h"
#include "sm64_ar_hud.h"
#include "sm64_ar_internal.h"
#include "sm64_story.h"
#include "sm64_host.h"
#include "sm64_settings.h"
#include "sm64_ar_view.h"
#include <stdlib.h>

extern void render_dl_power_meter(s16 numHealthWedges);   /* src/game/hud.c (not in hud.h) */
extern void render_hud_mario_lives(void);
extern void render_hud_coins(void);
extern void render_hud_stars(void);
extern void render_hud_timer(void);
extern u8 gOverrideHideHud;
extern bool gDjuiInMainMenu;
_Static_assert(SM64_HUD_GROUP_TIMER == GFX_AR_GROUP_TIMER, "timer group id");
_Static_assert(SM64_HUD_GROUP_MISC == GFX_AR_GROUP_MISC && SM64_HUD_GROUP_COUNTERS == GFX_AR_GROUP_COUNTERS &&
               SM64_HUD_GROUP_POWER_METER == GFX_AR_GROUP_POWER_METER && SM64_HUD_GROUP_DIALOG == GFX_AR_GROUP_DIALOG &&
               SM64_HUD_GROUP_PAUSE == GFX_AR_GROUP_PAUSE && SM64_HUD_GROUP_LABELS == GFX_AR_GROUP_LABELS &&
               SM64_HUD_GROUP_MENU == GFX_AR_GROUP_MENU && SM64_HUD_GROUP_CURSOR == GFX_AR_GROUP_CURSOR &&
               SM64_HUD_GROUP_GAMEOVER == GFX_AR_GROUP_GAMEOVER && SM64_HUD_GROUP_PIP == GFX_AR_GROUP_PIP,
               "group ids");

_Static_assert(SM64_ACT_READING_SIGN == ACT_READING_SIGN, "sm64.h");
_Static_assert(SM64_ACT_READING_NPC_DIALOG == ACT_READING_NPC_DIALOG, "sm64.h");
_Static_assert(SM64_ACT_WAITING_FOR_DIALOG == ACT_WAITING_FOR_DIALOG, "sm64.h");
_Static_assert(SM64_ACT_READING_AUTOMATIC_DIALOG == ACT_READING_AUTOMATIC_DIALOG, "sm64.h");

static sm64_hud_state sHud;
static sm64_meter_state sMeter;
static int sSteppedThisTick;

static void step(void) {
    sm64_hud_step(&sHud, gMarioStates[0].numCoins, gMarioStates[0].numLives);
    sm64_meter_step(&sMeter, gHudDisplay.wedges);
    sSteppedThisTick = 1;
}
void sm64_ar_hud_end_tick(void) { if (!sSteppedThisTick) { step(); } sSteppedThisTick = 0; }
int sm64_ar_hud_popup(sm64_popup *out) { *out = sHud.popup; return sHud.popup.kind != SM64_POPUP_NONE; }

static int sLetterOpen;
static sm64_score_glide sScoreGlide;
static uint32_t sScoreFrame;
static int sScoreStepped;
void sm64_ar_hud_reset(void) {
    memset(&sHud, 0, sizeof sHud); memset(&sMeter, 0, sizeof sMeter); sSteppedThisTick = 0; sLetterOpen = 0;
    sm64_score_glide_reset(&sScoreGlide); sScoreStepped = 0; sScoreFrame = 0;
}
static int meter_shown(void) {
    const s16 f = gHudDisplay.flags;
    return !gDjuiInMainMenu && !gOverrideHideHud && (f & HUD_DISPLAY_FLAG_CAMERA_AND_POWER) && (f & HUD_DISPLAY_FLAG_POWER) &&
           sm64_meter_alpha(&sMeter) > 0.f;
}

static int paused(void) { return sCurrPlayMode == PLAY_MODE_PAUSED; }
static int score_floats(void) { return sm64_settings_choice("score_float") == 0 && sm64_host_screen() == SM64_SCREEN_LEVEL; }

void sm64_ar_hud_render(void) {
    const s16 flags = gHudDisplay.flags;
    step();
    if (gDjuiInMainMenu || gOverrideHideHud) { return; }    /* vanilla's showHud (hud.c render_hud) */
    if (flags & HUD_DISPLAY_FLAG_TIMER) {
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_BEGIN, GFX_AR_GROUP_TIMER);
        render_hud_timer();
        render_text_labels();
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_END, GFX_AR_GROUP_TIMER);
    }
    const int floating = score_floats();
    if (floating) {
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_BEGIN, GFX_AR_GROUP_COUNTERS);
        if (flags & HUD_DISPLAY_FLAG_LIVES) { render_hud_mario_lives(); }
        if (flags & HUD_DISPLAY_FLAG_COIN_COUNT) { render_hud_coins(); }
        if (flags & HUD_DISPLAY_FLAG_STAR_COUNT) { render_hud_stars(); }
        render_text_labels();
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_END, GFX_AR_GROUP_COUNTERS);
    }
    if (paused()) {
        if (!floating) {
            GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_BEGIN, GFX_AR_GROUP_COUNTERS);
            if (flags & HUD_DISPLAY_FLAG_LIVES) { render_hud_mario_lives(); }
            if (flags & HUD_DISPLAY_FLAG_COIN_COUNT) { render_hud_coins(); }
            if (flags & HUD_DISPLAY_FLAG_STAR_COUNT) { render_hud_stars(); }
            GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_END, GFX_AR_GROUP_COUNTERS);
        }
    } else if (sHud.popup.kind != SM64_POPUP_NONE) {
        char text[24];
        if (sm64_popup_text(&sHud.popup, text, (int)sizeof text)) { print_text(SM64_POPUP_X, SM64_POPUP_Y, text); }
    }
    if (meter_shown()) {
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_BEGIN, GFX_AR_GROUP_POWER_METER);
        render_dl_power_meter(gHudDisplay.wedges);
        GFX_AR_EMIT_TAG(GFX_AR_TAG_GROUP_END, GFX_AR_GROUP_POWER_METER);
    }
}

static sm64_dialog_latch sLatch;
static int sLatchLevel = -1, sLatchArea = -1;
int sm64_ar_hud_dialog_speaker(const void **obj) { if (obj) { *obj = sLatch.obj; } return sLatch.kind; }
static int obj_active(const struct Object *o) { return o && (o->activeFlags & ACTIVE_FLAG_ACTIVE); }
static float speaker_top(const struct Object *o) {
    const int id = o->behavior ? (int)get_id_from_behavior(o->behavior) : -1;
    if (id == (int)id_bhvMessagePanel) { return SM64_SPEAKER_TOP_SIGN; }
    if (id == (int)id_bhvKingBobomb) { return SM64_SPEAKER_TOP_KING; }
    return o->hitboxHeight > 0.f ? o->hitboxHeight : SM64_SPEAKER_TOP_DEFAULT;
}

static void set_group(fc_builder *b, uint8_t id, uint8_t anchor, float alpha, const float *pos, float width_m, float min_deg,
                      uint8_t flags, const float *box) {
    fc_overlay_group g;
    memset(&g, 0, sizeof g);
    g.id = id; g.anchor = anchor; g.alpha = alpha; g.flags = flags;
    if (pos) { g.world_pos[0] = pos[0]; g.world_pos[1] = pos[1]; g.world_pos[2] = pos[2]; }
    g.width_m = width_m;
    g.min_angle_rad = min_deg * 0.0174532925f;
    if (box) { g.box[0] = box[0]; g.box[1] = box[1]; g.box[2] = box[2]; g.box[3] = box[3]; }
    fc_builder_set_group(b, &g);
}

/* Forgotten when the letter closes, on any other screen and at a level load, so a letter left mid-way never lends its
 * focus to the next one. */
static unsigned sLetterInits;
static float sLetterFocus[3];
static int sLetterFrontDone, sLetterFrontHow;
static float sLetterFront[3], sLetterFrontSlide;
static void letter_front(const float a[3], const float mn[3], const float mx[3], float upm, float out[3]) {
    if (!sLetterFrontDone) {
        float head[3], q[4];
        sm64_ar_view_get_head(head, q);
        const float toward[2] = { head[0] - a[0], head[2] - a[2] };
        sm64_solid *s = NULL;
        const int n = sm64_host_static_solids(a[1] - SM64_LETTER_CLEAR_M * upm, &s);
        if (n >= 0) {
            sLetterFrontSlide = sm64_letter_clear(a, toward, upm, mn, mx, s, n, sLetterFront, &sLetterFrontHow);
        } else {
            for (int k = 0; k < 3; k++) { sLetterFront[k] = a[k]; }
            sLetterFrontSlide = -1.f; sLetterFrontHow = SM64_LETTER_FALLBACK;
        }
        free(s);
        sLetterFrontDone = 1;
    }
    for (int k = 0; k < 3; k++) { out[k] = sLetterFront[k]; }
}
float sm64_ar_hud_letter_slide(int *how) { if (how) { *how = sLetterFrontHow; } return sLetterFrontDone ? sLetterFrontSlide : -2.f; }
static uint8_t dialog_style(void) {
    return sm64_settings_choice("dialog_plate") == 0 ? (uint8_t)(FC_GROUP_BACKING | FC_GROUP_BACKING_WHITE | FC_GROUP_INK) : (uint8_t)0;
}
static uint8_t box_style(void) {
    return (uint8_t)(dialog_style() & (sm64_host_dialog_inverted() ? (uint8_t)~FC_GROUP_INK : (uint8_t)0xFF));
}
static int sDlgShown;
static float sDlgAnchor[3], sDlgW, sDlgH;
static void note_dialog(const float a[3], float w_m, float h_m) {
    sDlgShown = 1; sDlgW = w_m; sDlgH = h_m;
    for (int k = 0; k < 3; k++) { sDlgAnchor[k] = a[k]; }
}
static float dialog_scale(void) { return sm64_settings_choice("dialog_size") == 1 ? SM64_DIALOG_LARGE_FACTOR : 1.f; }
static void set_dialog_group(fc_builder *b, const struct MarioState *m, const float base[3], int paused_now) {
    sDlgShown = 0;
    if (sLetterOpen && sLetterInits != sm64_host_level_inits()) { sLetterOpen = 0; }
    if (sm64_host_story() && get_dialog_id() == DIALOG_020) {
        if (!sLetterOpen) {
            for (int k = 0; k < 3; k++) { sLetterFocus[k] = gLakituState.focus[k]; }
            sLetterOpen = 1; sLetterInits = sm64_host_level_inits(); sLetterFrontDone = 0;
        }
        float mn[3], mx[3], a[3];
        sm64_ar_level_bounds(mn, mx);
        sm64_letter_anchor(sLetterFocus, mn, mx, sm64_ar_ground_y(), sm64_ar_units_per_meter(), a);
        if (sm64_settings_choice("letter_front") == 0) { letter_front(a, mn, mx, sm64_ar_units_per_meter(), a); }
        static const float kLetterBox[4] = SM64_LETTER_BOX;
        const uint8_t style = dialog_style() & (uint8_t)~FC_GROUP_INK;
        set_group(b, GFX_AR_GROUP_DIALOG, FC_ANCHOR_WORLD_POINT, 1.f, a, SM64_LETTER_WIDTH_M, 0.f,
                  FC_GROUP_YAW_ONLY | FC_GROUP_ON_TOP | style | (style ? FC_GROUP_FIXED_BOX : 0), style ? kLetterBox : NULL);
        note_dialog(a, SM64_LETTER_WIDTH_M, style ? sm64_panel_height_m(SM64_LETTER_WIDTH_M, kLetterBox) : SM64_LETTER_WIDTH_M);
        return;
    }
    sLetterOpen = 0;
    const int open = get_dialog_id() != DIALOG_NONE;
    if (gCurrLevelNum != sLatchLevel || gCurrAreaIndex != sLatchArea) {
        sm64_dialog_latch_reset(&sLatch); sLatchLevel = gCurrLevelNum; sLatchArea = gCurrAreaIndex;
    }
    struct Object *focus = gCutsceneFocus, *cont = gContinueDialogFunctionObject;
    const int kind = sm64_dialog_speaker(m->action, obj_active(m->usedObj), obj_active(focus) && gCamera && gCamera->cutscene != 0,
                                         obj_active(cont));
    const struct Object *cand = kind == SM64_SPEAKER_USED_OBJ ? m->usedObj : kind == SM64_SPEAKER_CUTSCENE_FOCUS ? focus
                              : kind == SM64_SPEAKER_CONTINUE_OBJ ? cont : NULL;
    const struct Object *held = (const struct Object *)sLatch.obj;
    const int k = sm64_dialog_latch_step(&sLatch, open, kind, cand, cand ? (const void *)cand->behavior : NULL, obj_active(held),
                                         held ? (const void *)held->behavior : NULL);
    const struct Object *sp = k == SM64_SPEAKER_MARIO ? NULL : (const struct Object *)sLatch.obj;
    if (sp == m->marioObj) { sp = NULL; }
    float a[3] = { base[0], base[1], base[2] }, top = SM64_SPEAKER_TOP_MARIO;
    if (sp) { a[0] = sp->header.gfx.pos[0]; a[1] = sp->header.gfx.pos[1]; a[2] = sp->header.gfx.pos[2]; top = speaker_top(sp); }
    const float upm = sm64_ar_units_per_meter();
    a[1] += sp ? sm64_dialog_anchor_lift(top, upm)
               : sm64_dialog_lift_above_meter(sm64_dialog_anchor_lift(top, upm), meter_shown(), upm);
    float mn[3], mx[3];
    sm64_ar_level_bounds(mn, mx);
    if (a[1] > mx[1] + SM64_DIALOG_MAX_ABOVE) { a[1] = mx[1] + SM64_DIALOG_MAX_ABOVE; }
    const float off = SM64_DIALOG_OFF_TABLE;
    const int on_table = mx[0] > mn[0] && a[0] > mn[0] - off && a[0] < mx[0] + off && a[2] > mn[2] - off && a[2] < mx[2] + off;
    struct DialogEntry *de = open ? dialog_table_get(get_dialog_id()) : NULL;
    if (open && de && on_table && !paused_now && !gOverrideDialogPos) {
        float box[4];
        sm64_dialog_box_ndc(de->leftOffset, de->width, de->linesPerBox, gDialogMinWidth, box);
        const float w = SM64_DIALOG_WIDTH_M * sm64_hud_size_factor(upm) * dialog_scale();
        set_group(b, GFX_AR_GROUP_DIALOG, FC_ANCHOR_WORLD_POINT, 1.f, a, w, SM64_DIALOG_MIN_DEG * dialog_scale(),
                  FC_GROUP_YAW_ONLY | FC_GROUP_ON_TOP | FC_GROUP_FIXED_BOX | box_style(), box);
        note_dialog(a, w, sm64_panel_height_m(w, box));
    } else {
        set_group(b, GFX_AR_GROUP_DIALOG, FC_ANCHOR_PANEL, 1.f, NULL, 0.f, 0.f, 0, NULL);
    }
}

static void screen_frame(fc_builder *b, int scr) {
    /* GAMEOVER first: world-point groups draw in the order their entries were first set, and the game-over panel (depth
     * tested, not ON_TOP) must be drawn before the on-top PRESS START label and the hand cursor, or it paints over them. */
    static const uint8_t kOthers[] = { GFX_AR_GROUP_GAMEOVER, GFX_AR_GROUP_DIALOG, GFX_AR_GROUP_PAUSE, GFX_AR_GROUP_COUNTERS,
                                       GFX_AR_GROUP_POWER_METER, GFX_AR_GROUP_TIMER, GFX_AR_GROUP_LABELS, GFX_AR_GROUP_CREDITS };
    for (size_t i = 0; i < sizeof kOthers; i++) { set_group(b, kOthers[i], FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL); }
    sLetterOpen = 0;
    if (sm64_screen_is_menu(scr)) {
        static const float kFull[4] = { -1.f, -1.f, 1.f, 1.f };
        float a[3];
        sm64_ar_menu_anchor(a);
        const int plate = sm64_settings_choice("menu_backdrop") == 0;
        const uint8_t fl = FC_GROUP_YAW_ONLY | FC_GROUP_ON_TOP | FC_GROUP_FIXED_BOX | (plate ? FC_GROUP_BACKING : 0) |
                           (plate && sm64_settings_choice("menu_backdrop_white") == 0 ? FC_GROUP_BACKING_WHITE : 0);
        set_group(b, GFX_AR_GROUP_MENU, FC_ANCHOR_WORLD_POINT, 1.f, a, sm64_ar_menu_width_m(), 0.f, fl, kFull);
    } else {
        set_group(b, GFX_AR_GROUP_MENU, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
    float mn[3], mx[3];
    if (scr == SM64_SCREEN_TITLE && sm64_ar_title_box(mn, mx)) {
        const float upm = sm64_ar_units_per_meter();
        const float p[3] = { 0.5f * (mn[0] + mx[0]), mx[1] + 0.03f * upm, 0.5f * (mn[2] + mx[2]) };
        set_group(b, GFX_AR_GROUP_LABELS, FC_ANCHOR_WORLD_POINT, 1.f, p, SM64_TITLE_TEXT_WIDTH_M, 0.f,
                  FC_GROUP_YAW_ONLY | FC_GROUP_ON_TOP, NULL);
    }
    if (sm64_gameover_panel_shown(sm64_host_story(), scr, sm64_host_title_gameover(), sm64_settings_choice("gameover_text")) &&
        sm64_ar_title_box(mn, mx)) {
        static const float kAll[4] = { -1.f, -1.f, 1.f, 1.f };
        float p[3];
        sm64_gameover_anchor(mn, mx, sm64_ar_units_per_meter(), p);
        set_group(b, GFX_AR_GROUP_GAMEOVER, FC_ANCHOR_WORLD_POINT, 1.f, p, SM64_GAMEOVER_WIDTH_M, 0.f,
                  FC_GROUP_YAW_ONLY | FC_GROUP_FIXED_BOX, kAll);
    }
    set_group(b, GFX_AR_GROUP_CURSOR, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
}

void sm64_ar_hud_frame(fc_builder *b) {
    const int scr = sm64_host_screen();
    if (scr != SM64_SCREEN_LEVEL && scr != SM64_SCREEN_CREDITS) { sm64_score_glide_reset(&sScoreGlide); screen_frame(b, scr); return; }
    set_group(b, GFX_AR_GROUP_MENU, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    set_group(b, GFX_AR_GROUP_CURSOR, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    set_group(b, GFX_AR_GROUP_GAMEOVER, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    if (scr == SM64_SCREEN_CREDITS) {
        float a[3];
        sm64_credits_anchor(sm64_ar_ground_y(), sm64_ar_units_per_meter(), a);
        set_group(b, GFX_AR_GROUP_CREDITS, FC_ANCHOR_WORLD_POINT, 1.f, a, SM64_CREDITS_WIDTH_M, 0.f, FC_GROUP_YAW_ONLY | FC_GROUP_ON_TOP, NULL);
    } else {
        set_group(b, GFX_AR_GROUP_CREDITS, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
    const int floating = score_floats();
    int score_shown = 0;
    float score_at[3] = { 0.f, 0.f, 0.f }, score_w = 0.f, score_hh = 0.f;
    if (floating) {   /* the floating score, set before the dialog so an on-top dialog is drawn over it (first-set order) */
        static const float kBox[4] = SM64_SCORE_BOX;
        float mn[3], mx[3], head[3], q[4];
        sm64_score_place sp;
        sm64_ar_level_bounds(mn, mx);
        sm64_ar_view_get_head(head, q);
        const uint32_t fr = sm64_host_frame_count();
        const int new_tick = !sScoreStepped || fr != sScoreFrame;
        sScoreStepped = 1; sScoreFrame = fr;
        const int seated = sm64_settings_choice("score_seated") == 0;
        const int ok = sm64_score_glide_step(&sScoreGlide, mn, mx, sm64_ar_ground_y(), sm64_ar_units_per_meter(), head,
                                             seated, new_tick ? 1.f / 30.f : 0.f, &sp);
        const float score_h = sp.width_m * ((kBox[3] - kBox[1]) * 120.f) / ((kBox[2] - kBox[0]) * 160.f);
        if (ok && !sm64_ar_pip_hides(sp.anchor, sp.width_m, score_h)) {
            set_group(b, GFX_AR_GROUP_COUNTERS, FC_ANCHOR_WORLD_POINT, 1.f, sp.anchor, sp.width_m, SM64_SCORE_MIN_DEG,
                      FC_GROUP_YAW_ONLY | FC_GROUP_FIXED_BOX, kBox);
            score_shown = seated;
            for (int k = 0; k < 3; k++) { score_at[k] = sp.anchor[k]; }
            score_w = sp.width_m; score_hh = score_h;
        } else {
            set_group(b, GFX_AR_GROUP_COUNTERS, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
        }
    } else {
        sm64_score_glide_reset(&sScoreGlide);
    }
    struct MarioState *m = &gMarioStates[0];
    float base[3] = { m->pos[0], m->pos[1], m->pos[2] };
    if (m->marioObj) { base[0] = m->marioObj->header.gfx.pos[0]; base[1] = m->marioObj->header.gfx.pos[1]; base[2] = m->marioObj->header.gfx.pos[2]; }
    const int p = paused();
    set_dialog_group(b, m, base, p);
    if (score_shown && sDlgShown &&
        sm64_dialog_hides_score(sDlgAnchor, sDlgW, sDlgH, score_at, score_w, score_hh, sm64_ar_units_per_meter())) {
        set_group(b, GFX_AR_GROUP_COUNTERS, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
    set_group(b, GFX_AR_GROUP_PAUSE, FC_ANCHOR_PANEL, 1.f, NULL, 0.f, 0.f, 0, NULL);
    if (!floating) { set_group(b, GFX_AR_GROUP_COUNTERS, p ? FC_ANCHOR_PANEL : FC_ANCHOR_HIDDEN, 1.f, NULL, 0.f, 0.f, 0, NULL); }
    const float upm = sm64_ar_units_per_meter();
    const float wf = sm64_hud_size_factor(upm);
    const float meter[3] = { base[0], base[1] + sm64_hud_height(SM64_HUD_METER_HEIGHT, upm), base[2] };
    if (sm64_meter_alpha(&sMeter) > 0.f) {
        set_group(b, GFX_AR_GROUP_POWER_METER, FC_ANCHOR_WORLD_POINT, sm64_meter_alpha(&sMeter), meter, SM64_HUD_METER_WIDTH_M * wf,
                  SM64_HUD_METER_MIN_DEG, FC_GROUP_YAW_ONLY, NULL);
    } else {
        set_group(b, GFX_AR_GROUP_POWER_METER, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
    if (gHudDisplay.flags & HUD_DISPLAY_FLAG_TIMER) {
        const float t[3] = { base[0], base[1] + sm64_hud_height(SM64_HUD_TIMER_HEIGHT, upm), base[2] };
        set_group(b, GFX_AR_GROUP_TIMER, FC_ANCHOR_WORLD_POINT, 1.f, t, SM64_HUD_TIMER_WIDTH_M * wf, SM64_HUD_TIMER_MIN_DEG,
                  FC_GROUP_YAW_ONLY, NULL);
    } else {
        set_group(b, GFX_AR_GROUP_TIMER, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
    if (p) {
        set_group(b, GFX_AR_GROUP_LABELS, FC_ANCHOR_PANEL, 1.f, NULL, 0.f, 0.f, 0, NULL);
    } else if (sHud.popup.kind != SM64_POPUP_NONE) {
        const float pos[3] = { base[0], base[1] + sm64_hud_height(SM64_HUD_POPUP_HEIGHT + sm64_popup_rise(&sHud.popup), upm), base[2] };
        set_group(b, GFX_AR_GROUP_LABELS, FC_ANCHOR_WORLD_POINT, sm64_popup_alpha(&sHud.popup), pos,
                  SM64_HUD_POPUP_WIDTH_M * wf, SM64_HUD_POPUP_MIN_DEG, FC_GROUP_YAW_ONLY, NULL);
    } else {
        set_group(b, GFX_AR_GROUP_LABELS, FC_ANCHOR_HIDDEN, 0.f, NULL, 0.f, 0.f, 0, NULL);
    }
}

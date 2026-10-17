#include "sm64_ar_hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sm64_levels.h"
#include "ar_float_panel.h"
#include "sm64_pip.h"
void sm64_hud_step(sm64_hud_state *s, int coins, int lives) {
    if (s->popup.kind != SM64_POPUP_NONE && ++s->popup.age >= SM64_POPUP_TICKS) { s->popup.kind = SM64_POPUP_NONE; }
    if (!s->primed) { s->coins = coins; s->lives = lives; s->primed = 1; return; }
    if (lives > s->lives) {
        s->popup.kind = SM64_POPUP_LIVES; s->popup.delta = lives - s->lives; s->popup.total = lives; s->popup.age = 0;
    } else if (coins > s->coins) {
        const int more = s->popup.kind == SM64_POPUP_COINS ? s->popup.delta : 0;
        s->popup.kind = SM64_POPUP_COINS; s->popup.delta = more + (coins - s->coins); s->popup.total = coins; s->popup.age = 0;
    }
    s->coins = coins; s->lives = lives;
}
int sm64_popup_text(const sm64_popup *p, char *buf, int cap) {
    if (p->kind == SM64_POPUP_COINS) { snprintf(buf, (size_t)cap, "$%d +%d", p->delta, p->total); return 1; }
    if (p->kind == SM64_POPUP_LIVES) { snprintf(buf, (size_t)cap, "$%d (%d", p->delta, p->total); return 1; }
    return 0;
}
float sm64_popup_alpha(const sm64_popup *p) {
    if (p->kind == SM64_POPUP_NONE) { return 0.f; }
    const int fade_from = SM64_POPUP_TICKS - SM64_POPUP_FADE_TICKS;
    if (p->age < fade_from) { return 1.f; }
    const float a = 1.f - (float)(p->age - fade_from) / (float)SM64_POPUP_FADE_TICKS;
    return a > 0.f ? a : 0.f;
}
float sm64_popup_rise(const sm64_popup *p) { return SM64_HUD_POPUP_RISE * (float)p->age / (float)SM64_POPUP_TICKS; }
int sm64_dialog_speaker(uint32_t act, int used_obj_active, int focus_active, int continue_active) {
    const int reading = act == SM64_ACT_READING_SIGN || act == SM64_ACT_WAITING_FOR_DIALOG || act == SM64_ACT_READING_NPC_DIALOG;
    if (reading && used_obj_active) { return SM64_SPEAKER_USED_OBJ; }
    if (focus_active) { return SM64_SPEAKER_CUTSCENE_FOCUS; }
    if (continue_active) { return SM64_SPEAKER_CONTINUE_OBJ; }
    return SM64_SPEAKER_MARIO;
}
void sm64_dialog_latch_reset(sm64_dialog_latch *l) { l->open = 0; l->kind = SM64_SPEAKER_MARIO; l->obj = 0; l->behavior = 0; }
int sm64_dialog_latch_step(sm64_dialog_latch *l, int open, int kind, const void *obj, const void *behavior, int latched_active,
                           const void *latched_behavior) {
    if (!open) { sm64_dialog_latch_reset(l); return SM64_SPEAKER_MARIO; }
    if (!l->open) {
        l->open = 1;
        if (kind == SM64_SPEAKER_MARIO || !obj) { l->kind = SM64_SPEAKER_MARIO; l->obj = 0; l->behavior = 0; }
        else { l->kind = kind; l->obj = obj; l->behavior = behavior; }
        return l->kind;
    }
    if (l->kind != SM64_SPEAKER_MARIO && !(latched_active && latched_behavior == l->behavior)) {
        l->kind = SM64_SPEAKER_MARIO; l->obj = 0; l->behavior = 0;
    }
    return l->kind;
}
void sm64_dialog_box_ndc(int left, int width, int lines, int min_width, float out[4]) {
    const float m = SM64_DIALOG_BOX_MARGIN_PX;
    const float w = (float)min_width > 143.f ? (float)min_width : 143.f;   /* vertex_text_bg_box's 130 x 1.1 */
    const float x0 = (float)left - 7.f - m, x1 = (float)left - 7.f + w + m;   /* X_VAL1 = -7 */
    const float top = (float)width + 5.f, y1 = top + m, y0 = top - 80.f * ((float)lines / 5.f + 0.1f) - m;   /* Y_VAL1/2 = 5 */
    out[0] = x0 / 160.f - 1.f; out[1] = y0 / 120.f - 1.f; out[2] = x1 / 160.f - 1.f; out[3] = y1 / 120.f - 1.f;
}
static float metric_factor(float upm) { return upm > 0.f && upm < SM64_AR_UNITS_PER_METER ? upm / SM64_AR_UNITS_PER_METER : 1.f; }
float sm64_hud_size_factor(float upm) { return upm > SM64_AR_UNITS_PER_METER && isfinite(upm) ? SM64_AR_UNITS_PER_METER / upm : 1.f; }
float sm64_hud_height(float h, float upm) {
    return h <= SM64_MARIO_HEIGHT_UNITS ? h : SM64_MARIO_HEIGHT_UNITS + (h - SM64_MARIO_HEIGHT_UNITS) * metric_factor(upm);
}
float sm64_dialog_anchor_lift(float top, float upm) { return top + SM64_DIALOG_MARGIN * metric_factor(upm); }
float sm64_dialog_lift_above_meter(float lift, int meter_shown, float upm) {
    const float clear = sm64_hud_height(SM64_HUD_METER_HEIGHT + SM64_METER_BOX_UNITS + SM64_METER_CLEAR_GAP, upm);
    return meter_shown && lift < clear ? clear : lift;
}
static int score_anchor_with(const ar_float_panel_params *p, const float mn[3], const float mx[3], float ground_y, float upm,
                             const float head[3], float out[3], int *capped) {
    *capped = 0;
    out[0] = out[1] = out[2] = 0.f;
    if (!(mx[0] > mn[0]) || !(mx[2] > mn[2])) { return 0; }
    const int r = ar_float_panel_anchor(p, mn, mx, ground_y, upm, head, out);
    if (r == AR_FLOAT_PANEL_HIDDEN) { return 0; }
    *capped = r == AR_FLOAT_PANEL_BESIDE;
    return 1;
}
int sm64_score_anchor(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], float out[3],
                      int *capped) {
    static const ar_float_panel_params kP = AR_FLOAT_PANEL_DEFAULTS;
    return score_anchor_with(&kP, mn, mx, ground_y, upm, head, out, capped);
}
static int segment_meets_box(double x0, double z0, double x1, double z1, const float mn[3], const float mx[3]) {
    double t0 = 0.0, t1 = 1.0;
    const double p[4] = { -(x1 - x0), x1 - x0, -(z1 - z0), z1 - z0 };
    const double q[4] = { x0 - mn[0], mx[0] - x0, z0 - mn[2], mx[2] - z0 };
    for (int k = 0; k < 4; k++) {
        if (p[k] == 0.0) { if (q[k] < 0.0) { return 0; } continue; }
        const double r = q[k] / p[k];
        if (p[k] < 0.0) { if (r > t1) { return 0; } if (r > t0) { t0 = r; } }
        else { if (r < t0) { return 0; } if (r < t1) { t1 = r; } }
    }
    return 1;
}
static int score_placement_margin(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3],
                                  int seated, double margin, sm64_score_place *out) {
    memset(out, 0, sizeof *out);
    if (!seated) {
        out->width_m = SM64_SCORE_WIDTH_M;
        return sm64_score_anchor(mn, mx, ground_y, upm, head, out->anchor, &out->capped);
    }
    static const ar_float_panel_params kSeated = { 0.50f, 1.50f, SM64_SCORE_SEATED_GAP_M };
    out->width_m = SM64_SCORE_SEATED_WIDTH_M;
    if (!score_anchor_with(&kSeated, mn, mx, ground_y, upm, head, out->anchor, &out->capped)) { return 0; }
    if (!out->capped) { return 1; }
    const double u = upm > 0.f && isfinite(upm) ? upm : 1.0;
    out->anchor[1] = (float)(ground_y + SM64_SCORE_SEATED_LIFT_M * u);
    if (head && isfinite(head[0]) && isfinite(head[2])) {
        const double cx = 0.5 * ((double)mn[0] + mx[0]), cz = 0.5 * ((double)mn[2] + mx[2]);
        const double hx = head[0] - cx, hz = head[2] - cz, dh = sqrt(hx * hx + hz * hz);
        if (dh > 1e-3) {
            const double ux = hx / dh, uz = hz / dh;
            const double da = (out->anchor[0] - cx) * ux + (out->anchor[2] - cz) * uz;
            const double lim = dh - SM64_SCORE_SEATED_HEAD_CLEAR_M * u - margin;
            if (da > lim) {
                const double x = cx + ux * lim, z = cz + uz * lim;
                const double hw = 0.5 * SM64_SCORE_SEATED_WIDTH_M * u;
                if (lim <= 0.0 || segment_meets_box(x - uz * hw, z + ux * hw, x + uz * hw, z - ux * hw, mn, mx)) {
                    static const ar_float_panel_params kAbove = { 0.50f, 1e6f, SM64_SCORE_SEATED_GAP_M };
                    const int r = score_anchor_with(&kAbove, mn, mx, ground_y, upm, head, out->anchor, &out->capped);
                    out->fell_back = r;
                    return r;
                }
                out->anchor[0] = (float)x; out->anchor[2] = (float)z;
            }
        }
    }
    return 1;
}
int sm64_score_placement(const float mn[3], const float mx[3], float ground_y, float upm, const float head[3], int seated,
                         sm64_score_place *out) {
    return score_placement_margin(mn, mx, ground_y, upm, head, seated, 0.0, out);
}
void sm64_score_glide_reset(sm64_score_glide *g) { memset(g, 0, sizeof *g); }
int sm64_score_glide_step(sm64_score_glide *g, const float mn[3], const float mx[3], float ground_y, float upm,
                          const float head[3], int seated, float dt_s, sm64_score_place *out) {
    if (!seated) {
        sm64_score_glide_reset(g);
        return sm64_score_placement(mn, mx, ground_y, upm, head, 0, out);
    }
    const int same = g->have && g->seated == seated && g->upm == upm && memcmp(g->mn, mn, sizeof g->mn) == 0 &&
                     memcmp(g->mx, mx, sizeof g->mx) == 0;
    const double u = upm > 0.f && isfinite(upm) ? upm : 1.0;
    const double margin = same && g->above ? SM64_SCORE_FALLBACK_HYST_M * u : 0.0;
    if (!score_placement_margin(mn, mx, ground_y, upm, head, seated, margin, out)) { sm64_score_glide_reset(g); return 0; }
    const float a = same ? (isfinite(dt_s) && dt_s > 0.f ? 1.f - expf(-dt_s / SM64_SCORE_GLIDE_TAU_S) : 0.f) : 1.f;
    for (int k = 0; k < 3; k++) {
        g->pos[k] += (out->anchor[k] - g->pos[k]) * a;
        out->anchor[k] = g->pos[k];
        g->mn[k] = mn[k]; g->mx[k] = mx[k];
    }
    g->have = 1; g->seated = seated; g->upm = upm; g->above = out->fell_back;
    return 1;
}
int sm64_dialog_hides_score(const float dlg[3], float dlg_w_m, float dlg_h_m, const float score[3], float score_w_m,
                            float score_h_m, float upm) {
    return sm64_pip_panels_overlap(dlg, dlg_w_m, dlg_h_m, score, score_w_m, score_h_m, upm);
}
float sm64_panel_height_m(float width_m, const float box[4]) {
    const float w = (box[2] - box[0]) * 160.f;
    return w > 0.f ? width_m * (box[3] - box[1]) * 120.f / w : 0.f;
}
void sm64_meter_step(sm64_meter_state *s, int wedges) {
    if (!s->primed) { s->primed = 1; s->wedges = wedges; s->hold = 0; return; }
    if (wedges != s->wedges) { s->wedges = wedges; s->hold = SM64_METER_HOLD_TICKS; }
    else if (s->hold > 0) { s->hold--; }
}
float sm64_meter_alpha(const sm64_meter_state *s) {
    if (s->wedges <= 0) { return 0.f; }
    if (s->wedges <= SM64_METER_LOW_WEDGES) { return 1.f; }
    if (s->hold <= 0) { return 0.f; }
    if (s->hold >= SM64_METER_FADE_TICKS) { return 1.f; }
    return (float)s->hold / (float)SM64_METER_FADE_TICKS;
}

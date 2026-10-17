#include "sm64_look.h"
#include <math.h>
#include <string.h>
static float scale(float upm) { return upm > 0.f && isfinite(upm) ? upm : 8000.f; }
void sm64_look_clip(int floor_on, int box_on, float ground_y, float lowest_floor, const float mn[3], const float mx[3],
                    float upm, fc_look *out) {
    const float u = scale(upm);
    if (floor_on && isfinite(ground_y) && isfinite(lowest_floor)) {
        const float y = lowest_floor < ground_y ? lowest_floor : ground_y;
        out->flags |= FC_LOOK_CLIP_FLOOR;
        out->clip_floor_y = y - SM64_CLIP_FLOOR_MARGIN_M * u;
    }
    if (box_on && mn && mx && isfinite(mn[0]) && isfinite(mn[2]) && isfinite(mx[0]) && isfinite(mx[2]) &&
        mx[0] > mn[0] && mx[2] > mn[2]) {
        const float m = SM64_CLIP_BOX_MARGIN_M * u;
        out->flags |= FC_LOOK_CLIP_BOX;
        out->clip_lo[0] = mn[0] - m; out->clip_lo[1] = mn[2] - m;
        out->clip_hi[0] = mx[0] + m; out->clip_hi[1] = mx[2] + m;
    }
}
void sm64_look_water(const float *boxes5, int n, uint32_t frame, fc_look *out) {
    out->flags |= FC_LOOK_LIQUID;
    out->time_s = (float)(frame % 108000u) / 30.f;
    out->water_count = 0;
    for (int i = 0; boxes5 && i < n && out->water_count < FC_LOOK_MAX_WATER; i++) {
        const float *b = &boxes5[i * 5];
        if (!isfinite(b[0]) || !isfinite(b[1]) || !isfinite(b[2]) || !isfinite(b[3]) || !isfinite(b[4]) || !(b[2] > b[0]) || !(b[3] > b[1])) { continue; }
        fc_water_box *w = &out->water[out->water_count++];
        w->lo[0] = b[0]; w->lo[1] = b[1]; w->hi[0] = b[2]; w->hi[1] = b[3]; w->surface_y = b[4];
    }
}
void sm64_look_cut(int on, int has_mario, const float mario[3], float upm, fc_look *out) {
    if (on) { out->flags |= FC_LOOK_CUT_ARMED; }
    if (!on || !has_mario || !mario || !isfinite(mario[0]) || !isfinite(mario[1]) || !isfinite(mario[2])) { return; }
    out->flags |= FC_LOOK_CUTAWAY;
    out->cut_target[0] = mario[0]; out->cut_target[1] = mario[1] + SM64_CUT_MID_UNITS; out->cut_target[2] = mario[2];
    out->cut_radius = SM64_CUT_RADIUS_M * scale(upm);
}
void sm64_behind_walls(int choice, int *silhouette, int *cut) {
    const int c = choice >= SM64_BEHIND_SILHOUETTE && choice <= SM64_BEHIND_OFF ? choice : SM64_BEHIND_SILHOUETTE;
    if (silhouette) { *silhouette = c == SM64_BEHIND_SILHOUETTE || c == SM64_BEHIND_BOTH; }
    if (cut) { *cut = c == SM64_BEHIND_CUTAWAY || c == SM64_BEHIND_BOTH; }
}
void sm64_look_cut_prev(sm64_cut_state *s, uint32_t frame, fc_look *out) {
    if (!(out->flags & FC_LOOK_CUTAWAY)) { s->have = 0; s->have_prev = 0; s->frame = frame; return; }
    if (!s->have || frame != s->frame) {
        s->have_prev = s->have && frame != s->frame;
        for (int k = 0; k < 3; k++) { s->prev[k] = s->target[k]; }
        s->frame = frame; s->have = 1;
    }
    for (int k = 0; k < 3; k++) { s->target[k] = out->cut_target[k]; }
    if (!s->have_prev) { return; }
    float d2 = 0.f;
    for (int k = 0; k < 3; k++) { const float d = out->cut_target[k] - s->prev[k]; d2 += d * d; }
    if (!(d2 <= SM64_CUT_SNAP_UNITS * SM64_CUT_SNAP_UNITS)) { return; }
    out->flags |= FC_LOOK_CUT_PREV;
    for (int k = 0; k < 3; k++) { out->cut_target_prev[k] = s->prev[k]; }
}
int sm64_trans_due(sm64_trans_state *s, uint32_t frame) {
    if (s->stepped && s->frame == frame) { return 0; }
    s->frame = frame; s->stepped = 1;
    return 1;
}
static void trans_step(sm64_trans_state *s, const sm64_warp_sample *w);
void sm64_trans_step(sm64_trans_state *s, const sm64_warp_sample *w) {
    const float before = s->cover;
    trans_step(s, w);
    /* the renderer eases from prev to cover between ticks; a cover that jumps up (an arrival's FROM, the course's
     * first frame after the act selector) is drawn at once, never eased in from clear */
    s->prev = s->cover - before > SM64_TRANS_JUMP ? s->cover : before;
}
static void trans_step(sm64_trans_state *s, const sm64_warp_sample *w) {
    const int edge = w->ready && (!s->ready_seen || w->key != s->ready_key);
    if (w->ready) { s->ready_seen = 1; s->ready_key = w->key; } else { s->ready_seen = 0; }
    if (w->paused) { s->cover = 1.f; s->held = 0; s->latch = 0; return; }
    if (w->active && w->delay > 0) {
        if (edge) { s->latch = w->delay; }
        if (s->latch > 0) {
            const float c = 1.f - (float)w->delay / (float)(s->latch + 1);
            s->cover = c < 0.f ? 0.f : c;
            s->kind = w->type < 8 ? FC_TRANS_FADE : FC_TRANS_IRIS;
            memcpy(s->rgb, w->rgb, sizeof s->rgb);
            s->held = 0;
            return;
        }
    } else {
        s->latch = 0;
    }
    if (w->active && w->delay <= 0) {
        const int span = w->time > 2 ? w->time - 1 : 1;
        float p = (float)w->count / (float)span;
        p = p < 0.f ? 0.f : p > 1.f ? 1.f : p;
        s->kind = w->type < 8 ? FC_TRANS_FADE : FC_TRANS_IRIS;
        memcpy(s->rgb, w->rgb, sizeof s->rgb);
        s->cover = (w->type & 1) ? p : 1.f - p;
        s->held = 0;
        return;
    }
    if (w->active) { s->cover = 0.f; s->held = 0; return; }
    if (s->cover >= 0.999f && s->held < SM64_TRANS_HOLD_TICKS) { s->held++; return; }
    s->cover = 0.f; s->held = 0;
}
void sm64_look_transition(int on, const sm64_trans_state *s, const float centre[3], const float mn[3], const float mx[3],
                          float upm, fc_look *out) {
    if (!on || !(s->cover > 0.f || s->prev > 0.f)) { return; }
    out->flags |= FC_LOOK_TRANSITION;
    out->trans_cover = s->cover > 1.f ? 1.f : s->cover > 0.f ? s->cover : 0.f;
    out->trans_cover_prev = s->prev > 1.f ? 1.f : s->prev > 0.f ? s->prev : 0.f;
    out->trans_kind = (uint8_t)(s->kind == FC_TRANS_IRIS ? FC_TRANS_IRIS : FC_TRANS_FADE);
    memcpy(out->trans_rgb, s->rgb, sizeof out->trans_rgb);
    const int box = mn && mx && isfinite(mn[0]) && isfinite(mn[2]) && isfinite(mx[0]) && isfinite(mx[2]) && mx[0] > mn[0] && mx[2] > mn[2];
    float c[3];
    if (centre && isfinite(centre[0]) && isfinite(centre[1]) && isfinite(centre[2])) { memcpy(c, centre, sizeof c); }
    else if (box) { c[0] = 0.5f * (mn[0] + mx[0]); c[1] = 0.5f * (mn[1] + mx[1]); c[2] = 0.5f * (mn[2] + mx[2]); }
    else { out->trans_kind = FC_TRANS_FADE; return; }
    memcpy(out->trans_centre, c, sizeof c);
    float far = 0.f;
    if (box) {
        const float xs[2] = { mn[0], mx[0] }, zs[2] = { mn[2], mx[2] };
        for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { const float d = hypotf(xs[i] - c[0], zs[j] - c[2]); far = d > far ? d : far; }
    } else {
        far = 2.f * scale(upm);
    }
    out->trans_radius = far + 0.05f * scale(upm);
}
void sm64_look_trans_fade(int clear, int transitions_on, const sm64_trans_state *s, fc_look *out) {
    if (!clear || !transitions_on || !out) { return; }
    out->flags |= FC_LOOK_TRANS_CLEAR_ARMED;
    if ((out->flags & FC_LOOK_TRANSITION) && out->trans_kind == FC_TRANS_FADE && s && s->kind == FC_TRANS_FADE) {
        out->trans_kind = FC_TRANS_CLEAR;
    }
}
int sm64_act_star_is_pale(int in_menu, uint8_t render_flags, const uint8_t env[4]) {
    return in_menu && (render_flags & FC_RS_BLEND) && env && env[3] == SM64_ACT_STAR_PALE_ALPHA;
}
void sm64_act_star_shade(fc_vertex *v, uint32_t n) {
    for (uint32_t i = 0; v && i < n; i++)
        for (int k = 0; k < 3; k++) { v[i].shade[k] = (uint8_t)(v[i].shade[k] * SM64_ACT_STAR_SHADE + 0.5f); }
}

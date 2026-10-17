#include "sm64_story.h"
#include <math.h>
static const char *const kNames[] = { "level", "title screen", "file select", "act selector", "the end", "credits" };
static const char *const kTitles[] = { "Level", "Title screen", "File select", "Act selector", "The end", "Credits" };
const char *sm64_screen_name(int s) { return s >= 0 && s <= SM64_SCREEN_CREDITS ? kNames[s] : 0; }
const char *sm64_screen_title(int s) { return s >= 0 && s <= SM64_SCREEN_CREDITS ? kTitles[s] : 0; }
void sm64_file_button_cursor(int file, float out[2]) {
    /* file_select.c:1322-1349 button positions; check_clicked_button(x, y, 200): x*160/(a*200), y*120/(a*3/4*200), a = 52.4213 */
    static const float kPos[4][2] = { { -6400.f, 2800.f }, { 1500.f, 2800.f }, { -6400.f, 0.f }, { 1500.f, 0.f } };
    const double a = 52.4213;
    out[0] = out[1] = 0.f;
    if (file < 1 || file > 4) return;
    out[0] = (float)(kPos[file - 1][0] * 160.0 / (a * 200.0));
    out[1] = (float)(kPos[file - 1][1] * 120.0 / (a * 3.0 / 4.0 * 200.0));
}
int sm64_screen_is_menu(int s) { return s == SM64_SCREEN_FILE_SELECT || s == SM64_SCREEN_ACT_SELECT || s == SM64_SCREEN_CAKE; }
float sm64_menu_width_m(int size_choice) { return size_choice == 1 ? SM64_MENU_LARGE_WIDTH_M : SM64_MENU_WIDTH_M; }
void sm64_menu_anchor(float ground_y, float upm, float out[3]) {
    out[0] = 0.f;
    out[1] = ground_y + SM64_MENU_LIFT_M * upm;
    out[2] = 0.f;
}
void sm64_credits_anchor(float ground_y, float upm, float out[3]) {
    out[0] = 0.f;
    out[1] = ground_y + SM64_CREDITS_ABOVE_M * upm;
    out[2] = 0.f;
}
void sm64_gameover_anchor(const float mn[3], const float mx[3], float upm, float out[3]) {
    out[0] = 0.5f * (mn[0] + mx[0]);
    out[1] = mn[1];
    out[2] = mn[2] - SM64_GAMEOVER_BEHIND_M * upm;
}
int sm64_gameover_panel_shown(int story, int screen, int title_gameover, int gameover_text_choice) {
    return story && screen == SM64_SCREEN_TITLE && title_gameover && gameover_text_choice == 0;
}
void sm64_title_fit_reset(sm64_title_fit *f) {
    f->run = f->seen = f->frozen = f->last = 0;
    for (int k = 0; k < 3; k++) { f->mn[k] = f->mx[k] = f->last_mn[k] = f->last_mx[k] = 0.f; }
}
void sm64_title_fit_step(sm64_title_fit *f, int have, const float mn[3], const float mx[3]) {
    if (f->frozen) { return; }
    if (!have) { f->run = 0; f->last = 0; return; }
    float d = 0.f;
    for (int k = 0; k < 3 && f->last; k++) {
        const float a = fabsf(mn[k] - f->last_mn[k]), b = fabsf(mx[k] - f->last_mx[k]);
        d = a > d ? a : d; d = b > d ? b : d;
    }
    const int still = f->last && d <= SM64_TITLE_SETTLE_FRAC * (mx[1] - mn[1]);
    if (!still) { f->run = 0; }
    for (int k = 0; k < 3; k++) {
        f->mn[k] = f->run > 0 && f->mn[k] < mn[k] ? f->mn[k] : mn[k];
        f->mx[k] = f->run > 0 && f->mx[k] > mx[k] ? f->mx[k] : mx[k];
        f->last_mn[k] = mn[k]; f->last_mx[k] = mx[k];
    }
    f->last = 1;
    f->run++;
    f->seen++;
    if ((f->run > SM64_TITLE_SETTLE_TICKS || f->seen >= SM64_TITLE_FIT_MAX_TICKS) && f->mx[1] > f->mn[1]) { f->frozen = 1; }
}
int sm64_title_level(const sm64_title_fit *f, float mn[3], float mx[3], float *ground_y, float *upm) {
    const float h = f->mx[1] - f->mn[1];
    if (!f->frozen || !(h > 0.f)) { return 0; }
    for (int k = 0; k < 3; k++) { mn[k] = f->mn[k]; mx[k] = f->mx[k]; }
    *ground_y = f->mn[1];
    *upm = h / SM64_TITLE_HEIGHT_M;
    return 1;
}
int sm64_ray_box(const float o[3], const float d[3], const float mn[3], const float mx[3], float hit[3]) {
    double t0 = 0.0, t1 = 1e30;
    for (int k = 0; k < 3; k++) {
        if (fabs(d[k]) < 1e-12) { if (o[k] < mn[k] || o[k] > mx[k]) { return 0; } continue; }
        double a = (mn[k] - o[k]) / d[k], b = (mx[k] - o[k]) / d[k];
        if (a > b) { const double t = a; a = b; b = t; }
        if (a > t0) { t0 = a; }
        if (b < t1) { t1 = b; }
        if (t0 > t1) { return 0; }
    }
    for (int k = 0; k < 3; k++) { hit[k] = (float)(o[k] + t0 * d[k]); }
    return 1;
}
int sm64_menu_tap_ready(int screen, unsigned screen_ticks, unsigned since_tap_a) {
    if (screen == SM64_SCREEN_ACT_SELECT) { return screen_ticks >= SM64_ACT_TAP_READY_TICKS && since_tap_a >= SM64_FILE_TAP_GAP_TICKS; }
    if (screen == SM64_SCREEN_FILE_SELECT) { return since_tap_a >= SM64_FILE_TAP_GAP_TICKS; }
    return 0;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
void sm64_letter_anchor(const float f[3], const float mn[3], const float mx[3], float ground_y, float upm, float out[3]) {
    const float lift = SM64_LETTER_LIFT_M * upm;
    out[0] = mx[0] > mn[0] ? clampf(f[0], mn[0], mx[0]) : f[0];
    out[2] = mx[2] > mn[2] ? clampf(f[2], mn[2], mx[2]) : f[2];
    const float y = (f[1] > ground_y ? f[1] : ground_y) + lift;
    const float capped = mx[1] > mn[1] && y > mx[1] + lift ? mx[1] + lift : y;
    const float most = ground_y + SM64_LETTER_MAX_ABOVE_M * upm;
    out[1] = capped > most ? most : capped;
}
static int overlaps(const sm64_solid *s, float x, float z, float half) {
    return s->x1 >= x - half && s->x0 <= x + half && s->z1 >= z - half && s->z0 <= z + half;
}
static int letter_blocked(float x, float z, float half, float clear_y, const sm64_solid *s, int n, float box[4], int *any) {
    int hit = 0;
    for (int i = 0; i < n; i++) {
        if (!(s[i].top > clear_y) || !overlaps(&s[i], x, z, half)) { continue; }
        if (!*any) { box[0] = s[i].x0; box[1] = s[i].z0; box[2] = s[i].x1; box[3] = s[i].z1; *any = 1; }
        box[0] = fminf(box[0], s[i].x0); box[1] = fminf(box[1], s[i].z0);
        box[2] = fmaxf(box[2], s[i].x1); box[3] = fmaxf(box[3], s[i].z1);
        hit = 1;
    }
    return hit;
}
float sm64_letter_clear(const float start[3], const float toward_xz[2], float upm, const float mn[3], const float mx[3],
                        const sm64_solid *s, int n, float out[3], int *how) {
    float dx = toward_xz[0], dz = toward_xz[1];
    const float len = sqrtf(dx * dx + dz * dz);
    if (len > 1e-6f) { dx /= len; dz /= len; } else { dx = 0.f; dz = 1.f; }
    const float hw = 0.5f * SM64_LETTER_WIDTH_M * upm;
    const float half = hw + SM64_LETTER_CLEAR_M * upm, clear_y = start[1] - SM64_LETTER_CLEAR_M * upm;
    float lo[2] = { -1e30f, -1e30f }, hi[2] = { 1e30f, 1e30f };
    for (int k = 0; k < 2; k++) {
        const int a = k ? 2 : 0;
        if (mx[a] > mn[a]) {
            lo[k] = mn[a] + hw; hi[k] = mx[a] - hw;
            if (lo[k] > hi[k]) { lo[k] = hi[k] = 0.5f * (mn[a] + mx[a]); }
        }
    }
    const float sx = clampf(start[0], lo[0], hi[0]), sz = clampf(start[2], lo[1], hi[1]);
    out[1] = start[1];
    float box[4] = { 0.f, 0.f, 0.f, 0.f };
    int any = 0;
    const int steps = (int)lroundf(SM64_LETTER_PUSH_MAX_M / SM64_LETTER_PUSH_STEP_M);
    for (int i = 0; i <= steps; i++) {
        const float d = (float)i * SM64_LETTER_PUSH_STEP_M * upm;
        const float x = sx + dx * d, z = sz + dz * d;
        if (x < lo[0] || x > hi[0] || z < lo[1] || z > hi[1]) { break; }
        if (!letter_blocked(x, z, half, clear_y, s, n, box, &any)) {
            out[0] = x; out[2] = z; *how = SM64_LETTER_CLEARED;
            return hypotf(x - start[0], z - start[2]) / upm;
        }
    }
    float t = 0.f;
    if (any) {
        const float bx0 = box[0] - half, bz0 = box[1] - half, bx1 = box[2] + half, bz1 = box[3] + half;
        const float tx = dx > 1e-6f ? (bx1 - sx) / dx : dx < -1e-6f ? (bx0 - sx) / dx : 1e30f;
        const float tz = dz > 1e-6f ? (bz1 - sz) / dz : dz < -1e-6f ? (bz0 - sz) / dz : 1e30f;
        t = fmaxf(0.f, fminf(tx, tz));
    }
    out[0] = clampf(sx + dx * t, lo[0], hi[0]); out[2] = clampf(sz + dz * t, lo[1], hi[1]);
    *how = SM64_LETTER_FALLBACK;
    return hypotf(out[0] - start[0], out[2] - start[2]) / upm;
}
void sm64_tap_queue_reset(sm64_tap_queue *q) {
    q->pending = q->staged = 0; q->screen = -1; q->wait = 0; q->since_a = SM64_FILE_TAP_GAP_TICKS; q->ndc[0] = q->ndc[1] = 0.f;
}
int sm64_tap_queue_step(sm64_tap_queue *q, int tapped, const float ndc[2], int screen, unsigned screen_ticks,
                        const char *drops[2], int *ndrops) {
    *ndrops = 0;
    if (q->staged) {
        q->staged = 0;
        if (screen == q->screen) {
            q->since_a = 0;
            if (tapped) {
                q->pending = 1; q->wait = 0; q->ndc[0] = ndc[0]; q->ndc[1] = ndc[1];
            }
            return SM64_TAP_PRESS_A;
        }
        drops[(*ndrops)++] = "the screen changed";
    }
    if (q->since_a < 1000000u) { q->since_a++; }
    if (tapped) {
        if (q->pending) { drops[(*ndrops)++] = "a newer tap replaced it"; }
        q->pending = 1; q->screen = screen; q->wait = 0; q->ndc[0] = ndc[0]; q->ndc[1] = ndc[1];
    }
    if (!q->pending) { return SM64_TAP_IDLE; }
    const char *why = 0;
    if (screen != q->screen || !sm64_screen_is_menu(screen)) { why = "the screen changed"; }
    else if (screen != SM64_SCREEN_FILE_SELECT && screen != SM64_SCREEN_ACT_SELECT) { why = "nothing to pick on this screen"; }
    else if (!sm64_menu_tap_ready(screen, screen_ticks, q->since_a)) {
        if (++q->wait <= SM64_TAP_WAIT_MAX_TICKS) { return SM64_TAP_IDLE; }
        why = "the menu did not take input";
    }
    q->pending = 0;
    if (why) { if (*ndrops < 2) { drops[(*ndrops)++] = why; } return SM64_TAP_IDLE; }
    return SM64_TAP_PICK;
}
void sm64_tap_queue_staged(sm64_tap_queue *q) { q->staged = 1; }
void sm64_splash_level(float mn[3], float mx[3], float *ground_y, float *upm) {
    static const float kMin[3] = SM64_SPLASH_HOLD_MIN, kMax[3] = SM64_SPLASH_HOLD_MAX;
    for (int k = 0; k < 3; k++) { mn[k] = kMin[k]; mx[k] = kMax[k]; }
    *ground_y = kMin[1];
    *upm = (kMax[1] - kMin[1]) / SM64_TITLE_HEIGHT_M;
}
int sm64_title_box_over_centre(const float mn[3], const float mx[3]) {
    return mn[0] <= 0.f && mx[0] >= 0.f && mn[2] <= 0.f && mx[2] >= 0.f;
}
#define STAR_K (289.7056 / 1300.0)   /* 120 / tan(22.5 deg) px per unit at depth 1000 + 300 */
void sm64_menu_cursor_from_ndc(const float ndc[2], float out[2]) {
    const float x = ndc[0] * 160.f, y = ndc[1] * 120.f;
    out[0] = x < -132.f ? -132.f : x > 132.f ? 132.f : x;
    out[1] = y < -90.f ? -90.f : y > 90.f ? 90.f : y;
}
void sm64_act_star_ndc(int index, int visible, float out[2]) {
    const double px = 160.0 + STAR_K * (75.0 - 75.0 * visible + 152.0 * index), py = 120.0 - STAR_K * 248.0;
    out[0] = (float)(px / 160.0 - 1.0);
    out[1] = (float)(1.0 - py / 120.0);
}
int sm64_act_star_at(const float ndc[2], int visible) {
    for (int i = 0; i < visible; i++) {
        float c[2];
        sm64_act_star_ndc(i, visible, c);
        if (fabsf(ndc[0] - c[0]) * 160.f <= 16.f && fabsf(ndc[1] - c[1]) * 120.f <= 24.f) { return i; }
    }
    return -1;
}
static int finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
void sm64_title_eye_target(const float head[3], float out[3]) {
    const float d = sqrtf(head[0] * head[0] + head[2] * head[2]);
    float r = d - SM64_TITLE_EYE_MIN_DIST_M;
    r = r > SM64_TITLE_EYE_OUT_M ? SM64_TITLE_EYE_OUT_M : r < 0.f ? 0.f : r;
    out[0] = d > 1e-4f ? head[0] / d * r : 0.f;
    out[2] = d > 1e-4f ? head[2] / d * r : 0.f;
    const float y = head[1] - 0.5f * SM64_TITLE_HEIGHT_M;
    out[1] = y > 0.f ? y : 0.f;
}
void sm64_title_place_reset(sm64_title_place *p) {
    static const float kDefault[3] = SM64_TITLE_EYE_DEFAULT_HEAD;
    p->have = 0; p->pending = 1;
    sm64_title_eye_target(kDefault, p->pos);
}
void sm64_title_place_request(sm64_title_place *p) { p->pending = 1; }
int sm64_title_place_step(sm64_title_place *p, const float head[3], float out[3]) {
    int placed = 0;
    if (p->pending && head && finite3(head)) {
        sm64_title_eye_target(head, p->pos);
        p->have = 1; p->pending = 0; placed = 1;
    }
    for (int k = 0; k < 3; k++) { out[k] = p->pos[k]; }
    return placed;
}
void sm64_title_eye_reset(sm64_title_eye *e) { e->have = 0; e->pos[0] = e->pos[1] = e->pos[2] = 0.f; }
void sm64_title_eye_seed(sm64_title_eye *e, const float at[3]) {
    if (!at || !finite3(at)) { sm64_title_eye_reset(e); return; }
    e->have = 1;
    for (int k = 0; k < 3; k++) { e->pos[k] = at[k]; }
}
void sm64_title_eye_step_to(sm64_title_eye *e, const float place[3], float dt_s, float out[3]) {
    const int ok = place && finite3(place) && isfinite(dt_s) && dt_s >= 0.f;
    if (!e->have) {
        if (place && finite3(place)) { for (int k = 0; k < 3; k++) { e->pos[k] = place[k]; } e->have = 1; }
    } else if (ok) {
        const float a = 1.f - expf(-dt_s / SM64_TITLE_EYE_TAU_S);
        for (int k = 0; k < 3; k++) { e->pos[k] += (place[k] - e->pos[k]) * a; }
    }
    for (int k = 0; k < 3; k++) { out[k] = e->pos[k]; }
}

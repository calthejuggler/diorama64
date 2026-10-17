#include <math.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "ar_adapter_sm64.h"
#include "sm64_ar_hud.h"
#include "ar_math.h"
#include "ar_view.h"
#include "sm64_audio.h"
#include "sm64_capture.h"
#include "sm64_host.h"
#include "sm64_ar_trees.h"
#include "sm64_pad_map.h"
#include "sm64_ar_hooks.h"
#include "sm64_ar_view.h"
#include "sm64_ar_setup.h"
#include "sm64_ar_internal.h"
#include "sm64_story.h"
#include "sm64_settings.h"
#include "sm64_pip.h"
#include "sm64_touch.h"
#include "sm64_eyes.h"

static ar_snapshot_ring *sRing;
static _Atomic(ar_texture_fifo *) sFifo;   // published once at boot; read by the stats/drain from other threads
static char sErr[256];
static uint32_t sTicks;
static int sScreen;
static sm64_setup sSetup;
static int sLivesApplied;
static int sStartLevelNum;
static ar_level_info sLevel = { 8000.f, 0.f, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, SM64_AR_UNITS_PER_METER };
static _Atomic int sLevelReq;
static pthread_mutex_t sStateLock = PTHREAD_MUTEX_INITIALIZER;
static sm64_level_state sState;
static int sStateValid;
static int sKeyLevel = -1, sKeyArea = -1, sFollowKeys;
static int sAnchorLevel = -1, sAnchorArea = -1;
static float sAnchorY;
/* Float bits; 0 = not set (2 cm). Any thread writes; the game thread applies it at the start of each tick
 * (apply_scale). */
static _Atomic uint32_t sMarioCmBits;

int ar_adapter_sm64_level_count(void) { return SM64_LEVEL_COUNT; }
const sm64_level_entry *ar_adapter_sm64_level(int index) { return sm64_level_at(index); }
static _Atomic(const char *) sRefusal;
const char *ar_adapter_sm64_request_refusal(void) { return atomic_load(&sRefusal); }
int ar_adapter_sm64_play_from_start(void) {
    const char *why = NULL;
    if (sm64_host_play_from_start(&why)) { return 1; }
    atomic_store(&sRefusal, why ? why : "refused");
    return 0;
}
int ar_adapter_sm64_request_level(int index, int act) {
    if (!sm64_level_at(index) || act < -1 || act == 0 || act > 6) { return 0; }
    sm64_level_state ls;
    if (ar_adapter_sm64_level_state(&ls)) {
        const char *scr = sm64_host_screen_refusal(ls.screen);
        if (scr) { atomic_store(&sRefusal, scr); return 0; }
    }
    atomic_store(&sLevelReq, (index + 1) * 16 + (act + 1));
    return 1;
}
static _Atomic(ar_adapter_sm64_screen_fn) sScreenLog;
void ar_adapter_sm64_set_screen_log(ar_adapter_sm64_screen_fn fn) { atomic_store(&sScreenLog, fn); }
static _Atomic(ar_adapter_sm64_dropped_fn) sDroppedLog;
void ar_adapter_sm64_set_dropped_log(ar_adapter_sm64_dropped_fn fn) { atomic_store(&sDroppedLog, fn); }
void ar_adapter_sm64_set_draw_all_rooms(int on) { sm64_host_set_draw_all_rooms(on); }
int ar_adapter_sm64_draw_all_rooms(void) { return sm64_host_draw_all_rooms(); }
void ar_adapter_sm64_set_other_room_objects(int mode) { sm64_host_set_other_room_objects(mode); }
int ar_adapter_sm64_other_room_objects(void) { return sm64_host_other_room_objects(); }
void ar_adapter_sm64_set_hide_outside(int on) { sm64_host_set_hide_outside(on); }
int ar_adapter_sm64_hide_outside(void) { return sm64_host_hide_outside(); }
int ar_adapter_sm64_test_place_mario(float x, float y, float z) { return sm64_host_test_place_mario(x, y, z); }
void ar_adapter_sm64_set_mario_cm(float cm) {
    const float c = sm64_mario_cm_clamp(cm);
    uint32_t b; memcpy(&b, &c, sizeof b);
    atomic_store(&sMarioCmBits, b);
}
float ar_adapter_sm64_mario_cm(void) {
    const uint32_t b = atomic_load(&sMarioCmBits);
    float c; memcpy(&c, &b, sizeof c);
    return b ? c : SM64_MARIO_CM_DEFAULT;
}
float sm64_ar_units_per_meter(void) { return sLevel.units_per_meter; }
float sm64_ar_ground_y(void) { return sLevel.ground_y; }
void sm64_ar_menu_anchor(float out[3]) { sm64_menu_anchor(sLevel.ground_y, sLevel.units_per_meter, out); }
static sm64_title_fit sTitle;
static float sTitleUpm;
static unsigned sTitleInits;
static void apply_scale(void) {
    if (sTitleUpm > 0.f) {
        sLevel.units_per_meter = sTitleUpm; sLevel.reference_units_per_meter = sTitleUpm;
        return;
    }
    if (sm64_screen_is_menu(sScreen)) { return; }
    if (sScreen == SM64_SCREEN_TITLE) { return; }
    sLevel.units_per_meter = sm64_units_per_meter_for_mario_cm(ar_adapter_sm64_mario_cm());
    sLevel.reference_units_per_meter = SM64_AR_UNITS_PER_METER;
}
int sm64_ar_title_box(float mn[3], float mx[3]) {
    if (!(sTitleUpm > 0.f)) { return 0; }
    for (int k = 0; k < 3; k++) { mn[k] = sLevel.bounds_min[k]; mx[k] = sLevel.bounds_max[k]; }
    return 1;
}
static sm64_title_place sTitlePlace;
static float sTitleAt[3];
static _Atomic unsigned sTablePlaced;
static unsigned sTablePlacedSeen;
static _Atomic(ar_adapter_sm64_touch_fn) sTitleLog;
void ar_adapter_sm64_table_placed(void) { atomic_fetch_add(&sTablePlaced, 1u); }
static _Atomic int sPlaceOnRequest;
void ar_adapter_sm64_set_title_place_on_request(int on) { atomic_store(&sPlaceOnRequest, on ? 1 : 0); }
void ar_adapter_sm64_set_title_log(ar_adapter_sm64_touch_fn fn) { atomic_store(&sTitleLog, fn); }
static sm64_title_eye sTitleEye;
static float sTitleHead[3] = { NAN, NAN, NAN };
static float sTitleOff[3];
static int sTitleFromTable;
static float sInfoOff[3];
void sm64_ar_title_offset(float out[3]) { for (int k = 0; k < 3; k++) { out[k] = sTitleOff[k]; } }
static void line_log(ar_adapter_sm64_touch_fn fn, const char *line) { if (fn) { fn(line, sTicks); } }
static void title_place_tick(void) {
    const unsigned n = atomic_load(&sTablePlaced);
    if (n != sTablePlacedSeen) { sTablePlacedSeen = n; sm64_title_place_request(&sTitlePlace); }
    if (sm64_title_place_step(&sTitlePlace, sTitleHead, sTitleAt)) {
        sm64_title_eye_reset(&sTitleEye); sTitleFromTable = 0;
        char line[96];
        snprintf(line, sizeof line, "placed at=%.3f,%.3f,%.3f reason=table", (double)sTitleAt[0], (double)sTitleAt[1], (double)sTitleAt[2]);
        line_log(atomic_load(&sTitleLog), line);
    }
}
static void set_title_info(const float mn[3], const float mx[3], float gy, float upm) {
    float off[3] = { 0.f, 0.f, 0.f };
    if (sm64_settings_choice("title_eye_level") == 0) {
        float a[3];
        if (!sTitleEye.have && sTitleFromTable) {
            const float table[3] = { 0.5f * (mn[0] + mx[0]) / upm, (mn[1] - gy) / upm, 0.5f * (mn[2] + mx[2]) / upm };
            sm64_title_eye_seed(&sTitleEye, table);
        }
        sm64_title_eye_step_to(&sTitleEye, sTitleAt, 1.f / 30.f, a);
        off[0] = a[0] * upm - 0.5f * (mn[0] + mx[0]);
        off[1] = a[1] * upm + gy - mn[1];
        off[2] = a[2] * upm - 0.5f * (mn[2] + mx[2]);
    } else {
        sm64_title_eye_reset(&sTitleEye);
    }
    for (int k = 0; k < 3; k++) {
        sLevel.bounds_min[k] = mn[k] + off[k]; sLevel.bounds_max[k] = mx[k] + off[k]; sTitleOff[k] = sInfoOff[k] = off[k];
    }
    sLevel.ground_y = gy; sLevel.units_per_meter = upm; sLevel.reference_units_per_meter = upm; sTitleUpm = upm;
}
/* A new fit at every title script load: the splash and the head are two loads of one TITLE screen (sm64_host_screen
 * never says LEVEL between them). */
static void title_tick(int prev_screen) {
    if (sScreen != SM64_SCREEN_TITLE) {
        for (int k = 0; k < 3; k++) { sTitleOff[k] = 0.f; }
        sTitleUpm = 0.f; sm64_title_eye_reset(&sTitleEye); sTitleFromTable = 1;
        return;
    }
    const unsigned inits = sm64_host_level_inits();
    if (prev_screen != SM64_SCREEN_TITLE || inits != sTitleInits) { sm64_title_fit_reset(&sTitle); sTitleInits = inits; }
    float mn[3], mx[3], gy, upm;
    if (sm64_host_title_splash()) { sm64_splash_level(mn, mx, &gy, &upm); set_title_info(mn, mx, gy, upm); return; }
    const int have = sm64_capture_world_bounds(mn, mx) && sm64_title_box_over_centre(mn, mx);
    sm64_title_fit_step(&sTitle, have, mn, mx);
    float fmn[3], fmx[3];
    if (sm64_title_level(&sTitle, fmn, fmx, &gy, &upm)) { set_title_info(fmn, fmx, gy, upm); }
}
/* Published by the game thread after each tick for the layer thread's hit-test; menu_a and tmin/tmax in table metres,
 * toff in level units. */
typedef struct { int screen; float menu_a[3]; float upm, ground; int title; float tmin[3], tmax[3]; float menu_w; float toff[3]; } pointer_geo;
static pthread_mutex_t sPtrLock = PTHREAD_MUTEX_INITIALIZER;
static pointer_geo sPtrGeo;
static _Atomic int sPtrLocked;
static _Atomic uint64_t sTap;   /* bit 63: pending; bits 32..47 / 16..31: x, y as int16 (x 32767) */
/* Written by the layer thread, read by the game thread each tick. Only the grab's own pinch (id) moves or releases it;
 * gen counts grabs. */
typedef struct { int active, released; uint64_t id; unsigned gen; float hit_level[3]; float hand0[3], hand[3]; int has_hand;
                 int kind; float at[3]; } grab_state;
static grab_state sGrab;   /* guarded by sPtrLock */
static int sGrabStage;
int sm64_ar_head_grab(uint64_t *id) {
    pthread_mutex_lock(&sPtrLock);
    const int held = sGrab.active && !sGrab.released;
    if (held && id) { *id = sGrab.id; }
    pthread_mutex_unlock(&sPtrLock);
    return held;
}
static sm64_hand_tips sHands[2];
void ar_adapter_sm64_set_hands(const sm64_hand_tips hands[2]) {
    pthread_mutex_lock(&sPtrLock);
    for (int i = 0; i < 2; i++) { if (hands) { sHands[i] = hands[i]; } else { memset(&sHands[i], 0, sizeof sHands[i]); } }
    pthread_mutex_unlock(&sPtrLock);
}
int ar_adapter_sm64_wants_hands(void) {
    return sm64_settings_choice("title_eyes") != SM64_EYES_OFF;
}
static _Atomic(ar_adapter_sm64_touch_fn) sTouchLog;
void ar_adapter_sm64_set_touch_log(ar_adapter_sm64_touch_fn fn) { atomic_store(&sTouchLog, fn); }
void ar_adapter_sm64_set_pointer_lock(int locked) {
    atomic_store(&sPtrLocked, locked ? 1 : 0);
    if (locked) {   /* a head grab held when recording or a replay starts lets go (its END would be refused) */
        pthread_mutex_lock(&sPtrLock); if (sGrab.active) { sGrab.released = 1; } pthread_mutex_unlock(&sPtrLock);
    }
}
float ar_adapter_sm64_pip_alpha(void) { return sm64_ar_pip_alpha(); }
int ar_adapter_sm64_intro_shot(void) { return sm64_host_intro_shot(); }
void ar_adapter_sm64_set_pip_sky(int on) { sm64_ar_pip_set_sky(on); }
int ar_adapter_sm64_game_camera(ar_pose *table_from_eye, float *tan_half_fovy, float *near_m) {
    float view[16], roll[16], proj[16], eye[3];
    if (!sm64_host_game_camera(SM64_PIP_ASPECT, view, roll, proj, eye) || !(proj[5] > 0.f)) { return 0; }
    if (!sm64_pip_camera_pose(view, roll, sLevel.units_per_meter, sLevel.ground_y, table_from_eye)) { return 0; }
    *tan_half_fovy = 1.f / proj[5];
    /* guPerspectiveF: m[3][2] = 2 n f / (n - f), m[2][2] = (n + f) / (n - f), so n = m[3][2] / (m[2][2] - 1) */
    *near_m = proj[14] / (proj[10] - 1.f) / sLevel.units_per_meter;
    return 1;
}
int ar_adapter_sm64_setting_count(void) { return sm64_settings_count(); }
const char *ar_adapter_sm64_setting_key(int i) { const sm64_setting_def *d = sm64_settings_def(i); return d ? d->key : NULL; }
const char *ar_adapter_sm64_setting_default(const char *key) {
    for (int i = 0; key && i < sm64_settings_count(); i++) { if (!strcmp(sm64_settings_def(i)->key, key)) { return sm64_settings_def(i)->def; } }
    return NULL;
}
int ar_adapter_sm64_set_setting(const char *key, const char *value, char *err, size_t cap) { return sm64_settings_set(key, value, err, cap); }
int ar_adapter_sm64_get_setting(const char *key, char *buf, size_t cap) { return sm64_settings_get(key, buf, cap); }
int ar_adapter_sm64_audio_state(void) { return sm64_audio_state(); }
int ar_adapter_sm64_settings_revision(void) { return sm64_settings_revision(); }
int ar_adapter_sm64_settings_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override) {
    return sm64_settings_drop_stored(key, stored, stored_rev, has_launch_override);
}
/* Latched once per tick on the game thread, so the frame hook and the layer thread's hit-test use the same width even
 * when the setting changes mid-tick. */
static float sMenuW = SM64_MENU_LARGE_WIDTH_M;
float sm64_ar_menu_width_m(void) { return sMenuW; }
static void publish_pointer_geo(void) {
    pointer_geo g; g.screen = sScreen; g.upm = sLevel.units_per_meter; g.ground = sLevel.ground_y;
    float a[3]; sm64_ar_menu_anchor(a);
    g.menu_a[0] = a[0] / g.upm; g.menu_a[1] = (a[1] - g.ground) / g.upm; g.menu_a[2] = a[2] / g.upm;
    g.menu_w = sm64_ar_menu_width_m();
    float mn[3], mx[3];
    g.title = sScreen == SM64_SCREEN_TITLE && !sm64_host_title_splash() && sm64_ar_title_box(mn, mx);
    sm64_ar_title_offset(g.toff);
    for (int k = 0; k < 3; k++) {
        const float off = k == 1 ? g.ground : 0.f;
        g.tmin[k] = g.title ? (mn[k] - off) / g.upm : 0.f;
        g.tmax[k] = g.title ? (mx[k] - off) / g.upm : 0.f;
    }
    pthread_mutex_lock(&sPtrLock); sPtrGeo = g; pthread_mutex_unlock(&sPtrLock);
}
int ar_adapter_sm64_menu_tap(float x, float y) {
    pointer_geo g; pthread_mutex_lock(&sPtrLock); g = sPtrGeo; pthread_mutex_unlock(&sPtrLock);
    if (!sm64_screen_is_menu(g.screen)) { return 0; }
    const uint64_t qx = (uint16_t)(int16_t)lrintf(fmaxf(-1.f, fminf(1.f, x)) * 32767.f);
    const uint64_t qy = (uint16_t)(int16_t)lrintf(fmaxf(-1.f, fminf(1.f, y)) * 32767.f);
    atomic_store(&sTap, (1ull << 63) | (qx << 32) | (qy << 16));
    return 1;
}
int ar_adapter_sm64_pointer(sm64_pointer *p) {
    p->target = SM64_POINTER_NONE;
    if (atomic_load(&sPtrLocked)) { return 0; }
    pointer_geo g; pthread_mutex_lock(&sPtrLock); g = sPtrGeo; pthread_mutex_unlock(&sPtrLock);
    if (p->phase != SM64_POINTER_BEGIN) {
        pthread_mutex_lock(&sPtrLock);
        if (sGrab.active && sGrab.id == p->id) {
            if (p->has_hand) { for (int k = 0; k < 3; k++) { sGrab.hand[k] = p->hand[k]; } }
            if (p->phase == SM64_POINTER_END || p->phase == SM64_POINTER_CANCEL) { sGrab.released = 1; }
        }
        pthread_mutex_unlock(&sPtrLock);
        return 1;
    }
    if (sm64_screen_is_menu(g.screen) && !p->no_ray) {
        static const float kFull[4] = { -1.f, -1.f, 1.f, 1.f };
        if (ar_world_point_hit(g.menu_a, 1, &p->head, g.menu_w, kFull, 320.f, 240.f, p->ray_o, p->ray_d, p->ndc)) {
            p->target = SM64_POINTER_MENU;
            ar_adapter_sm64_menu_tap(p->ndc[0], p->ndc[1]);
            return 1;
        }
    }
    if (g.title) {
        float hit[3];
        const int kind = sm64_touch_begin(sm64_settings_choice("title_touch"), p->has_hand, p->hand, !p->no_ray, p->ray_o,
                                          p->ray_d, g.tmin, g.tmax, hit);
        if (kind != SM64_TOUCH_NONE) {
            p->target = SM64_POINTER_HEAD;
            if (kind == SM64_TOUCH_IGNORED) { return 1; }
            pthread_mutex_lock(&sPtrLock);
            if (sGrab.active && !sGrab.released) { pthread_mutex_unlock(&sPtrLock); return 1; }
            sGrab.active = 1; sGrab.released = 0; sGrab.has_hand = p->has_hand; sGrab.id = p->id; sGrab.gen++;
            sGrab.kind = kind; for (int k = 0; k < 3; k++) { sGrab.at[k] = hit[k]; }
            for (int k = 0; k < 3; k++) { sGrab.hit_level[k] = (k == 1 ? hit[k] * g.upm + g.ground : hit[k] * g.upm) - g.toff[k];
                                          sGrab.hand0[k] = sGrab.hand[k] = p->hand[k]; }
            pthread_mutex_unlock(&sPtrLock);
            return 1;
        }
    }
    return 0;
}
/* A new grab starts with the cursor, so A is let go for a tick between two (the face springs back). */
static int sDriveKind;
static unsigned sDriveGen;
static void touch_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void touch_log(const char *fmt, ...) {
    const ar_adapter_sm64_touch_fn fn = atomic_load(&sTouchLog);
    if (!fn) { return; }
    char line[160];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    fn(line, sTicks);
}
static int head_touch_step(void) {
    grab_state gs;
    pthread_mutex_lock(&sPtrLock);
    gs = sGrab; if (gs.released) { sGrab.active = 0; sGrab.released = 0; }
    pthread_mutex_unlock(&sPtrLock);
    const int title = sScreen == SM64_SCREEN_TITLE;
    const int pinch_held = gs.active && !gs.released && title;
    int kind = SM64_TOUCH_NONE; unsigned gen = 0;
    float target[3];
    if (pinch_held) {
        kind = gs.kind; gen = gs.gen;
        for (int k = 0; k < 3; k++) { target[k] = gs.hit_level[k] + (gs.has_hand ? (gs.hand[k] - gs.hand0[k]) * sLevel.units_per_meter : 0.f); }
    }
    if (kind != sDriveKind || gen != sDriveGen) {
        if (sDriveKind != SM64_TOUCH_NONE) {
            const char *why = atomic_load(&sPtrLocked) ? "lock" : !title ? "screen" : "end";
            touch_log("release kind=%s why=%s", sm64_touch_kind_name(sDriveKind), why);
        }
        if (kind != SM64_TOUCH_NONE) {
            touch_log("grab kind=%s at=%.3f,%.3f,%.3f", sm64_touch_kind_name(kind), (double)gs.at[0], (double)gs.at[1], (double)gs.at[2]);
        }
        sDriveKind = kind; sDriveGen = gen; sGrabStage = 0;
    }
    if (kind == SM64_TOUCH_NONE) { sGrabStage = 0; return 0; }
    float c[2];
    if (sm64_host_gd_world_to_cursor(target, c)) { sm64_host_gd_set_cursor(c[0], c[1]); }
    const int a = sGrabStage >= 1;
    sGrabStage = sGrabStage >= 1 ? 2 : 1;
    return a;
}
static _Atomic int sEyesActive;
static _Atomic(ar_adapter_sm64_touch_fn) sEyesLog;
void ar_adapter_sm64_set_title_eyes_active(int on) { atomic_store(&sEyesActive, on ? 1 : 0); }
void ar_adapter_sm64_set_eyes_log(ar_adapter_sm64_touch_fn fn) { atomic_store(&sEyesLog, fn); }
static sm64_eyes sEyes;
static sm64_eyes_pick sPick;
static int sEyesMode = -1, sEyesFollow = -1;
enum { EYES_GRAB = 3 };
static int sEyesFollowing;
static int sEyesMaskLogged;
static void eyes_step(const float viewer[3]) {
    if (!atomic_load(&sEyesActive)) {
        sm64_host_gd_set_eye_follow(0); sm64_eyes_reset(&sEyes); sm64_eyes_pick_reset(&sPick); sEyesFollowing = 0;
        return;
    }
    const int mode = sm64_settings_choice("title_eyes");
    const int locked = atomic_load(&sPtrLocked);
    pointer_geo g; sm64_hand_tips hands[2];
    pthread_mutex_lock(&sPtrLock); g = sPtrGeo; hands[0] = sHands[0]; hands[1] = sHands[1]; pthread_mutex_unlock(&sPtrLock);
    const int head_up = sScreen == SM64_SCREEN_TITLE && g.title;
    int follow = SM64_EYES_AT_NONE, following = 0;
    if (head_up && !locked && sDriveKind != SM64_TOUCH_NONE) {
        follow = EYES_GRAB;
    } else if (head_up && !locked && mode != SM64_EYES_OFF) {
        const float centre[3] = { 0.5f * (g.tmin[0] + g.tmax[0]), 0.5f * (g.tmin[1] + g.tmax[1]), 0.5f * (g.tmin[2] + g.tmax[2]) };
        float t[3];
        follow = sm64_eyes_pick_step(&sPick, mode, hands, viewer, centre, t);
        if (follow != SM64_EYES_AT_NONE) {
            float tw[3], eyes[3], aim[3], c[2];
            for (int k = 0; k < 3; k++) { tw[k] = (k == 1 ? t[k] * g.upm + g.ground : t[k] * g.upm) - g.toff[k]; }
            if (!sm64_host_gd_eyes_world(eyes)) {
                for (int k = 0; k < 3; k++) { eyes[k] = (k == 1 ? centre[k] * g.upm + g.ground : centre[k] * g.upm) - g.toff[k]; }
            }
            float toward[2], mid[2], fwd[3];
            const int geo = sm64_eyes_aim(tw, eyes, SM64_EYES_AIM_M * g.upm, aim) && sm64_host_gd_world_to_cursor(aim, toward) &&
                            sm64_host_gd_world_to_cursor(eyes, mid) && sm64_host_gd_camera_forward(fwd);
            if (geo && (!sm64_eyes_follows(sEyesFollowing, sm64_eyes_angle_deg(tw, eyes, fwd)) || !sm64_eyes_cursor(mid, toward, c))) {
            } else if (geo) {
                float out[2];
                if (!sEyesFollowing) {   /* a follow that starts looks at once (a saccade): easing from elsewhere (the
                                          * midpoint, a grab's point) would cross the eyes on the way */
                    sm64_eyes_clamp(c); sm64_eyes_jump(&sEyes, c); out[0] = c[0]; out[1] = c[1];
                } else {
                    float cur[2];
                    sm64_host_gd_cursor(cur);
                    sm64_eyes_ease(&sEyes, cur, c, 1.f / 30.f, out);
                }
                sm64_host_gd_set_cursor(out[0], out[1]);
                following = 1;
            }
        }
    }
    sEyesFollowing = following;
    sm64_host_gd_set_eye_follow(following);
    if (follow == SM64_EYES_AT_NONE || follow == EYES_GRAB) {
        sm64_eyes_reset(&sEyes); sm64_eyes_pick_reset(&sPick);
    }
    if (head_up && mode != SM64_EYES_OFF && !sEyesMaskLogged && sm64_host_gd_eyes_still_status() < 0) {
        sEyesMaskLogged = 1;
        const ar_adapter_sm64_touch_fn fn = atomic_load(&sEyesLog);
        if (fn) { fn("mask=unavailable", sTicks); }
    }
    if (mode != sEyesMode || follow != sEyesFollow) {
        sEyesMode = mode; sEyesFollow = follow;
        const ar_adapter_sm64_touch_fn fn = atomic_load(&sEyesLog);
        if (fn) {
            char line[64];
            snprintf(line, sizeof line, "mode=%s follow=%s", sm64_eyes_mode_name(mode),
                     follow == EYES_GRAB ? "grab" : sm64_eyes_follow_name(follow));
            fn(line, sTicks);
        }
    }
}
static sm64_tap_queue sTapQ = { 0, 0, -1, 0, SM64_FILE_TAP_GAP_TICKS, { 0.f, 0.f } };
static unsigned sScreenTicks;
static int sNoPlayerA;
static void tap_dropped(const char *why) {
    const ar_adapter_sm64_dropped_fn fn = atomic_load(&sDroppedLog);
    if (fn) { fn("tap", why, sTicks); }
}
static int menu_tap_step(void) {
    const uint64_t t = atomic_exchange(&sTap, 0);
    float ndc[2] = { (float)(int16_t)(uint16_t)(t >> 32) / 32767.f, (float)(int16_t)(uint16_t)(t >> 16) / 32767.f };
    const char *drops[2]; int nd = 0;
    const int r = sm64_tap_queue_step(&sTapQ, (int)(t >> 63), ndc, sScreen, sScreenTicks, drops, &nd);
    for (int i = 0; i < nd; i++) { tap_dropped(drops[i]); }
    if (r == SM64_TAP_PRESS_A) { return 1; }
    if (r != SM64_TAP_PICK) { return 0; }
    if (sScreen == SM64_SCREEN_FILE_SELECT) {
        float c[2]; sm64_menu_cursor_from_ndc(sTapQ.ndc, c);
        sm64_host_set_menu_cursor(c[0], c[1]); sm64_tap_queue_staged(&sTapQ);
    } else {
        const int i = sm64_act_star_at(sTapQ.ndc, sm64_host_act_stars_visible());
        if (i >= 0 && sm64_host_act_select(i)) { sm64_tap_queue_staged(&sTapQ); }
        else { tap_dropped(i < 0 ? "no star there" : "that star cannot be chosen yet"); }
    }
    return 0;
}
static const char *const kOtherRooms[] = { "hidden", "frozen", "live" };
const char *ar_adapter_sm64_other_rooms_name(int mode) { return mode >= 0 && mode < 3 ? kOtherRooms[mode] : NULL; }
void ar_adapter_sm64_set_tree_style(int style) { sm64_trees_set_style(style); }
int ar_adapter_sm64_tree_style(void) { return sm64_trees_style(); }
const char *ar_adapter_sm64_tree_style_name(int style) { return sm64_trees_style_name(style); }
int ar_adapter_sm64_tree_style_parse(const char *name) { return sm64_trees_style_parse(name); }
void ar_adapter_sm64_set_billboard_style(int style) { sm64_ar_set_billboard_style(style); }
int ar_adapter_sm64_billboard_style(void) { return sm64_ar_billboard_style(); }
const char *ar_adapter_sm64_billboard_style_name(int style) { return sm64_ar_billboard_style_name(style); }
int ar_adapter_sm64_billboard_style_parse(const char *name) { return sm64_ar_billboard_style_parse(name); }
void ar_adapter_sm64_set_cannon_visible(int on) { sm64_host_set_cannon_visible(on); }
int ar_adapter_sm64_cannon_visible(void) { return sm64_host_cannon_visible(); }
int ar_adapter_sm64_pack_count(void) { return sm64_host_pack_count(); }
int ar_adapter_sm64_pack_info(int i, char *name, int cap, int *kind, int *tex, int *bins, double *mb) {
    const char *n = sm64_host_pack_name(i);
    if (!n) { return 0; }
    if (name && cap > 0) { snprintf(name, (size_t)cap, "%s", n); }
    if (kind) { *kind = sm64_host_pack_kind(i); }
    return sm64_host_pack_files(i, tex, bins, mb);
}
int ar_adapter_sm64_set_packs(int kind, int on, const char *name) {
    sm64_host_set_pack_choice(kind, on, name);
    return sm64_host_resolve_pack(kind, on, name);
}
int ar_adapter_sm64_pack_enabled(int i) { return sm64_host_pack_enabled(i); }
double ar_adapter_sm64_pack_load_ms(void) { return sm64_host_pack_last_load_ms(); }
void ar_adapter_sm64_texfifo_stats(uint64_t *pushed_bytes, uint64_t *peak_pending_bytes) {
    uint64_t p = 0, k = 0;
    ar_texture_fifo *f = sFifo;
    if (f) ar_texfifo_stats(f, &p, &k);
    if (pushed_bytes) *pushed_bytes = p;
    if (peak_pending_bytes) *peak_pending_bytes = k;
}
int ar_adapter_sm64_trees_3d(void) { return sm64_host_trees_3d(); }
int ar_adapter_sm64_tree_cards(void) { return sm64_trees_count(sm64_host_frame_count()); }
int ar_adapter_sm64_objects_force_3d(void) { return sm64_host_objects_force_3d(-1); }
int ar_adapter_sm64_probe_layer_clamp(void) { return sm64_host_probe_layer_clamp(); }
unsigned ar_adapter_sm64_gfx_clamped(void) { return sm64_host_gfx_clamped(); }
uint64_t ar_adapter_sm64_deep_state_hash(void) { return sm64_host_deep_state_hash(); }
unsigned ar_adapter_sm64_pack_draws(void) { return sm64_host_pack_draws(); }
unsigned ar_adapter_sm64_pack_tilts(void) { return sm64_host_pack_tilts(); }
int ar_adapter_sm64_pack_tilt_sample(float m[16], float floor_n[3], int *yaw) { return sm64_host_pack_tilt_sample(m, floor_n, yaw); }
void ar_adapter_sm64_test_queue_moving_coin(int tick) { sm64_host_test_queue_moving_coin(tick); }
void ar_adapter_sm64_test_queue_metal_cap(int tick) { sm64_host_test_queue_metal_cap(tick); }
int ar_adapter_sm64_actor_instances(const char *geo_name) { return sm64_host_actor_instances(geo_name); }
int ar_adapter_sm64_other_rooms_parse(const char *name) {
    for (int i = 0; name && i < 3; i++) { if (strcmp(name, kOtherRooms[i]) == 0) { return i; } }
    return -1;
}
int ar_adapter_sm64_level_state(sm64_level_state *out) {
    pthread_mutex_lock(&sStateLock);
    const int ok = sStateValid;
    if (ok) { *out = sState; }
    pthread_mutex_unlock(&sStateLock);
    return ok;
}
static void publish_state(void) {
    sm64_level_state s;
    s.level_num = sm64_host_level(); s.index = sm64_level_index_of(s.level_num); s.area = sm64_host_area();
    s.act = sm64_host_act(); s.home_index = sm64_level_index_of(sm64_host_home_level());
    s.pending = sm64_host_warp_pending() || atomic_load(&sLevelReq) != 0;
    for (int i = 0; i < SM64_LEVEL_COUNT; i++) {
        const int slot = sm64_level_at(i)->star_slot;
        s.stars[i] = slot >= 0 ? sm64_host_save_star_flags(slot) : 0;
    }
    const char *why = NULL;
    s.wait_ticks = sm64_host_request_wait(&why);
    snprintf(s.wait_reason, sizeof s.wait_reason, "%s", s.wait_ticks > 0 && why ? why : "");
    s.forced_count = sm64_host_request_forced(&why);
    snprintf(s.forced_reason, sizeof s.forced_reason, "%s", s.forced_count > 0 && why ? why : "");
    s.save_count = sm64_host_saves(&s.save_course, &s.save_star);
    {
        const float ex = sLevel.bounds_max[0] - sLevel.bounds_min[0], ez = sLevel.bounds_max[2] - sLevel.bounds_min[2];
        s.extent_units = ex > 0.f && ez > 0.f ? (ex > ez ? ex : ez) : 0.f;
    }
    s.story = sm64_host_story();
    s.screen = sm64_host_screen();
    {
        static int last_screen = -1, last_story = -1;
        const ar_adapter_sm64_screen_fn fn = atomic_load(&sScreenLog);
        if (fn && (s.screen != last_screen || s.story != last_story)) { fn(s.screen, s.story, sTicks); }
        last_screen = s.screen; last_story = s.story;
    }
    {
        static int last_dropped;
        const char *what = NULL, *why = NULL;
        const int n = sm64_host_dropped(&what, &why);
        if (n != last_dropped) {
            last_dropped = n;
            atomic_store(&sRefusal, why ? why : "dropped");
            const ar_adapter_sm64_dropped_fn fn = atomic_load(&sDroppedLog);
            if (fn) { fn(what ? what : "?", why ? why : "?", sTicks); }
        }
    }
    pthread_mutex_lock(&sStateLock);
    sState = s; sStateValid = 1;
    pthread_mutex_unlock(&sStateLock);
}

int ar_adapter_sm64_setup(const char *text, char *err, size_t cap) {
    sm64_setup s;
    if (!sm64_setup_parse(text, &s, err, cap)) { return 0; }
    sSetup = s;
    sm64_host_set_story(s.has_start && s.start_story);
    if (s.has_mario_cm) { ar_adapter_sm64_set_mario_cm(s.mario_cm); }
    if (s.has_cannon) { ar_adapter_sm64_set_cannon_visible(s.cannon); }
    if (s.has_rooms) { ar_adapter_sm64_set_draw_all_rooms(s.draw_all_rooms); }
    if (s.has_other_rooms) { ar_adapter_sm64_set_other_room_objects(s.other_rooms); }
    return 1;
}
int ar_adapter_sm64_story(void) { return sm64_host_story(); }
void ar_adapter_sm64_reset_game_settings(void) {
    ar_adapter_sm64_set_mario_cm(SM64_MARIO_CM_DEFAULT);
    ar_adapter_sm64_set_cannon_visible(1);
    ar_adapter_sm64_set_draw_all_rooms(1);
    ar_adapter_sm64_set_other_room_objects(SM64_OTHER_ROOMS_LIVE);
}
static int a_init(const char *rom, const char *save) {
    if (sRing) { snprintf(sErr, sizeof sErr, "already initialised (the game boots once per process)"); return 0; }
    sRing = ar_ring_create();
    sm64_title_place_reset(&sTitlePlace);
    if (atomic_load(&sPlaceOnRequest)) { sTitlePlace.pending = 0; }
    sm64_eyes_reset(&sEyes); sm64_eyes_pick_reset(&sPick); sEyesFollowing = sEyesMaskLogged = 0;
    ar_texture_fifo *fifo = ar_texfifo_create();
    sm64_capture_install(sRing, fifo);
    sFifo = fifo;
    sm64_host_set_ar_mode(1);
    sm64_ar_hud_reset();
    sm64_ar_pip_reset();
    sm64_ar_look_reset();
    sm64_ar_hooks_install();
    if (sm64_host_boot(save, rom) != SM64_BOOT_OK) { snprintf(sErr, sizeof sErr, "%s", sm64_host_last_error()); return 0; }
    sm64_audio_install();
    const sm64_level_entry *start = sm64_level_at(sSetup.has_level ? sSetup.level_index : SM64_LEVEL_BOB_INDEX);
    sStartLevelNum = start->level_num;
    if (sm64_host_story()) { sStartLevelNum = 0; sFollowKeys = 1; }
    if (sSetup.has_stars) { sm64_host_seed_course_stars(start->course, sSetup.stars); }
    sm64_host_start_level(start->level_num, sSetup.has_act ? sSetup.act : SM64_HOST_NEXT_ACT);
    apply_scale();
    return 1;
}
static const char *a_err(void) { return sErr; }
static float area_anchor(int level, int area) {
    const int rule = sm64_level_anchor_rule(level, area);
    float y;
    if (rule == SM64_ANCHOR_ENTRY ? sm64_host_entry_ground(&y) : sm64_host_area_ground(rule, &y)) { return y; }
    return sm64_host_ground_height();
}
static int anchor_known(int level, int area) {
    return level == sAnchorLevel && (area == sAnchorArea || sm64_level_anchor_rule(level, area) == SM64_ANCHOR_ENTRY);
}
static void refresh_level(void) {
    if (sm64_host_story()) {
        float mn[3], mx[3];
        sm64_host_level_bounds(mn, mx);
        if (!sm64_host_level_ready() && !(mx[0] >= mn[0])) { return; }
    }
    /* The boot's pass through another level on its way to the start level keeps the floor-mode plane: those ticks are part of
     * BoB's recorded route, whose stereo audio must stay byte-identical; every level the game is in afterwards gets
     * its area's anchor (area_anchor). */
    const int booting = !sFollowKeys && sm64_host_level() != sStartLevelNum;
    const int lv = sm64_host_level(), ar = sm64_host_area();
    if (booting) {
        sLevel.ground_y = sm64_host_ground_plane();
    } else {
        if (!anchor_known(lv, ar) && sm64_host_level_ready()) { sAnchorY = area_anchor(lv, ar); sAnchorLevel = lv; sAnchorArea = ar; }
        sLevel.ground_y = anchor_known(lv, ar) ? sAnchorY : sm64_host_ground_height();
    }
    sm64_host_level_bounds(sLevel.bounds_min, sLevel.bounds_max);
    for (int k = 0; k < 3; k++) { sInfoOff[k] = 0.f; }
    sKeyLevel = sm64_host_level(); sKeyArea = sm64_host_area();
    if (sKeyLevel == sm64_host_home_level()) { sFollowKeys = 1; }
}
void sm64_ar_level_bounds(float mn[3], float mx[3]) {
    static int key_level = -1, key_area = -1;
    static float own_mn[3], own_mx[3];
    const int lv = sm64_host_level(), ar = sm64_host_area();
    const float *a = sLevel.bounds_min, *b = sLevel.bounds_max;
    if (lv != sKeyLevel || ar != sKeyArea || !(b[0] > a[0])) {
        if (lv != key_level || ar != key_area) {
            /* Never the previous level's: its collision may still be loaded until this level is ready. */
            memset(own_mn, 0, sizeof own_mn); memset(own_mx, 0, sizeof own_mx);
            if (sm64_host_level_ready()) {
                sm64_host_level_bounds(own_mn, own_mx);
                if (own_mx[0] > own_mn[0]) { key_level = lv; key_area = ar; }
            }
        }
        a = own_mn; b = own_mx;
    }
    memcpy(mn, a, sizeof(float) * 3); memcpy(mx, b, sizeof(float) * 3);
}
/* 0 = off; else bit 63 set and x, y, z as int16 (x 32767) in bits 32..47, 16..31, 0..15. One word: no torn reads. */
static _Atomic uint32_t sPlayerFocusBits;
void ar_adapter_sm64_set_player_focus(float s) { uint32_t b; memcpy(&b, &s, sizeof b); atomic_store(&sPlayerFocusBits, b); }
static _Atomic uint32_t sObjectFocusBits;
void ar_adapter_sm64_set_object_focus(float s) { uint32_t b; memcpy(&b, &s, sizeof b); atomic_store(&sObjectFocusBits, b); }
/* The music setting in one word: bit 0 on, bits 1..23 size x 1000, bits 24..39 rotation x 100 (degrees per second); 0 = box. */
static _Atomic uint64_t sMusicRing;
void ar_adapter_sm64_set_music_ring(int on, float size, float rotate_dps) {
    const uint64_t sz = (uint64_t)lrintf((size > 0.f && size < 8000.f ? size : 1.f) * 1000.f) & 0x7FFFFFu;
    const uint64_t r = (uint64_t)lrintf((rotate_dps > 0.f && rotate_dps < 600.f ? rotate_dps : 0.f) * 100.f) & 0xFFFFu;
    atomic_store(&sMusicRing, (uint64_t)(on ? 1u : 0u) | (sz << 1) | (r << 24));
}
/* The ring's listener settings in one word: bit 0 at head, bits 1..11 focus x 1000. */
static _Atomic uint32_t sRingListener;
void ar_adapter_sm64_set_ring_listener(int at_head, float focus) {
    const uint32_t f = (uint32_t)lrintf((focus > 0.f ? (focus < 1.f ? focus : 1.f) : 0.f) * 1000.f);
    atomic_store(&sRingListener, (at_head ? 1u : 0u) | (f << 1));
}
static _Atomic uint64_t sKeyLight;
static uint64_t sKeyLightApplied;
void ar_adapter_sm64_set_normal_flags(uint8_t mask) { sm64_capture_set_normal_flags(mask); }
void ar_adapter_sm64_set_key_light(int on, const float d[3]) {
    const double len = d ? sqrt((double)d[0] * d[0] + (double)d[1] * d[1] + (double)d[2] * d[2]) : 0.0;
    if (!on || !(len > 1e-6)) { atomic_store(&sKeyLight, 0); return; }
    uint64_t v = 1ull << 63;
    for (int k = 0; k < 3; k++) {
        const int16_t q = (int16_t)lrint(d[k] / len * 32767.0);
        v |= (uint64_t)(uint16_t)q << (32 - 16 * k);
    }
    atomic_store(&sKeyLight, v);
}
static void apply_key_light(void) {
    uint64_t v = atomic_load(&sKeyLight);
    /* No steering while a gd head is up: gd's lights orbit in its camera frame, not SM64's (0x28,0x28,0x28), so the
     * steering sent them above-behind the face. The splash logo is SM64's own geometry and stays steered. */
    const int front = sm64_settings_choice("title_light") == 0;
    sm64_capture_set_goddard_self_lit(front);
    if (front && sm64_host_screen() == SM64_SCREEN_TITLE && !sm64_host_title_splash()) { v = 0; }
    if (v == sKeyLightApplied) { return; }
    sKeyLightApplied = v;
    if (!(v >> 63)) { sm64_capture_clear_light_dir(); return; }
    float dir[3];
    for (int k = 0; k < 3; k++) { dir[k] = (float)(int16_t)(uint16_t)(v >> (32 - 16 * k)) / 32767.f; }
    sm64_capture_set_light_dir(dir);
}
void ar_adapter_sm64_test_clear_light_view(void) { sKeyLightApplied = 0; sm64_capture_clear_light_dir(); }
static void a_tick(const ar_player_input *p, uint32_t n) {
    apply_scale();
    sMenuW = sm64_menu_width_m(sm64_settings_choice("menu_size"));
    sm64_ar_view_set_units_per_meter(sLevel.units_per_meter);
    const int tap_a = menu_tap_step();
    const int grab_a = head_touch_step();
    eyes_step(n > 0 && p[0].present ? p[0].head_in_table.pos : NULL);
    for (int k = 0; k < 3; k++) { sTitleHead[k] = NAN; }
    if (n > 0 && p[0].present) {
        sm64_pad pad = sm64_pad_mask_ar(sm64_pad_from_ar(&p[0].pad));
        if (tap_a || grab_a) { pad.buttons |= SM64_N64_A; }
        sm64_host_set_pad(&pad);
        float head_level[3];
        ar_level_from_table_point(sLevel.units_per_meter, sLevel.ground_y, p[0].head_in_table.pos, head_level);
        sm64_ar_view_set_head(head_level, p[0].head_in_table.quat);
        for (int k = 0; k < 3; k++) { sTitleHead[k] = p[0].head_in_table.pos[k]; }
        float frame_level[3];
        ar_level_from_table_point(sLevel.units_per_meter, sLevel.ground_y, p[0].input_frame_in_table.pos, frame_level);
        sm64_ar_view_set_control_frame(frame_level, p[0].input_frame_in_table.quat);
        sm64_capture_set_head_rotation(p[0].head_in_table.quat);
        float box_min[3], box_max[3];
        for (int k = 0; k < 3; k++) {
            const float off = (k == 1 ? sLevel.ground_y : 0.f) + sInfoOff[k];
            box_min[k] = (sLevel.bounds_min[k] - off) / sLevel.units_per_meter;
            box_max[k] = (sLevel.bounds_max[k] - off) / sLevel.units_per_meter;
        }
        const sm64_audio_frame af = { sLevel.units_per_meter, sLevel.ground_y, p[0].head_in_table,
                                      { box_min[0], box_min[1], box_min[2] }, { box_max[0], box_max[1], box_max[2] } };
        sm64_audio_set_tick_frame(&af);
        sm64_audio_set_music_gain(ar_music_box_gain(p[0].head_in_table.pos, box_min, box_max, SM64_MUSIC_FALLOFF_PER_M));
    } else if (tap_a || grab_a || sNoPlayerA) {
        const sm64_pad pad = { (tap_a || grab_a) ? SM64_N64_A : 0, 0, 0 };
        sm64_host_set_pad(&pad);
        sNoPlayerA = tap_a || grab_a;
    }
    title_place_tick();
    if (sSetup.has_lives && !sLivesApplied && sm64_host_level() == sStartLevelNum) { sm64_host_set_lives(sSetup.lives); sLivesApplied = 1; }
    if (sSetup.has_kill_at && sTicks == sSetup.kill_at) { sm64_host_test_kill_mario(); }
    if (sSetup.has_warp && sTicks == sSetup.warp_at) { sm64_host_test_warp_node(sSetup.warp_node); }
    const int req = atomic_exchange(&sLevelReq, 0);
    if (req) { sm64_host_request_warp(sm64_level_at(req / 16 - 1)->level_num, req % 16 - 1); }
    apply_key_light();
    { const uint32_t b = atomic_load(&sPlayerFocusBits); float s; memcpy(&s, &b, sizeof s); sm64_audio_set_player_focus(s); }
    { const uint32_t b = atomic_load(&sObjectFocusBits); float s; memcpy(&s, &b, sizeof s); sm64_audio_set_object_focus(s); }
    {
        const uint64_t v = atomic_load(&sMusicRing);
        sm64_audio_set_music_ring((int)(v & 1u), v ? (float)((v >> 1) & 0x7FFFFFu) / 1000.f : 1.f, (float)((v >> 24) & 0xFFFFu) / 100.f);
    }
    { const uint32_t v = atomic_load(&sRingListener); sm64_audio_set_ring_listener((int)(v & 1u), (float)(v >> 1) / 1000.f); }
    {
        const sm64_audio_options ao = { sm64_settings_choice("sound") != 1, sm64_settings_choice("audio") != 1,
                                        sm64_settings_choice("reverb") == 1, sm64_settings_choice("event_music") != 1 };
        sm64_audio_set_options(&ao);
    }
    sm64_capture_begin_tick();
    sm64_audio_tick();
    sm64_host_tick();
    sm64_audio_tick_end();
    const int prev_screen = sScreen;
    sScreen = sm64_host_screen();
    title_tick(prev_screen);
    sScreenTicks = sScreen != prev_screen ? 0 : sScreenTicks + 1;
    if (sScreen != prev_screen) { apply_scale(); }
    const int moved = sFollowKeys && sm64_host_level_ready() && (sm64_host_level() != sKeyLevel || sm64_host_area() != sKeyArea);
    const int tick30 = sTicks++ % 30 == 0;
    /* Never while a menu is up: the file select and the act selector load main_menu collision. */
    if (sScreen == SM64_SCREEN_LEVEL || sScreen == SM64_SCREEN_CREDITS) {
        if (tick30 || moved || sScreen != prev_screen) { refresh_level(); }
    }
    sm64_capture_set_world_offset(sTitleOff);
    sm64_capture_set_floating(sInfoOff[0] != 0.f || sInfoOff[1] != 0.f || sInfoOff[2] != 0.f);
    sm64_capture_end_tick(&sLevel);
    publish_state();
    publish_pointer_geo();
}
int ar_adapter_sm64_visibility(const float eye_table[3], int *mario_visible, float *floor_fraction) {
    float eye[3];
    ar_level_from_table_point(sLevel.units_per_meter, sLevel.ground_y, eye_table, eye);
    return sm64_host_visibility(eye, mario_visible, floor_fraction);
}
static const fc_frame *a_acquire(void) { return sRing ? ar_ring_acquire_latest(sRing) : NULL; }
static void a_release(const fc_frame *f) { if (sRing) { ar_ring_release(sRing, f); } }
static uint32_t a_drain(uint64_t up_to, ar_texture_upload_fn fn, void *user) { ar_texture_fifo *f = sFifo; return f ? ar_texfifo_drain(f, up_to, fn, user) : 0; }
static ar_level_info a_level(void) { return sLevel; }
static uint32_t a_audio(ar_audio_pull *p) { return sm64_audio_pull_scene(p); }
static void a_set_mode(uint32_t mode) { sm64_audio_set_mode(mode); }
static uint64_t a_hash(void) { return sm64_host_state_hash(); }
static const char *a_status(void) {
    static char line[128];
    snprintf(line, sizeof line, "level=%d act=%d lives=%d coins=%d health=0x%x stars=0x%x home=%d", sm64_host_level(),
             sm64_host_act(), sm64_host_mario_lives(), sm64_host_mario_coins(), sm64_host_mario_health(),
             sm64_host_course_star_flags(sm64_host_course()), sm64_host_home_level());
    return line;
}

static const ar_adapter sAdapter = { AR_ADAPTER_ABI_VERSION, "sm64", a_init, a_err, a_tick, a_acquire, a_release,
                                     a_drain, a_level, a_audio, a_hash, a_status, a_set_mode };
const ar_adapter *ar_adapter_sm64(void) { return &sAdapter; }
void ar_adapter_sm64_set_resource_dir(const char *dir) { sm64_host_set_resource_dir(dir); }
void ar_adapter_sm64_set_pack_stamps_required(int on) { sm64_host_set_pack_stamps_required(on); }
void ar_adapter_sm64_set_bundled_pack_dir(const char *dir) { sm64_host_set_bundled_pack_dir(dir); }
int ar_adapter_sm64_pack_stamps(int *refused, double *ms) { return sm64_host_pack_stamps(refused, ms); }
const char *ar_adapter_sm64_pack_refusal(int i) { return sm64_host_pack_refusal(i); }

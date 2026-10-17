#include <math.h>
#include <pthread.h>
#include <string.h>
#include "fc_builder.h"
#include "sm64_look.h"
#include "sm64_host.h"
#include "sm64_levels.h"
#include "sm64_settings.h"
#include "sm64_story.h"
#include "sm64_ar_internal.h"

static int sFloorLevel = -1, sFloorArea = -1;
static unsigned sFloorInits;
static float sLowest = NAN;
static fc_look sLast;
static int sHaveLast;
static pthread_mutex_t sMu = PTHREAD_MUTEX_INITIALIZER;
static sm64_trans_state sTrans;
static const fc_builder *sLookB;
static fc_look sLookOn;
static sm64_cut_state sCut;
#define SM64_FOCUS_OWNERS 16   /* MAX_PLAYERS: 0003-Capture-hooks-and-draw-tags.patch tags player i's geometry as owner i + 1 */
static const fc_focus_style kNoSilhouette[SM64_FOCUS_OWNERS];
void sm64_ar_look_reset(void) {
    sFloorLevel = -1; sFloorArea = -1; sFloorInits = 0; sLowest = NAN; sLookB = NULL;
    memset(&sTrans, 0, sizeof sTrans);
    memset(&sCut, 0, sizeof sCut);
    pthread_mutex_lock(&sMu); memset(&sLast, 0, sizeof sLast); sHaveLast = 0; pthread_mutex_unlock(&sMu);
}
int ar_adapter_sm64_look(fc_look *out) {
    pthread_mutex_lock(&sMu);
    const int have = sHaveLast;
    if (have && out) { *out = sLast; }
    pthread_mutex_unlock(&sMu);
    return have;
}
static float lowest_floor(void) {
    const int lv = sm64_host_level(), ar = sm64_host_area();
    const unsigned inits = sm64_host_level_inits();
    if (lv != sFloorLevel || ar != sFloorArea || inits != sFloorInits) {
        sLowest = NAN;
        float y;
        if (sm64_host_area_lowest_floor(&y)) { sLowest = y; }
        sFloorLevel = lv; sFloorArea = ar; sFloorInits = inits;
    }
    return sLowest;
}
void sm64_ar_look_frame(fc_builder *b) {
    fc_look L;
    memset(&L, 0, sizeof L);
    if (sm64_trans_due(&sTrans, sm64_host_frame_count())) {
        sm64_warp_sample w;
        sm64_host_warp_sample(&w);
        sm64_trans_step(&sTrans, &w);
    }
    int silhouette = 1, cut = 0;
    sm64_behind_walls(sm64_settings_choice("behind_walls"), &silhouette, &cut);
    fc_builder_set_focus_styles(b, silhouette ? NULL : kNoSilhouette, silhouette ? 0u : SM64_FOCUS_OWNERS);
    const int level_screen = sm64_host_screen() == SM64_SCREEN_LEVEL && sm64_host_level() != 0;
    float mn[3], mx[3], m[3];
    if (level_screen) { sm64_ar_level_bounds(mn, mx); sm64_host_mario_gfx_pos(m); }
    if (level_screen && sm64_host_level_ready()) {
        sm64_look_clip(sm64_settings_choice("below_table") == 0, sm64_settings_choice("trim_level") == 0, sm64_ar_ground_y(),
                       lowest_floor(), mn, mx, sm64_ar_units_per_meter(), &L);
        if (sm64_settings_choice("liquids") == 0) {
            float boxes[FC_LOOK_MAX_WATER * 5];
            const int n = sm64_host_water_boxes(boxes, FC_LOOK_MAX_WATER);
            sm64_look_water(boxes, n < FC_LOOK_MAX_WATER ? n : FC_LOOK_MAX_WATER, sm64_host_frame_count(), &L);
            if (sm64_settings_choice("water_trim") == 0) { L.flags |= FC_LOOK_WATER_TRIM; }
        }
        sm64_look_cut(cut, sm64_host_mario_drawn(), m, sm64_ar_units_per_meter(), &L);
    }
    sm64_look_cut_prev(&sCut, sm64_host_frame_count(), &L);
    /* On level-screen frames, ready or not (in Story, vanilla whitens the castle while Mario is inside the painting,
     * his object inactive). */
    if (level_screen) {
        sm64_look_transition(sm64_settings_choice("transitions") == 0, &sTrans, sm64_host_mario_hitbox_height() > 0.f ? m : NULL,
                             mn, mx, sm64_ar_units_per_meter(), &L);
        sm64_look_trans_fade(sm64_settings_choice("trans_fade") == 0, sm64_settings_choice("transitions") == 0, &sTrans, &L);
    }
    if (sm64_settings_choice("fine_textures") == 0) { L.flags |= FC_LOOK_FINE_TEXTURES; }
    fc_builder_set_look(b, &L);
    sLookB = b; sLookOn = L;
    pthread_mutex_lock(&sMu); sLast = L; sHaveLast = 1; pthread_mutex_unlock(&sMu);
}
void sm64_ar_look_float(fc_builder *b) {
    if (b == NULL || b != sLookB) { return; }
    fc_look L = sLookOn;
    L.flags |= FC_LOOK_FLOATING;
    fc_builder_set_look(b, &L);
    pthread_mutex_lock(&sMu); sLast = L; pthread_mutex_unlock(&sMu);
}

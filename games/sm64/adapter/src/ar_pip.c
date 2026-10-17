#include <string.h>
#include "fc_builder.h"
#include "sm64_pip.h"
#include "sm64_host.h"
#include "sm64_story.h"
#include "sm64_settings.h"
#include "sm64_ar_hud.h"
#include "sm64_ar_internal.h"
#include "sm64_ar_view.h"

static sm64_pip_fade sFade;
static uint32_t sStepFrame;
static int sStepped;
static int sShown;
static unsigned sInits;
static int sArea = -1;
static float sAnchor[3];
static float sWidth = SM64_PIP_WIDTH_M;
static sm64_pip_follow sFollow;
void sm64_ar_pip_reset(void) {
    memset(&sFade, 0, sizeof sFade); sStepFrame = 0; sStepped = 0; sShown = 0; sInits = 0; sArea = -1;
    sm64_pip_follow_reset(&sFollow); sWidth = SM64_PIP_WIDTH_M;
}
float sm64_ar_pip_alpha(void) { return sShown ? sm64_pip_alpha(&sFade) : 0.f; }
int sm64_ar_pip_anchor(float out[3]) {
    if (!sShown) { return 0; }
    for (int k = 0; k < 3; k++) { out[k] = sAnchor[k]; }
    return 1;
}
int sm64_ar_pip_hides(const float a[3], float w_m, float h_m) {
    return sShown && sm64_pip_panels_overlap(sAnchor, sWidth, sWidth / SM64_PIP_ASPECT, a, w_m, h_m, sm64_ar_units_per_meter());
}
static int sSky = 1;
void sm64_ar_pip_set_sky(int on) { sSky = on != 0; }
int sm64_ar_pip_sky_wanted(void) {   /* sm64_host_tick, before the game's render: vanilla's background for the PiP (0010-HUD-menus-and-screen-scenes.patch) */
    return sSky && sm64_settings_choice("intro_pip") == 0 && sm64_host_screen() == SM64_SCREEN_LEVEL && sm64_host_intro_shot() >= 0;
}
void sm64_ar_pip_frame(fc_builder *b) {
    const int scr = sm64_host_screen();
    const int shot = sm64_host_intro_shot();
    const int want = sm64_pip_wanted(sm64_settings_choice("intro_pip") == 0, scr == SM64_SCREEN_LEVEL, shot >= 0, shot,
                                     sm64_host_dialog_open(), sm64_host_paused());
    const unsigned inits = sm64_host_level_inits();
    const int area = sm64_host_area();
    if (scr != SM64_SCREEN_LEVEL || inits != sInits || area != sArea) { sFade.level = 0; }
    sInits = inits; sArea = area;
    const uint32_t fr = sm64_host_frame_count();
    const int new_tick = !sStepped || fr != sStepFrame;
    if (new_tick) { sStepped = 1; sStepFrame = fr; sm64_pip_fade_step(&sFade, want); }
    const float alpha = sm64_pip_alpha(&sFade);
    const int follow = sm64_settings_choice("pip_follow") == 0;
    sShown = 0;
    fc_overlay_group g;
    memset(&g, 0, sizeof g);
    g.id = SM64_HUD_GROUP_PIP;
    if (alpha > 0.f && scr == SM64_SCREEN_LEVEL) {
        float mn[3], mx[3], head[3], q[4], a[3], view[16], roll[16], proj[16];
        fc_game_view gv;
        memset(&gv, 0, sizeof gv);
        sm64_ar_level_bounds(mn, mx);
        sm64_ar_view_get_head(head, q);
        const int cam = sm64_host_game_camera(SM64_PIP_ASPECT, view, roll, proj, gv.eye);
        const float upm = sm64_ar_units_per_meter();
        const int placed = follow ? cam && sm64_pip_follow_step(&sFollow, gv.eye, upm, new_tick ? 1.f / 30.f : 0.f, a)
                                  : sm64_pip_anchor(mn, mx, sm64_ar_ground_y(), upm, head, a);
        if (placed && cam && sm64_pip_clip_from_level(view, roll, proj, gv.clip_from_level)) {
            static const uint8_t kSky[4] = SM64_PIP_SKY_RGBA;
            gv.aspect = SM64_PIP_ASPECT;
            memcpy(gv.clear_rgba, kSky, sizeof kSky);
            fc_builder_set_game_view(b, &gv);
            g.anchor = FC_ANCHOR_WORLD_POINT; g.alpha = alpha;
            g.flags = FC_GROUP_YAW_ONLY | FC_GROUP_GAME_VIEW | (follow ? FC_GROUP_ON_TOP : 0);
            for (int k = 0; k < 3; k++) { g.world_pos[k] = a[k]; sAnchor[k] = a[k]; }
            sWidth = follow ? SM64_PIP_FOLLOW_WIDTH_M : SM64_PIP_WIDTH_M;
            g.width_m = sWidth;
            g.min_angle_rad = (follow ? SM64_PIP_FOLLOW_MIN_DEG : SM64_PIP_MIN_DEG) * 0.0174532925f;
            sShown = 1;
        }
    }
    if (!sShown || !follow) { sm64_pip_follow_reset(&sFollow); }
    fc_builder_set_group(b, &g);   /* hidden unless shown: the group's entry comes first (world points draw in that order) */
}

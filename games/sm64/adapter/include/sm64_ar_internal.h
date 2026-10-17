#pragma once
#include <stdint.h>
#include "fc_builder.h"
#ifdef __cplusplus
extern "C" {
#endif
void sm64_ar_hud_render(void);
void sm64_host_render_hud(void);            /* gArGameHooks->render_hud: test-forced HUD flags, then the AR HUD (sm64_host.c) */
void sm64_ar_hud_frame(fc_builder *b);
void sm64_ar_pip_frame(fc_builder *b);
void sm64_ar_pip_reset(void);
void sm64_ar_look_reset(void);
void sm64_ar_look_frame(fc_builder *b);
void sm64_ar_look_float(fc_builder *b);
int sm64_ar_pip_hides(const float a[3], float w_m, float h_m);
float sm64_ar_pip_alpha(void);
/* vanilla's skybox behind the PiP this tick (gArPipSky, 0010-HUD-menus-and-screen-scenes.patch) */
int sm64_ar_pip_sky_wanted(void);
void sm64_ar_pip_set_sky(int on);
int sm64_ar_pip_anchor(float out[3]);
float sm64_ar_floor_at(void *user, float x, float y, float z);
void sm64_ar_cannon_frame(fc_builder *b);
void sm64_ar_level_bounds(float mn[3], float mx[3]);
float sm64_ar_units_per_meter(void);
float sm64_ar_ground_y(void);
void sm64_ar_menu_anchor(float out[3]);
float sm64_ar_menu_width_m(void);
int sm64_ar_title_box(float mn[3], float mx[3]);
void sm64_ar_title_offset(float out[3]);
int sm64_ar_head_grab(uint64_t *id);
void sm64_host_mark_warp(int level_num);   /* a hook issued a dynos warp: it counts as in flight (sm64_host.c) */
#ifdef __cplusplus
}
#endif

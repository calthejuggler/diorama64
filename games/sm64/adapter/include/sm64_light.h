#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_ROLE_TERRAIN = 0, SM64_ROLE_OBJECT = 1, SM64_ROLE_SHADOW = 2, SM64_ROLE_EFFECT = 3, SM64_ROLE_GODDARD = 4,
       SM64_ROLE_WATER = 5, SM64_ROLE_LAVA = 6 };
/* = GFX_AR_ROLE_* (0005-Draw-roles-and-light-view.patch) */
enum { SM64_MAT_LIT = 1, SM64_MAT_TEXGEN = 2 };                               /* = GFX_AR_MAT_* */
uint8_t sm64_light_flags(int role, int material, uint8_t render_flags, uint32_t cc_opts);
uint8_t sm64_light_role(int role, int material, int owner_metal, int goddard_self_lit, int splash);
/* SM64 authors its lights at (0x28, 0x28, 0x28) in eye space, so gfx_ar_set_light_view(R) makes them shine from
 * dir_level (towards the light, level axes). The roll about dir_level is free. A zero vector counts as straight up. */
void sm64_light_view_for_dir(const float dir_level[3], float m[4][4]);
#ifdef __cplusplus
}
#endif

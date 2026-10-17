#include "sm64_light.h"
#include <math.h>
#include <string.h>
#include "fc_frame.h"
uint8_t sm64_light_flags(int role, int material, uint8_t render_flags, uint32_t cc_opts) {
    if (role == SM64_ROLE_WATER || role == SM64_ROLE_LAVA) { role = SM64_ROLE_TERRAIN; }
    if (role == SM64_ROLE_SHADOW) { return FC_LF_FAKE_SHADOW; }
    const int lit = (material & SM64_MAT_LIT) != 0;
    uint8_t f = FC_LF_TINT | FC_LF_RECEIVER | (lit ? FC_LF_GAME_LIT : 0);
    if (role == SM64_ROLE_TERRAIN) { f |= FC_LF_STATIC | (lit ? 0 : FC_LF_BAKED); }
    else if (role != SM64_ROLE_EFFECT) { f |= FC_LF_CASTER; }
    if (material & SM64_MAT_TEXGEN) { f |= FC_LF_REFLECTIVE; }
    /* water, smoke: blended without a depth write. Fog-mode opaque terrain (G_RM_FOG_SHADE_A) also blends, but writes
     * depth; cutouts (trees) are alpha-tested. */
    const int see_through = (render_flags & FC_RS_BLEND) && !(render_flags & FC_RS_DEPTH_WRITE) &&
                            !(cc_opts & FC_CC_TEXTURE_EDGE);
    if (see_through) { f &= (uint8_t)~(FC_LF_CASTER | FC_LF_STATIC); }
    return f;
}
uint8_t sm64_light_role(int role, int material, int owner_metal, int goddard_self_lit, int splash) {
    if (role == SM64_ROLE_GODDARD) { return (goddard_self_lit && !splash) ? FC_LR_SELF_LIT : 0; }
    if (role == SM64_ROLE_WATER) { return FC_LR_WATER; }
    if (role == SM64_ROLE_LAVA) { return FC_LR_LAVA; }
    if (role == SM64_ROLE_OBJECT && owner_metal && (material & SM64_MAT_TEXGEN)) { return FC_LR_MIRROR; }
    return 0;
}
static void frame_of(const double x[3], double f[3][3]) {
    const int steep = fabs(x[1]) >= 0.99;
    const double ref[3] = { 0.0, steep ? 0.0 : 1.0, steep ? 1.0 : 0.0 };
    double t[3] = { ref[1] * x[2] - ref[2] * x[1], ref[2] * x[0] - ref[0] * x[2], ref[0] * x[1] - ref[1] * x[0] };
    const double tl = sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    for (int k = 0; k < 3; k++) { t[k] /= tl; }
    const double b[3] = { x[1] * t[2] - x[2] * t[1], x[2] * t[0] - x[0] * t[2], x[0] * t[1] - x[1] * t[0] };
    for (int k = 0; k < 3; k++) { f[k][0] = t[k]; f[k][1] = b[k]; f[k][2] = x[k]; }
}
void sm64_light_view_for_dir(const float dir_level[3], float m[4][4]) {
    double d[3] = { dir_level[0], dir_level[1], dir_level[2] };
    double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(l > 1e-9)) { d[0] = 0.0; d[1] = 1.0; d[2] = 0.0; l = 1.0; }
    for (int k = 0; k < 3; k++) { d[k] /= l; }
    const double s = 1.0 / sqrt(3.0);
    const double a[3] = { s, s, s };
    double fd[3][3], fa[3][3];
    frame_of(d, fd);
    frame_of(a, fa);
    memset(m, 0, sizeof(float) * 16);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double v = 0.0;
            for (int k = 0; k < 3; k++) { v += fd[i][k] * fa[j][k]; }
            m[i][j] = (float)v;
        }
    }
    m[3][3] = 1.f;
}

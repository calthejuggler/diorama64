#include "sm64.h"
#include "game/level_update.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "fc_builder.h"
#include "sm64_cannon.h"
#include "sm64_ar_internal.h"

float sm64_ar_floor_at(void *user, float x, float y, float z) {
    (void)user;
    struct Surface *s = NULL;
    const float h = find_floor(x, y, z, &s);
    return s ? h : -1e9f;
}
void sm64_ar_cannon_frame(fc_builder *b) {
    struct MarioState *m = &gMarioStates[0];
    if (m->action != ACT_IN_CANNON || m->actionState != 2) { return; }
    static float pts[3 * SM64_CANNON_ARC_MAX_POINTS];
    const uint32_t n = sm64_cannon_arc(m->pos, sins(m->faceAngle[0]), coss(m->faceAngle[0]), sins(m->faceAngle[1]),
                                       coss(m->faceAngle[1]), sm64_ar_floor_at, NULL, pts, SM64_CANNON_ARC_MAX_POINTS,
                                       SM64_CANNON_ARC_MAX_TICKS);
    const uint8_t yellow[4] = { 255, 236, 64, 255 };
    fc_builder_add_polyline(b, pts, n, yellow, SM64_CANNON_ARC_WIDTH_M);
}

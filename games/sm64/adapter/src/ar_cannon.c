#include "sm64_cannon.h"
uint32_t sm64_cannon_arc(const float start[3], float sp, float cp, float sy, float cy, sm64_floor_fn floor, void *user,
                         float *xyz, uint32_t cap, uint32_t max_ticks) {
    uint32_t n = 0;
    float pos[3] = { start[0] + 120.f * cp * sy, start[1] + 120.f * sp, start[2] + 120.f * cp * cy };
    float fwd = 100.f * cp, vy = 100.f * sp;
#define PUT() do { if (n < cap) { xyz[n * 3] = pos[0]; xyz[n * 3 + 1] = pos[1]; xyz[n * 3 + 2] = pos[2]; n++; } } while (0)
    PUT();
    for (uint32_t t = 0; t < max_ticks && n < cap; t++) {
        const float vx = sy * fwd, vz = cy * fwd;                         /* mario_set_forward_vel */
        for (int q = 0; q < 4; q++) {                                     /* perform_air_step: 4 quarter steps */
            const float next[3] = { pos[0] + vx / 4.f, pos[1] + vy / 4.f, pos[2] + vz / 4.f };
            const float fy = floor(user, next[0], next[1], next[2]);
            if (fy <= -10000.f) { return n; }
            pos[0] = next[0]; pos[1] = next[1]; pos[2] = next[2];
            if (pos[1] <= fy) { pos[1] = fy; PUT(); return n; }           /* AIR_STEP_LANDED */
            PUT();
        }
        vy -= 1.f;                                                        /* apply_gravity, ACT_SHOT_FROM_CANNON */
        if (vy < -75.f) { vy = -75.f; }
        fwd = (float)((double)fwd - 0.05);                                /* m->forwardVel -= 0.05 (a double literal) */
        if (fwd < 10.f) { fwd = 10.f; }
    }
#undef PUT
    return n;
}

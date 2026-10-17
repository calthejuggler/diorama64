#include <math.h>
#include <stddef.h>
#include "sm64_story.h"
#include "sm64_touch.h"
static int finite3(const float v[3]) { return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
int sm64_touch_hands_allowed(int mode) { return mode == SM64_TOUCH_BOTH || mode == SM64_TOUCH_HANDS; }
int sm64_touch_at_face(const float p[3], const float mn[3], const float mx[3]) {
    if (!finite3(p) || !finite3(mn) || !finite3(mx)) { return 0; }
    for (int k = 0; k < 3; k++) {
        if (p[k] < mn[k] - SM64_TITLE_TOUCH_MARGIN_M || p[k] > mx[k] + SM64_TITLE_TOUCH_MARGIN_M) { return 0; }
    }
    return 1;
}
int sm64_touch_begin(int mode, int has_hand, const float hand[3], int has_ray, const float ray_o[3], const float ray_d[3],
                     const float mn[3], const float mx[3], float hit[3]) {
    if (sm64_touch_hands_allowed(mode) && has_hand && sm64_touch_at_face(hand, mn, mx)) {
        for (int k = 0; k < 3; k++) { hit[k] = hand[k]; }
        return SM64_TOUCH_DIRECT;
    }
    if (!has_ray || !sm64_ray_box(ray_o, ray_d, mn, mx, hit)) { return SM64_TOUCH_NONE; }
    return mode == SM64_TOUCH_HANDS ? SM64_TOUCH_IGNORED : SM64_TOUCH_GAZE;
}
const char *sm64_touch_kind_name(int kind) {
    switch (kind) {
    case SM64_TOUCH_DIRECT: return "pinch";
    case SM64_TOUCH_GAZE: return "gaze";
    default: return NULL;
    }
}

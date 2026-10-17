#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_TOUCH_MODE_BOTH = 0, SM64_TOUCH_MODE_HANDS = 1, SM64_TOUCH_MODE_PINCH = 2 };
#define SM64_TOUCH_BOTH SM64_TOUCH_MODE_BOTH
#define SM64_TOUCH_HANDS SM64_TOUCH_MODE_HANDS
#define SM64_TOUCH_PINCH SM64_TOUCH_MODE_PINCH
#define SM64_TITLE_TOUCH_MARGIN_M 0.03f
int sm64_touch_hands_allowed(int mode);
int sm64_touch_at_face(const float p[3], const float mn[3], const float mx[3]);
enum { SM64_TOUCH_NONE = 0, SM64_TOUCH_DIRECT = 1, SM64_TOUCH_GAZE = 2, SM64_TOUCH_IGNORED = 3 };
int sm64_touch_begin(int mode, int has_hand, const float hand[3], int has_ray, const float ray_o[3], const float ray_d[3],
                     const float mn[3], const float mx[3], float hit[3]);
const char *sm64_touch_kind_name(int kind);
typedef struct sm64_hand_tips { int tracked; float index_tip[3]; } sm64_hand_tips;
#ifdef __cplusplus
}
#endif

#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_AR_LOOP_ACTS 6
int sm64_next_act(uint32_t course_star_flags);
/* One warp seen by the before_warp hook or a game over (0008-Story-mode-and-game-flow.patch). home_course 0: a level
 * without acts. */
typedef struct sm64_loop_warp {
    int home_level, home_course;
    int cur_level, cur_act;
    int dest_level;
    int dest_course;
    uint32_t dest_flags;
    int star_exit;
    uint32_t home_flags;
    int home_returns;
    int home_boss;
    int credits;
} sm64_loop_warp;
typedef struct sm64_loop_go { int take, level, act; } sm64_loop_go;
sm64_loop_go sm64_loop_on_warp(const sm64_loop_warp *w);
sm64_loop_go sm64_loop_on_game_over(const sm64_loop_warp *w);
#ifdef __cplusplus
}
#endif

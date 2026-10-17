#include "sm64_ar_loop.h"
int sm64_next_act(uint32_t flags) {
    for (int act = 1; act <= SM64_AR_LOOP_ACTS; act++) {
        if (!(flags & (1u << (act - 1)))) { return act; }
    }
    return 1;
}
static int keep_act(const sm64_loop_warp *w) {
    return (w->cur_level == w->home_level && w->cur_act >= 1 && w->cur_act <= SM64_AR_LOOP_ACTS) ? w->cur_act
                                                                                                  : sm64_next_act(w->home_flags);
}
static sm64_loop_go go_home(const sm64_loop_warp *w, int act) {
    sm64_loop_go g = { 1, w->home_level, w->home_course ? act : 0 };
    return g;
}
sm64_loop_go sm64_loop_on_warp(const sm64_loop_warp *w) {
    const sm64_loop_go none = { 0, 0, 0 };
    if (w->credits) { return go_home(w, sm64_next_act(w->home_flags)); }
    if (w->dest_level == w->cur_level) { return none; }
    if (w->star_exit) { return go_home(w, sm64_next_act(w->home_flags)); }
    if (w->home_returns) { return w->dest_level == w->home_boss ? none : go_home(w, 0); }
    if (w->home_course && w->cur_level == w->home_level) { return go_home(w, keep_act(w)); }
    if (w->dest_course) {
        const sm64_loop_go g = { 1, w->dest_level, sm64_next_act(w->dest_flags) };
        return g;
    }
    return none;
}
sm64_loop_go sm64_loop_on_game_over(const sm64_loop_warp *w) { return go_home(w, keep_act(w)); }

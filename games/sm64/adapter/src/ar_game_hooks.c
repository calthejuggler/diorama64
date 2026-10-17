#include "sm64.h"
#include "game/ar_game_hooks.h"
#include "behavior_table.h"
#include "game/rendering_graph_node.h"
#include "sm64_ar_hooks.h"
#include "sm64_ar_view.h"
#include "sm64_ar_trees.h"
#include "game/game_init.h"
#include "sm64_capture.h"
#include "sm64_ar_internal.h"

static s16 hook_control_yaw(struct MarioState *m) { return sm64_ar_control_yaw(m->pos); }

int sm64_ar_upright_behavior(int id) {
    return id >= 0 && (id == (int)id_bhvTree || id == (int)id_bhvOrangeNumber || id == (int)id_bhvStarNumber ||
                       id == (int)id_bhvBlueCoinNumber);
}
/* gCurGraphNodeProcessingObject is the object whose graph is being drawn, also during its GEO_BILLBOARD nodes
 * (number_geo). */
static void tree_crossed(f32 dest[4][4], f32 mtx[4][4], const f32 pos[3]) {
    float t[3], r[3], u[3], b[3];
    for (int k = 0; k < 3; k++) { t[k] = mtx[0][k] * pos[0] + mtx[1][k] * pos[1] + mtx[2][k] * pos[2] + mtx[3][k]; }
    sm64_trees_fixed_axes(t, r, u, b);
    for (int k = 0; k < 3; k++) { dest[0][k] = r[k]; dest[1][k] = u[k]; dest[2][k] = b[k]; dest[3][k] = t[k]; }
    dest[0][3] = 0.f; dest[1][3] = 0.f; dest[2][3] = 0.f; dest[3][3] = 1.f;
    sm64_trees_add(gGlobalTimer, t, r, u, b);
}
/* A tree a model pack draws in 3D: its vanilla graph still runs the billboard as the state pass, but no card is
 * drawn, so it is neither crossed nor registered (a 3D trunk face could lie in a card's plane and be copied). */
static int drawn_3d_by_pack(const struct Object *o) {
    const struct GraphNode *g = o && o->header.gfx.sharedChild ? dynos_ar_pack_graph(o->header.gfx.sharedChild) : NULL;
    return g && (g->extraFlags & GRAPH_EXTRA_FORCE_3D);
}
static void hook_billboard(f32 dest[4][4], f32 mtx[4][4], f32 pos[3], s32 cylinder) {
    const struct Object *o = gCurGraphNodeProcessingObject;
    const int id = o && o->behavior ? (int)get_id_from_behavior(o->behavior) : -1;
    if (id == (int)id_bhvTree && sm64_trees_style() == SM64_TREES_CROSSED && !drawn_3d_by_pack(o)) { tree_crossed(dest, mtx, pos); return; }
    sm64_ar_billboard(dest, (const float (*)[4])mtx, pos, sm64_ar_billboard_yaw_only(cylinder, sm64_ar_upright_behavior(id)));
}
static void hook_listener_vector(f32 out[3], f32 p[3]) { sm64_ar_listener_vector(p, out); }

#include "course_table.h"
#include "level_table.h"
#include "game/area.h"
#include "game/save_file.h"
#include "data/dynos.c.h"
#include "sm64_ar_loop.h"
#include "game/level_update.h"
static int is_node_warp(s16 id, s16 level, s16 area, s16 node) {
    const struct ObjectWarpNode *w = area_get_warp_node(id);
    return w && (w->node.destLevel & 0x7F) == level && w->node.destArea == area && w->node.destNode == node;
}
#include "sm64_host.h"
#include "pc/gfx/gfx_ar_hooks.h"
#include "sm64_levels.h"
static sm64_loop_warp loop_state(s16 dest, int star_exit) {
    sm64_loop_warp w = { 0 };
    w.home_level = sm64_host_home_level();
    const int course = get_level_course_num((s16)w.home_level);
    w.home_course = (course >= COURSE_MIN && course <= COURSE_STAGES_MAX) ? course : 0;
    w.cur_level = gCurrLevelNum; w.cur_act = gCurrActNum; w.dest_level = dest; w.star_exit = star_exit;
    const int dest_course = dest > 0 ? get_level_course_num(dest) : 0;
    w.dest_course = (dest_course >= COURSE_MIN && dest_course <= COURSE_STAGES_MAX) ? dest_course : 0;
    w.dest_flags = w.dest_course ? save_file_get_star_flags(gCurrSaveFileNum - 1, w.dest_course - 1) : 0;
    w.home_flags = w.home_course ? save_file_get_star_flags(gCurrSaveFileNum - 1, w.home_course - 1) : 0;
    const sm64_level_entry *e = sm64_level_at(sm64_level_index_of(w.home_level));
    w.home_returns = e && !e->course && !e->hub;
    w.home_boss = e ? e->boss_level : 0;
    return w;
}
/* Every warp (0008-Story-mode-and-game-flow.patch): the return rule (sm64_loop_on_warp) decides; vanilla handles what
 * it does not take. */
static s32 hook_before_warp(s16 level, s16 area, s16 node, s32 arg) {
    if (gArMode && gArStory) { return 0; }
    sm64_loop_warp w = loop_state(level, is_node_warp(WARP_NODE_F0, level, area, node));
    w.credits = node >= WARP_NODE_CREDITS_MIN;
    const sm64_loop_go g = sm64_loop_on_warp(&w);
    if (!g.take || !dynos_warp_to_level(g.level, 1, g.act)) { return 0; }
    sm64_host_mark_warp(g.level);
    if (w.credits) { gCurrCreditsEntry = NULL; }   /* WARP_OP_CREDITS_START set it: the next level would load as a credits scene */
    /* The warp through the area's death node (death, falling out, exit course) lands, in vanilla, on a death-exit
     * spawn that takes a life (unless exiting the course) and refills health. DynOS enters at the level's entry
     * spawn, which skips it: do its bookkeeping here, or Mario would respawn with health below 0x100 and die again. */
    if (is_node_warp(WARP_NODE_DEATH, level, area, node)) {
        struct MarioState *m = &gMarioStates[0];
        if (arg != WARP_ARG_EXIT_COURSE && m->numLives > 0) { m->numLives--; }   /* lose_life_after_death_exit */
        m->health = 0x880; m->hurtCounter = 0; m->healCounter = 0;
    }
    return 1;
}
/* Game over (0008-Story-mode-and-game-flow.patch): vanilla goes to the entry level with 4 lives and full health; we go
 * home with those values. */
static s32 hook_game_over(void) {
    if (gArMode && gArStory) {
        warp_special(SPECIAL_WARP_GODDARD_GAMEOVER);
        return 1;
    }
    const sm64_loop_warp w = loop_state(0, 0);
    const sm64_loop_go g = sm64_loop_on_game_over(&w);
    if (!dynos_warp_to_level(g.level, 1, g.act)) { return 0; }
    sm64_host_mark_warp(g.level);
    struct MarioState *m = &gMarioStates[0];
    m->numLives = 4;
    m->health = 0x880; m->hurtCounter = 0; m->healCounter = 0;
    return 1;
}

static struct ArGameHooks sHooks = {
    .control_yaw = hook_control_yaw,
    .billboard = hook_billboard,
    .listener_vector = hook_listener_vector,
    .render_hud = sm64_host_render_hud,
    .before_warp = hook_before_warp,
    .game_over = hook_game_over,
};
static void frame_hook(fc_builder *b) {
    sm64_host_update_screen();   /* this frame's screen, not the last tick's */
    sm64_ar_pip_frame(b);        /* first: its group is drawn before the HUD's on-top groups */
    sm64_ar_hud_frame(b);
    sm64_ar_cannon_frame(b);
    sm64_ar_look_frame(b);
}
void sm64_ar_hooks_install(void) {
    gArGameHooks = &sHooks;
    sm64_capture_set_frame_hook(frame_hook);
}

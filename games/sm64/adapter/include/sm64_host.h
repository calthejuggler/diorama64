#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sm64_packs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { SM64_BOOT_OK = 1, SM64_BOOT_NO_ROM = 0, SM64_BOOT_BAD_ROM = -1 } sm64_boot_result;
typedef struct { uint16_t buttons; int8_t stick_x; int8_t stick_y; } sm64_pad;
/* May be called at most once per process: coopdx global state is never torn down. */
sm64_boot_result sm64_host_boot(const char *savepath, const char *rom_path);
const char      *sm64_host_last_error(void);
void             sm64_host_tick(void);
uint32_t         sm64_host_frame_count(void);
int              sm64_host_level(void);
int              sm64_host_act(void);
uint32_t         sm64_host_mario_action(void);
void             sm64_host_mario_pos(float out[3]);
float            sm64_host_mario_hitbox_height(void);
#define SM64_HOST_NEXT_ACT (-1)
#define SM64_HOST_BOB_NEXT_ACT SM64_HOST_NEXT_ACT
int              sm64_host_start_level(int level_num, int act);
int              sm64_host_start_bob(int act);
int              sm64_host_using_null_network(void);
float            sm64_host_ground_height(void);
float            sm64_host_ground_plane(void);
int              sm64_host_level_entry(float pos[3], int *area);
int              sm64_host_entry_ground(float *out_y);
int              sm64_host_area_ground(int rule, float *out_y);
int              sm64_host_area_lowest_floor(float *out_y);
void             sm64_host_set_draw_all_rooms(int on);
int              sm64_host_draw_all_rooms(void);
enum { SM64_OTHER_ROOMS_HIDDEN = 0, SM64_OTHER_ROOMS_FROZEN = 1, SM64_OTHER_ROOMS_LIVE = 2 };
void             sm64_host_set_other_room_objects(int mode);
int              sm64_host_other_room_objects(void);
void             sm64_host_set_hide_outside(int on);
int              sm64_host_hide_outside(void);
void             sm64_host_set_cannon_visible(int on);
int              sm64_host_cannon_visible(void);
void             sm64_host_level_bounds(float min[3], float max[3]);
struct sm64_solid;
int              sm64_host_static_solids(float min_top, struct sm64_solid **out);
/* Gas boxes are left out as find_water_level does; LLL's volcano's lava floor box too (lava, not water). */
int              sm64_host_water_boxes(float *out5, int cap);
struct sm64_warp_sample;
void             sm64_host_warp_sample(struct sm64_warp_sample *out);
void             sm64_host_set_pad(const sm64_pad *pad);   /* applied on the next tick; exclusive (no physical input) */
void             sm64_host_pad_seen(sm64_pad *out);
int              sm64_host_input_injected(void);
void             sm64_host_set_story(int on);
int              sm64_host_story(void);
/* LEVEL only once the level is ready: while one loads, the screen before it stays, so a screen change never shows a
 * "level" blip. */
int              sm64_host_screen(void);
/* Computed at the end of the game's frame so a new screen's overlay groups are right on its first frame. */
void             sm64_host_update_screen(void);
int              sm64_host_menu_scene(void);
int              sm64_host_test_mario_has_model(void);
unsigned         sm64_host_level_inits(void);
int              sm64_host_player_metal(int player_index);
int              sm64_host_title_splash(void);
int              sm64_host_title_gameover(void);
int              sm64_host_gd_world_to_cursor(const float p[3], float out[2]);
int              sm64_host_gd_cursor_to_world(float out[3]);
int              sm64_host_gd_camera_forward(float out[3]);
void             sm64_host_gd_set_cursor(float x, float y);
void             sm64_host_gd_cursor(float out[2]);
unsigned         sm64_host_gd_eye_offset(float out[2]);
unsigned         sm64_host_gd_eye_offsets(float left[2], float right[2]);
unsigned         sm64_host_gd_eye_screens(float left[2], float right[2]);
int              sm64_host_gd_eyes_world(float out[3]);
int              sm64_host_gd_eyes_still_at(float frame);
float            sm64_host_gd_eye_frame(void);
int              sm64_host_gd_eyes_still_status(void);
void             sm64_host_gd_still_mask(const unsigned char *pose, int n, int run, unsigned char *out);
unsigned         sm64_host_gd_eye_still_skips(void);
void             sm64_host_gd_set_eye_follow(int on);
void             sm64_host_set_menu_cursor(float x, float y);
int              sm64_host_act_select(int index);
int              sm64_host_act_stars_visible(void);
int              sm64_host_save_file(void);
void             sm64_host_menu_cursor(float out[2]);
int              sm64_host_demo_running(void);
int              sm64_host_mario_in_intro(void);
int              sm64_host_dialog_id(void);
int              sm64_host_dialog_inverted(void);
int              sm64_host_intro_shot(void);
int              sm64_host_game_camera(float aspect, float view[16], float roll[16], float proj[16], float eye[3]);
void             sm64_host_set_ar_mode(int on);
void             sm64_host_set_resource_dir(const char *dir);
void             sm64_host_set_pack_stamps_required(int on);
/* Before boot. When set, packs load only from there, never from <save>/dynos/packs
 * (0014-Packs-only-from-the-app-bundle.patch). */
void             sm64_host_set_bundled_pack_dir(const char *dir);
int              sm64_host_pack_stamps(int *refused, double *ms);
const char      *sm64_host_pack_refusal(int i);
uint64_t         sm64_host_state_hash(void);
uint64_t         sm64_host_deep_state_hash(void);
void sm64_host_rerender(void);
int  sm64_host_area(void);
void sm64_host_camera_pos(float out[3]);
void sm64_host_camera_focus(float out[3]);
int  sm64_host_collision_floors(float *tris9, int cap);
int  sm64_host_visibility(const float eye_level[3], int *mario_visible, float *floor_fraction);
int  sm64_host_rendered_objects(float *xyz, int *behavior_ids, int cap);
int  sm64_host_draw_distance(void);
int  sm64_host_king_bobomb_pos(float out[3]);
int  sm64_host_dialog_open(void);
int  sm64_host_dialog_state(void);
void sm64_host_mario_gfx_pos(float out[3]);
/* Mario's object is drawn: his graph node is active (the cannon, warps, pipes and act_disappeared clear it).
 * GRAPH_RENDER_INVISIBLE is not read: the hurt blink sets it every other frame. */
int  sm64_host_mario_drawn(void);
const void *sm64_host_mario_sound_source(void);
void sm64_host_test_no_mario_source(int on);
void sm64_host_mario_camera_to_object(float out[3]);
int  sm64_host_process_lods(void);
int  sm64_host_behavior_id(const char *bhv_name);
int  sm64_host_objects_with_behavior(int behavior_id, float *xyz, int cap);
int  sm64_host_far_plane_unbounded(void);
/* Known after sm64_host_boot (-1 before); any thread may read the table once the count is >= 0. */
int         sm64_host_pack_count(void);
const char *sm64_host_pack_name(int index);
int         sm64_host_pack_kind(int index);
int         sm64_host_pack_files(int index, int *tex, int *bins, double *mb);
int         sm64_host_pack_enabled(int index);
void        sm64_host_set_pack_choice(int kind, int on, const char *name);
int         sm64_host_resolve_pack(int kind, int on, const char *name);
double      sm64_host_pack_last_load_ms(void);
int         sm64_host_objects_force_3d(int behavior_id);
/* Published by the game thread after each tick, so any thread may read it. */
int         sm64_host_trees_3d(void);
unsigned    sm64_host_gfx_clamped(void);
unsigned    sm64_host_pack_bin_refusals(void);
int         sm64_host_probe_layer_clamp(void);
unsigned    sm64_host_pack_draws(void);
unsigned    sm64_host_pack_tilts(void);
int         sm64_host_pack_tilt_sample(float m[16], float floor_n[3], int *yaw);
void        sm64_host_test_queue_moving_coin(int tick);
void        sm64_host_test_queue_metal_cap(int tick);
/* With a model pack enabled DynOS may resolve the name to the pack's layout: count with packs off. */
int         sm64_host_actor_instances(const char *geo_name);
void        sm64_host_generate_tex(const char *png_dir, const char *out_dir);
void        sm64_host_generate_actors(const char *dir);

typedef struct { float pos[3]; int32_t room, action, anim_frame, other, flagged, drawn, behavior; } sm64_room_object;
int  sm64_host_test_room_objects(sm64_room_object *out, int cap);
int  sm64_host_mario_room(void);
typedef struct { float pos[3]; int32_t behavior, action, doorway, front, back, tag, drawn, index, stars; } sm64_door_info;
int  sm64_host_test_doors(sm64_door_info *out, int cap);
int  sm64_host_test_face_door(int index, float dist, int side);
int  sm64_host_test_spawn_from_mario(float x, float y, float z);
int  sm64_host_test_object_invisible(int index, int on);
int  sm64_host_room_coverage(int room);
int  sm64_host_rooms_visited(int level, int area, int room);
int  sm64_host_rooms_count(void);
int  sm64_host_rooms_file(void);
int  sm64_host_rooms_path(char *buf, size_t cap);
int  sm64_host_door_room(void);
int  sm64_host_test_walk_forward(float speed);
int  sm64_host_test_spawn_coin(float x, float y, float z);
int  sm64_host_test_move_object(int index, float x, float y, float z);
int  sm64_host_test_object_floor_room(int index);
int  sm64_host_test_room_at(float x, float y, float z);
int  sm64_host_test_roomless_objects(float *xyz, int *index, int cap);
int  sm64_host_test_spawn_roomless(float x, float y, float z);
/* Defined only in a build with the tests (SM64AR_TEST_SEAMS): never in the app. */
void sm64_host_test_rooms_save_fails(int on);
void sm64_host_test_stale_room(int room);
void sm64_host_test_mario_render_active(int on);
int  sm64_host_test_floors_of_type(int type, float *xyz, int *room, int cap);
int  sm64_host_test_room_under_mario(void);
size_t sm64_host_test_save_buffer(void *buf, size_t cap);
void sm64_host_test_save_copy(int src_file, int dst_file);
void sm64_host_test_save_erase(int file);
void sm64_host_test_save_write(void);
void sm64_host_test_heal(int wedges);
void sm64_host_test_hurt(int wedges);
void sm64_host_test_add_coins(int n);
void sm64_host_test_force_hud_flags(int flags);
void sm64_host_test_hide_hud(int on);
void sm64_host_test_start_timer(void);
int  sm64_host_test_enter_cannon(void);
int  sm64_host_mario_cannon_aiming(void);
int  sm64_host_mario_shot_from_cannon(void);
int  sm64_host_star_pos(int star_index, float out[3]);
int  sm64_host_mario_in_cannon(void);
int  sm64_host_cannon_angles(int *pitch, int *yaw);
int  sm64_host_cannon_yaw_limits(int *lo, int *hi);
uint32_t sm64_host_cannon_arc(int pitch, int yaw, float *xyz, uint32_t cap);
int  sm64_host_cannon_rendered(int *base, int *barrel);
int  sm64_host_cannon_barrel_axes(float axes[9]);
int  sm64_host_mario_on_ground(void);
int  sm64_host_mario_holding(void);
int  sm64_host_nearest_object(int behavior_id, const float from[3], float max_dy, float out[3]);
int  sm64_host_nearest_open_cannon(const float from[3], float out[3]);
const char *sm64_host_behavior_name(int id);
int  sm64_host_test_place_mario(float x, float y, float z);
int  sm64_host_test_drop_mario(float x, float y, float z);
int  sm64_host_test_face_object(int behavior_id, float dist);
int  sm64_host_paused(void);
int      sm64_host_test_spawn_star(int star_index);
int      sm64_host_saves(int *course, int *star);
int      sm64_host_menu_mode(void);
int      sm64_host_time_stop(void);
int      sm64_host_save_prompt_open(void);
int      sm64_host_save_unwritten(void);
int      sm64_host_test_spawn_star_noexit(int star_index);
int      sm64_host_test_spawn_bowser_key(void);
void     sm64_host_test_grand_star_warp(void);
int              sm64_host_test_grand_star_ending(void);
int      sm64_host_test_open_sign(int id);
int      sm64_host_test_open_dialog(int id);
void     sm64_host_test_kill_mario(void);
#define SM64_SOUND_AT_LISTENER 0
#define SM64_SOUND_AT_MARIO 1
#define SM64_SOUND_AT_ORIGIN 2
void     sm64_host_test_play_sound(uint32_t sound_bits, int where);
void     sm64_host_test_play_sound_at(uint32_t sound_bits, int slot, const float offset[3]);
void     sm64_host_test_sound_banks(uint32_t enabled_mask);
int  sm64_host_test_play_secondary_music(int seq);
void sm64_host_test_play_star_jingle(void);
uint32_t sm64_host_bob_star_flags(void);
void     sm64_host_test_save_bob_star(int star_index);
int      sm64_host_mario_lives(void);
void     sm64_host_seed_bob_stars(uint32_t mask);
void     sm64_host_set_lives(int n);
int      sm64_host_in_bob(void);
int      sm64_host_mario_coins(void);
int      sm64_host_mario_health(void);
void     sm64_host_test_exit_course(void);
int      sm64_host_pause_line(void);
int      sm64_host_changing_level(void);
int      sm64_host_request_warp(int level_num, int act);
int      sm64_host_warp_pending(void);
#define SM64_HOST_FORCE_TICKS 30
int      sm64_host_request_forced(const char **reason);
#define SM64_HOST_HOLD_NONE 0
#define SM64_HOST_HOLD_DELAYED_WARP 1
#define SM64_HOST_HOLD_PLAY_MODE 2
void     sm64_host_test_hold_unsafe(int kind);
int      sm64_host_request_wait(const char **reason);
int      sm64_host_home_level(void);
int      sm64_host_level_ready(void);
int      sm64_host_course(void);
uint32_t sm64_host_course_star_flags(int course);
uint32_t sm64_host_save_star_flags(int slot);
void     sm64_host_seed_course_stars(int course, uint32_t mask);
void     sm64_host_test_save_course_star(int course, int star_index);
void     sm64_host_test_warp_node(int node);
int      sm64_host_test_enter_painting(int index);
const char *sm64_host_warp_wait_reason(void);
const char *sm64_host_screen_refusal(int screen);
int sm64_host_play_from_start(const char **why);
int sm64_host_dropped(const char **what, const char **why);
void sm64_host_test_publish_screen(int screen);
int  sm64_host_test_skip_intro(void);
int  sm64_host_test_stay_in_level_after_star(void);
void sm64_host_test_set_stay_in_level_after_star(int v);
#ifdef __cplusplus
}
#endif

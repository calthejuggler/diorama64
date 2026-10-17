#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ar_adapter.h"
#include "sm64_levels.h"
#include "sm64_packs.h"
#include "sm64_touch.h"
#ifdef __cplusplus
extern "C" {
#endif
const ar_adapter *ar_adapter_sm64(void);
int ar_adapter_sm64_setup(const char *text, char *err, size_t err_cap);
/* A replay calls it before ar_adapter_sm64_setup: a recording's setup without these items was made at the defaults. */
void ar_adapter_sm64_reset_game_settings(void);
int ar_adapter_sm64_story(void);
void ar_adapter_sm64_set_resource_dir(const char *dir);
void ar_adapter_sm64_set_pack_stamps_required(int on);
void ar_adapter_sm64_set_bundled_pack_dir(const char *dir);
int  ar_adapter_sm64_pack_stamps(int *refused, double *ms);
const char *ar_adapter_sm64_pack_refusal(int i);
int ar_adapter_sm64_level_count(void);
const sm64_level_entry *ar_adapter_sm64_level(int index);
/* Any thread; applied at the next safe tick, the latest request wins. */
int ar_adapter_sm64_request_level(int index, int act);
int ar_adapter_sm64_play_from_start(void);
const char *ar_adapter_sm64_request_refusal(void);
/* Any thread (the visionOS layer thread through VosPlatform's claim). Never claims while locked (recording or
 * replaying). */
enum { SM64_POINTER_BEGIN = 0, SM64_POINTER_MOVE = 1, SM64_POINTER_END = 2, SM64_POINTER_CANCEL = 3 };
enum { SM64_POINTER_NONE = 0, SM64_POINTER_MENU = 1, SM64_POINTER_HEAD = 2 };
typedef struct sm64_pointer { uint64_t id; int phase; float ray_o[3], ray_d[3]; int has_hand; float hand[3]; ar_pose head; int target; float ndc[2]; int no_ray; } sm64_pointer;
int ar_adapter_sm64_pointer(sm64_pointer *p);
void ar_adapter_sm64_set_pointer_lock(int locked);
/* Any thread; table metres ([0] left, [1] right; NULL: none tracked). */
void ar_adapter_sm64_set_hands(const sm64_hand_tips hands[2]);
int  ar_adapter_sm64_wants_hands(void);
typedef void (*ar_adapter_sm64_touch_fn)(const char *line, uint32_t tick);
void ar_adapter_sm64_set_touch_log(ar_adapter_sm64_touch_fn fn);
/* Only where the AR runtime turns it on: by default (the harness, the tests, replays) gd's cursor is never moved by it
 * and its eyes stay vanilla. */
void ar_adapter_sm64_set_title_eyes_active(int on);
void ar_adapter_sm64_set_eyes_log(ar_adapter_sm64_touch_fn fn);
void ar_adapter_sm64_table_placed(void);
void ar_adapter_sm64_set_title_place_on_request(int on);
void ar_adapter_sm64_set_title_log(ar_adapter_sm64_touch_fn fn);
int ar_adapter_sm64_menu_tap(float x, float y);
/* Called on the game thread after the first tick and after every tick whose screen or story state changed. */
typedef void (*ar_adapter_sm64_screen_fn)(int screen, int story, uint32_t tick);
void ar_adapter_sm64_set_screen_log(ar_adapter_sm64_screen_fn fn);
typedef void (*ar_adapter_sm64_dropped_fn)(const char *what, const char *why, uint32_t tick);
void ar_adapter_sm64_set_dropped_log(ar_adapter_sm64_dropped_fn fn);
int ar_adapter_sm64_level_state(sm64_level_state *out);
/* Rendering only by itself, but objects in other rooms run only with it on, so with Live it changes the game (recorded
 * as the setup item rooms=own). */
void ar_adapter_sm64_set_draw_all_rooms(int on);
int ar_adapter_sm64_draw_all_rooms(void);
void ar_adapter_sm64_set_other_room_objects(int mode);
int ar_adapter_sm64_other_room_objects(void);
const char *ar_adapter_sm64_other_rooms_name(int mode);
int ar_adapter_sm64_other_rooms_parse(const char *name);
void ar_adapter_sm64_set_hide_outside(int on);
int ar_adapter_sm64_hide_outside(void);
void ar_adapter_sm64_set_mario_cm(float cm);
float ar_adapter_sm64_mario_cm(void);
int ar_adapter_sm64_test_place_mario(float x, float y, float z);
void ar_adapter_sm64_set_tree_style(int style);
int ar_adapter_sm64_tree_style(void);
const char *ar_adapter_sm64_tree_style_name(int style);
int ar_adapter_sm64_tree_style_parse(const char *name);
void ar_adapter_sm64_set_billboard_style(int style);
int ar_adapter_sm64_billboard_style(void);
const char *ar_adapter_sm64_billboard_style_name(int style);
int ar_adapter_sm64_billboard_style_parse(const char *name);
int  ar_adapter_sm64_setting_count(void);
const char *ar_adapter_sm64_setting_key(int i);
const char *ar_adapter_sm64_setting_default(const char *key);
int  ar_adapter_sm64_set_setting(const char *key, const char *value, char *err, size_t cap);
int  ar_adapter_sm64_get_setting(const char *key, char *buf, size_t cap);
int  ar_adapter_sm64_audio_state(void);
int  ar_adapter_sm64_settings_revision(void);
int  ar_adapter_sm64_settings_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override);
struct fc_look;
int ar_adapter_sm64_look(struct fc_look *out);
/* It changes the game (the cannon's angles are object state), so a recording carries it as the setup item cannon=0. */
void ar_adapter_sm64_set_cannon_visible(int on);
int ar_adapter_sm64_cannon_visible(void);
/* Rendering only: packs never change the game's state (models are staged without animations). */
int    ar_adapter_sm64_pack_count(void);
int    ar_adapter_sm64_pack_info(int index, char *name, int cap, int *kind, int *tex, int *bins, double *mb);
int    ar_adapter_sm64_set_packs(int kind, int on, const char *name);
int    ar_adapter_sm64_pack_enabled(int index);
double ar_adapter_sm64_pack_load_ms(void);
void ar_adapter_sm64_texfifo_stats(uint64_t *pushed_bytes, uint64_t *peak_pending_bytes);
int    ar_adapter_sm64_trees_3d(void);
int    ar_adapter_sm64_tree_cards(void);
int    ar_adapter_sm64_objects_force_3d(void);
unsigned ar_adapter_sm64_gfx_clamped(void);
uint64_t ar_adapter_sm64_deep_state_hash(void);
float ar_adapter_sm64_pip_alpha(void);
int ar_adapter_sm64_intro_shot(void);
void ar_adapter_sm64_set_pip_sky(int on);
int ar_adapter_sm64_game_camera(ar_pose *table_from_eye, float *tan_half_fovy, float *near_m);
int ar_adapter_sm64_probe_layer_clamp(void);
unsigned ar_adapter_sm64_pack_draws(void);
unsigned ar_adapter_sm64_pack_tilts(void);
int ar_adapter_sm64_pack_tilt_sample(float m[16], float floor_n[3], int *yaw);
void ar_adapter_sm64_test_queue_moving_coin(int tick);
void ar_adapter_sm64_test_queue_metal_cap(int tick);
int ar_adapter_sm64_actor_instances(const char *geo_name);
/* dir_table: towards the light, table axes (= level axes); a zero vector counts as off. */
void ar_adapter_sm64_set_key_light(int on, const float dir_table[3]);
void ar_adapter_sm64_test_clear_light_view(void);
void ar_adapter_sm64_set_normal_flags(uint8_t mask);
void ar_adapter_sm64_set_player_focus(float strength);
void ar_adapter_sm64_set_object_focus(float strength);
void ar_adapter_sm64_set_music_ring(int on, float size, float rotate_dps);
void ar_adapter_sm64_set_ring_listener(int at_head, float focus);
/* Game thread (or the harness's offscreen loop, which steps the game itself). */
int ar_adapter_sm64_visibility(const float eye_table[3], int *mario_visible, float *floor_fraction);
#ifdef __cplusplus
}
#endif

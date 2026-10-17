#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Main thread, once, before any scene. */
int  arvos_configure(int argc, const char *const *argv, const char *bundle_dir, const char *documents_dir);
int  arvos_autostart(void);
int  arvos_lifecycle_test(double *close_after_s, double *reopen_after_s);
int  arvos_hide_status(void);
int  arvos_status(char *buf, int cap);
void arvos_log(const char *line);
/* Render thread spawned by the CompositorLayer closure; returns when the layer is invalidated. */
void arvos_layer_run(void *layer_renderer);
void arvos_space_closed(void);
/* Main thread, after the space was dismissed: waits up to wait_s for the frame loop to end so its close-time saves are
 * done. */
int  arvos_prepare_quit(double wait_s);
/* Main thread, last: _exit(0) after flushing the run log; no static destructors while the audio engine, Metal and the
 * game may still run. */
void arvos_quit_now(void) __attribute__((noreturn));
int  arvos_replace_test(double *after_s);
void arvos_replace_table(void);
void arvos_set_background(int background);
/* kind 0 indirect pinch, 1 direct pinch, 2 pointer, 3 touch, 4 other; phase 0 active, 1 ended, 2 cancelled; timestamp
 * in seconds; the selection ray at pinch start and the pinching hand's pose (xyzw) when present, in the immersive
 * space's metres, assumed to be ARKit's tracking space; chirality 0 none, 1 left, 2 right. */
typedef struct arvos_spatial_event {
    uint64_t id; int32_t kind; int32_t phase; double timestamp;
    int32_t has_ray; float ray_origin[3]; float ray_direction[3];
    int32_t has_pose; float pose_position[3]; float pose_orientation[4];
    int32_t chirality;
} arvos_spatial_event;
void arvos_push_spatial_event(const arvos_spatial_event *e);   /* main thread (onSpatialEvent) */
int  arvos_game(char *buf, int cap);
int  arvos_game_setting_option(const char *key, char *buf, int cap);
int  arvos_sm64_audio_state(void);
int  arvos_sm64_setting_count(void);
int  arvos_sm64_setting_key(int i, char *buf, int cap);
int  arvos_sm64_setting_default(const char *key, char *buf, int cap);
int  arvos_sm64_setting_set(const char *key, const char *value);
int  arvos_sm64_settings_revision(void);
int  arvos_sm64_setting_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override);
int  arvos_level_count(void);
int  arvos_level_name(int index, char *buf, int cap);
int  arvos_level_has_acts(int index);
int  arvos_level_section(int index);
int  arvos_select_level(int index, int act);
int  arvos_level_test(double *after_s, int *index, int *act);
typedef struct arvos_level_view {
    int current, home, pending, act; char current_name[64]; int story;
    int screen;
} arvos_level_view;
int  arvos_level_view_get(arvos_level_view *out);
int  arvos_level_label(int index, char *buf, int cap);
int  arvos_has_draw_all_rooms(void);
int  arvos_draw_all_rooms_option(void);
int  arvos_draw_all_rooms(void);
void arvos_set_draw_all_rooms(int on);
int  arvos_hide_outside_option(void);
void arvos_set_hide_outside(int on);
double arvos_mario_focus_far_m(double cm);
double arvos_mario_cm_option(void);
double arvos_mario_cm(void);
int  arvos_mario_cm_lock(char *buf, int cap);
int  arvos_start_option(void);
int  arvos_set_start_in(int story);
int  arvos_play_from_start(void);
int  arvos_start_effective(char *source, int cap);
void arvos_set_mario_cm(double cm, int final_value);
int  arvos_scale_readout(double cm, char *buf, int cap);
int  arvos_other_rooms_option(void);
int  arvos_other_rooms(void);
void arvos_set_other_rooms(int mode);
int  arvos_tree_style_option(void);
void arvos_set_tree_style(int style);
int  arvos_billboards_option(void);
void arvos_set_billboards(int style);
int  arvos_cannon_visible_option(void);
int  arvos_cannon_visible(void);
void arvos_set_cannon_visible(int on);
int  arvos_pack_count(void);
int  arvos_pack_info(int index, char *name, int cap);
int  arvos_pack_option(int kind, char *buf, int cap);
void arvos_set_packs(int kind, int on, const char *name);
int  arvos_trees_3d(void);
int  arvos_look_key_count(void);
int  arvos_look_key(int index, char *buf, int cap);
int  arvos_look_default(const char *key, char *buf, int cap);
int  arvos_look_option(const char *key, char *buf, int cap);
int  arvos_set_look(const char *key, const char *value, int final_value);
int  arvos_look_defaults_rev(void);
int  arvos_look_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override);
int  arvos_foveation(void);
void arvos_render_probe_begin(void);
void arvos_render_probe_end(void);
int  arvos_render_quality(double *quality);

int  arvos_audio_key_count(void);
int  arvos_audio_key(int index, char *buf, int cap);
int  arvos_audio_default(const char *key, char *buf, int cap);
int  arvos_audio_option(const char *key, char *buf, int cap);
int  arvos_set_audio(const char *key, const char *value, int final_value);
int  arvos_audio_preset_count(void);
int  arvos_audio_preset_name(int index, char *buf, int cap);
int  arvos_audio_preset_value(const char *preset, const char *key, char *buf, int cap);
int  arvos_apply_audio_preset(const char *preset);
typedef struct arvos_room_light { int room; float abs_lum, neutral, gain; float dir[3]; float tint[3]; unsigned long long probes; } arvos_room_light;
int  arvos_room_light_get(arvos_room_light *out);
#ifdef __cplusplus
}
#endif

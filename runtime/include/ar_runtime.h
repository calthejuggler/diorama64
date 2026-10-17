/* runtime/include/ar_runtime.h — the runtime between a platform and a game adapter.
 *
 * Threads:
 *   create, destroy, init_game, set_texture_sink, start/stop_game_thread: one setup thread, before or after frames.
 *   begin_frame, submit_frame: the render thread only (they call the platform's render-thread entry points).
 *   end_frame: any thread, once per successful begin_frame (usually the GPU completion handler).
 *   step_game, ticks_due: the game thread, or the render thread when it steps the game itself; never both.
 *   ticks_done: any thread. pull_audio: one audio thread (single consumer). */
#pragma once
#include "ar_adapter.h"
#include "ar_math.h"
#include "ar_platform.h"
#include "ar_sarec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AR_NEAR_M 0.05f
typedef struct ar_runtime ar_runtime;
typedef struct ar_view_out {
    ar_mat4 clip_from_level;   /* reverse-Z proj * eye_from_tracking * tracking_from_table * table_from_level */
    ar_mat4 clip_from_table;
    float eye_level[3];
    uint32_t x, width, height;
    ar_pose tracking_from_eye;
    ar_fov fov;
    float near_m;
    ar_mat4 clip_from_tracking;
} ar_view_out;
typedef struct ar_render_frame {
    int64_t display_time_ns;
    const fc_frame *snapshot;  /* NULL until the game published one; released by ar_runtime_end_frame */
    float interp_alpha;
    uint32_t view_count;
    ar_view_out views[AR_MAX_VIEWS];
    float head_level[3];
    ar_pose head_table;
    ar_pose tracking_from_table;
    ar_level_info level;
    uint8_t table_valid;
    uint8_t head_tracked;
    uint8_t has_environment;
    uint8_t _reserved;
    float table_half_extent[2];
    float display_hz;
    ar_environment environment;
    ar_preview preview;
} ar_render_frame;
ar_runtime *ar_runtime_create(const ar_adapter *adapter, const ar_platform *platform);
void ar_runtime_destroy(ar_runtime *rt);
int  ar_runtime_init_game(ar_runtime *rt, const char *rom_path, const char *save_dir);
void ar_runtime_set_texture_sink(ar_runtime *rt, ar_texture_upload_fn fn, void *user);
void ar_runtime_step_game(ar_runtime *rt);
uint32_t ar_runtime_ticks_due(ar_runtime *rt);
uint64_t ar_runtime_ticks_done(ar_runtime *rt);
int  ar_runtime_start_game_thread(ar_runtime *rt);
void ar_runtime_stop_game_thread(ar_runtime *rt);
/* 0 = the platform stopped. The game clock runs only while the session is FOCUSED and the table has been located at
 * least once. */
int  ar_runtime_begin_frame(ar_runtime *rt, ar_render_frame *out);
float ar_interp_alpha(int64_t display_ns, int64_t arrival_ns);
/* After the frame's GPU work is encoded, before present: hands the views used back to the platform (end_frame). */
/* Call exactly once per successful ar_runtime_begin_frame. */
void ar_runtime_submit_frame(ar_runtime *rt, const ar_render_frame *frame);
void ar_runtime_end_frame(ar_runtime *rt, const ar_render_frame *frame);
uint32_t ar_runtime_pull_audio(ar_runtime *rt, int16_t *stereo, uint32_t frames);
/* Setup thread, before ar_runtime_init_game. */
int ar_runtime_set_audio_mode(ar_runtime *rt, uint32_t mode);
uint32_t ar_runtime_audio_mode(ar_runtime *rt);
/* One consumer thread (the same one as ar_runtime_pull_audio; never both). */
uint32_t ar_runtime_pull_audio_scene(ar_runtime *rt, ar_audio_pull *p);
/* The runtime keeps the pointer: rec must outlive the replay. Setup thread, before the game steps. */
typedef struct ar_replay_status {
    uint64_t played;
    uint64_t checked;
    int diverged;
    uint64_t first_bad_tick;
    uint64_t expected, got;
    int done;
} ar_replay_status;
void ar_runtime_set_replay(ar_runtime *rt, const sarec *rec);
ar_replay_status ar_runtime_replay_status(ar_runtime *rt);   /* any thread */
/* Setup thread. */
void ar_runtime_set_recorder(ar_runtime *rt, sarec *out);
#ifdef __cplusplus
}
#endif

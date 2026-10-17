#pragma once
#include <stdint.h>
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AR_PLATFORM_ABI_VERSION 4u
#define AR_MAX_VIEWS 4
/* Each backend builds its own projection: the runtime uses infinite-far reverse-Z with near_m (0 = AR_NEAR_M). */
typedef struct ar_view { ar_pose tracking_from_eye; ar_fov fov; uint32_t width, height; float near_m; } ar_view;
typedef struct ar_frame_info {
    int64_t display_time_ns;
    uint8_t should_render;
    uint8_t head_tracked;             /* 0 while tracking is lost (tracking_from_head is then the last good pose) */
    uint8_t _reserved[2];
    float display_hz;
    uint32_t view_count;
    ar_view views[AR_MAX_VIEWS];
    ar_pose tracking_from_head;
} ar_frame_info;
/* The table: the only world root the runtime sees (it may jump). half_extent: x and z half sizes in metres of the
 * physical surface (0 = unknown). */
typedef struct ar_table { ar_pose tracking_from_table; float half_extent[2]; } ar_table;
typedef struct ar_submit_info {
    int64_t display_time_ns;
    uint32_t view_count;
    ar_view views[AR_MAX_VIEWS];      /* exactly the poses, fov and near planes the matrices were built from */
    ar_pose tracking_from_table;
    uint8_t table_valid;
    uint8_t _reserved[3];
} ar_submit_info;
typedef enum { AR_SESSION_IDLE = 0, AR_SESSION_READY, AR_SESSION_VISIBLE, AR_SESSION_FOCUSED,
               AR_SESSION_STOPPING, AR_SESSION_LOST } ar_session_state;
typedef struct ar_anchor_id { uint8_t uuid[16]; } ar_anchor_id;
typedef void (*ar_anchor_done_fn)(void *ctx, int ok);
typedef struct ar_environment {
    void *cube_texture;            /* id<MTLTexture>: MTLTextureTypeCube, linear HDR, Display P3 primaries; unretained, valid
                                      until the next get_environment call; never NULL when get_environment returned 1 */
    float camera_scale_reference;
    uint32_t generation;           /* +1 whenever cube_texture changes. Once a probe was returned, the platform keeps
                                      returning the last one (same generation) while it has no newer one */
    ar_pose tracking_from_probe;
    int64_t timestamp_ns;
} ar_environment;
#define AR_PREVIEW_MAX_OUTLINES 8
enum { AR_PREVIEW_NONE = 0, AR_PREVIEW_SEEKING = 1, AR_PREVIEW_DRAGGING = 2 };
typedef struct ar_preview {
    uint8_t state;
    uint8_t on_plane;
    uint8_t outlines_only;
    uint8_t _reserved;
    ar_pose tracking_from_preview;
    uint32_t outline_count;
    float outlines[AR_PREVIEW_MAX_OUTLINES][4][3];
} ar_preview;
typedef struct ar_platform {
    uint32_t abi_version;
    void *user;
    int  (*wait_frame)(void *user, ar_frame_info *out);              /* render thread only; 0 = stop */
    int  (*get_table)(void *user, ar_table *out);                    /* render thread; 1 = located this frame */
    int  (*get_pad)(void *user, uint32_t player, ar_pad *out);       /* game thread; 1 = connected */
    ar_session_state (*session_state)(void *user);                  /* render thread */
    /* Optional (NULL allowed). */
    void (*begin_frame)(void *user);                                 /* render thread, after wait_frame (xrBeginFrame) */
    void (*end_frame)(void *user, const ar_submit_info *used);       /* render thread, after encoding (xrEndFrame); not ar_runtime_end_frame (GPU-complete, any thread) */
    void (*set_passthrough)(void *user, int on);
    void (*persist_anchor)(void *user, ar_anchor_id id, ar_anchor_done_fn done, void *ctx);
    void (*share_anchor)(void *user, ar_anchor_id id, ar_anchor_done_fn done, void *ctx);
    /* Optional (NULL allowed): render thread, after wait_frame. 1 = a probe is available this frame. */
    int  (*get_environment)(void *user, ar_environment *out);
    /* Optional (NULL allowed): render thread, after wait_frame. */
    int  (*get_preview)(void *user, ar_preview *out);
} ar_platform;
#ifdef __cplusplus
}
#endif

#pragma once
#include <stdint.h>
#include "ar_snapshot_ring.h"
#include "ar_texture_fifo.h"
#include "fc_builder.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t frames, dropped, uploads; } sm64_capture_stats;
/* Call once, before sm64_host_boot. */
void sm64_capture_install(ar_snapshot_ring *ring, ar_texture_fifo *fifo);
sm64_capture_stats sm64_capture_get_stats(void);
typedef struct { uint32_t shown, fading, hidden, terrain; } sm64_room_tris;
sm64_room_tris sm64_capture_room_tris(int room);
const void *sm64_capture_rendering_api(void);
void sm64_capture_set_head_rotation(const float quat[4]);
void sm64_capture_set_light_dir(const float dir_level[3]);
void sm64_capture_clear_light_dir(void);
void sm64_capture_set_goddard_self_lit(int on);
void sm64_capture_set_normal_flags(uint8_t mask);
void sm64_capture_set_frame_hook(void (*fn)(fc_builder *b));
/* Game thread: between begin_tick and end_tick the frame is held back; end_tick publishes it with the level info the
 * tick ends with (frame-locked). */
void sm64_capture_begin_tick(void);
void sm64_capture_end_tick(const ar_level_info *level);
void sm64_capture_set_world_offset(const float d[3]);
void sm64_capture_set_floating(int on);
int sm64_capture_world_bounds(float mn[3], float mx[3]);
#ifdef __cplusplus
}
#endif

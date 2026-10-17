/* runtime/include/ar_scene_ring.h — bed + lanes + per-tick metadata between the game thread (writer) and one consumer.
 * Frames carry an absolute index from the first frame ever written; metadata is keyed by it. */
#pragma once
#include <stdint.h>
#include "ar_audio_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ar_scene_ring ar_scene_ring;
ar_scene_ring *ar_scene_ring_create(int with_lanes, uint32_t capacity_frames, uint32_t meta_capacity);
void ar_scene_ring_destroy(ar_scene_ring *r);
/* Game thread. May wait briefly for a read's copy but never blocks the consumer; when full the oldest whole frames are
 * dropped with their metadata. */
uint64_t ar_scene_ring_write(ar_scene_ring *r, const int16_t *bed, const int16_t *lanes, uint32_t frames,
                             const ar_audio_tick_meta *meta);
/* One consumer thread. Never blocks: while a write holds the ring it returns 0 at once. It stops before the first tick
 * whose metadata would not fit, so metadata is never lost. */
uint32_t ar_scene_ring_read(ar_scene_ring *r, ar_audio_pull *p);
uint64_t ar_scene_ring_contended(ar_scene_ring *r);
uint64_t ar_scene_ring_dropped(ar_scene_ring *r);
uint64_t ar_scene_ring_meta_dropped(ar_scene_ring *r);
/* Takes the lock: a real-time consumer must not call it (use the frame count read returns instead). */
uint32_t ar_scene_ring_available(ar_scene_ring *r);
#ifdef __cplusplus
}
#endif

#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ar_audio_ring ar_audio_ring;
ar_audio_ring *ar_audio_ring_create(uint32_t capacity_frames);
void ar_audio_ring_destroy(ar_audio_ring *r);
/* Never blocks: when full, the oldest frames are dropped. Returns frames written (= frames). */
uint32_t ar_audio_ring_write(ar_audio_ring *r, const int16_t *stereo, uint32_t frames);
/* Returns frames read (<= frames, <= available). Any thread. Never blocks (it may run on a real-time audio thread):
 * while a write holds the ring it returns 0 at once and counts the contention. */
uint32_t ar_audio_ring_read(ar_audio_ring *r, int16_t *stereo, uint32_t frames);
uint64_t ar_audio_ring_contended(ar_audio_ring *r);
uint32_t ar_audio_ring_available(ar_audio_ring *r);
#ifdef __cplusplus
}
#endif

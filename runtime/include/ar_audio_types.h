#pragma once
#include <stdint.h>
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AR_AUDIO_RATE_HZ 32000u
#define AR_AUDIO_MAX_SOURCES 32u
enum { AR_AUDIO_MODE_STEREO = 0u, AR_AUDIO_MODE_SOURCES = 1u };
enum { AR_SRC_ACTIVE = 1u, AR_SRC_NEW = 2u, AR_SRC_NO_DISTANCE_LOSS = 4u,
       AR_SRC_FOCUS = 8u,
       AR_SRC_MUSIC = 16u };
typedef struct ar_audio_source {
    uint32_t id;       /* nonzero while active; a new id means another emitter took the lane (snap, never glide) */
    uint32_t flags;
    float pos[3];
    float gain;
} ar_audio_source;
typedef struct ar_audio_tick_meta {
    uint64_t first_frame;
    uint32_t frames;
    uint32_t active;
    uint32_t dropped;
    uint32_t _reserved;
    ar_pose listener;
    float bed_anchor[3];
    float bed_half_extent[3];
    ar_audio_source lane[AR_AUDIO_MAX_SOURCES];
} ar_audio_tick_meta;
typedef struct ar_audio_pull {
    int16_t *bed;
    int16_t *lanes;              /* NULL or AR_AUDIO_MAX_SOURCES * frames, frame-major (lane fastest); zeros in stereo mode */
    uint32_t frames;
    uint64_t first_frame;
    ar_audio_tick_meta *meta;    /* NULL (metadata in the range is discarded) or meta_cap entries */
    uint32_t meta_cap;
    uint32_t meta_count;         /* out: the ticks whose first frame lies in the frames returned, in order */
} ar_audio_pull;
#ifdef __cplusplus
}
#endif

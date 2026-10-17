#pragma once
#include <stdint.h>
#include "ar_audio_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_SFX_LANES 16u
#define SM64_MUSIC_LANE0 SM64_SFX_LANES
#define SM64_MUSIC_CHANNELS 16u
#ifdef __cplusplus
static_assert(SM64_MUSIC_LANE0 + SM64_MUSIC_CHANNELS <= AR_AUDIO_MAX_SOURCES, "music lanes fit");
#else
_Static_assert(SM64_MUSIC_LANE0 + SM64_MUSIC_CHANNELS <= AR_AUDIO_MAX_SOURCES, "music lanes fit");
#endif
#define SM64_SFX_MAX_VOICES 72
typedef struct sm64_sfx_voice {
    uint8_t channel;
    uint8_t no_distance_loss;
    uint64_t identity;
    double energy;
} sm64_sfx_voice;
typedef struct sm64_sfx_lane { int16_t channel; uint64_t identity; uint32_t id; uint32_t flags; } sm64_sfx_lane;
typedef struct sm64_sfx_lanes {
    sm64_sfx_lane lane[SM64_SFX_LANES];
    uint32_t next_id;
    uint64_t stolen;
    uint64_t dropped;
    uint32_t limit;
} sm64_sfx_lanes;
void sm64_sfx_lanes_reset(sm64_sfx_lanes *s);
uint32_t sm64_sfx_lanes_assign(sm64_sfx_lanes *s, const sm64_sfx_voice *voices, uint32_t n, int *lane_of);
#ifdef __cplusplus
}
#endif

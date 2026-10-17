#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ar_audio_types.h"
#ifdef __cplusplus
extern "C" {
#endif
size_t ar_scene_track_line(const ar_audio_tick_meta *m, uint64_t tick, uint64_t origin_frame, char *out, size_t cap);
#ifdef __cplusplus
}
#endif

#pragma once
#include <stdint.h>
#include "sm64_orchestra.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_EVENT_SEQ_PIRANHA 0x0B
#define SM64_EVENT_SEQ_MERRY_GO_ROUND 0x13
#define SM64_EVENT_SEQ_ENDLESS_STAIRS 0x18
int sm64_event_music_is_song(int seq_id);
typedef struct sm64_event_music {
    sm64_orchestra orch;
    int8_t lane_of[16];
    uint32_t layout;
} sm64_event_music;
void sm64_event_music_reset(sm64_event_music *e);
int sm64_event_music_update(sm64_event_music *e, int env_seq, uint16_t env_in_use, uint16_t level_mask);
int sm64_event_music_lane(const sm64_event_music *e, int channel);
uint32_t sm64_event_music_lane_id(const sm64_event_music *e, int channel);
int sm64_event_music_seat(const sm64_event_music *e, int channel, int *count);
/* 180 / lcm(level_seats, count): no event seat lands on a level seat. */
float sm64_event_music_seat_offset(int level_seats, int count);
#ifdef __cplusplus
}
#endif

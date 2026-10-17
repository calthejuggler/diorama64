#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_RING_MARGIN_M 0.05f
#define SM64_RING_HEIGHT_M 0.10f
typedef struct sm64_orchestra {
    int seq_id;
    uint16_t mask;
    uint32_t layout;
} sm64_orchestra;
void sm64_orchestra_reset(sm64_orchestra *o);
int  sm64_orchestra_update(sm64_orchestra *o, int seq_id, uint16_t in_use);
void sm64_orchestra_new_layout(sm64_orchestra *o);
int  sm64_orchestra_seat(const sm64_orchestra *o, int channel, int *count);
uint32_t sm64_orchestra_lane_id(const sm64_orchestra *o, int channel);
float sm64_ring_radius(const float box_min_m[3], const float box_max_m[3], float size);
float sm64_ring_angle_step(float angle_deg, float rotate_dps);
void sm64_ring_point(int seat, int count, float radius, float angle_deg, float cx, float cz, float out[3]);
float sm64_ring_height_step(float y, int at_head, const float head[3]);
float sm64_ring_nearest_seat(int count, float radius, float angle_deg, float cx, float cz, float y, const float head[3]);
#ifdef __cplusplus
}
#endif

#include "sm64_event_music.h"
#include <string.h>
int sm64_event_music_is_song(int s) {
    return s == SM64_EVENT_SEQ_PIRANHA || s == SM64_EVENT_SEQ_MERRY_GO_ROUND || s == SM64_EVENT_SEQ_ENDLESS_STAIRS;
}
void sm64_event_music_reset(sm64_event_music *e) {
    memset(e, 0, sizeof *e);
    sm64_orchestra_reset(&e->orch);
    for (int c = 0; c < 16; c++) { e->lane_of[c] = -1; }
}
int sm64_event_music_update(sm64_event_music *e, int env_seq, uint16_t env_in_use, uint16_t level_mask) {
    const int seq = sm64_event_music_is_song(env_seq) ? env_seq : -1;
    int changed = 0;
    if (seq != e->orch.seq_id) {
        for (int c = 0; c < 16; c++) { e->lane_of[c] = -1; }
    }
    if (sm64_orchestra_update(&e->orch, seq, seq < 0 ? 0 : env_in_use)) { changed = 1; }
    uint16_t taken = 0;
    for (int c = 0; c < 16; c++) {
        if (e->lane_of[c] < 0) { continue; }
        const int k = e->lane_of[c] - 16;
        if (level_mask & (1u << k)) { e->lane_of[c] = -1; changed = 1; }
        else { taken |= (uint16_t)(1u << k); }
    }
    if (seq >= 0) {
        for (int c = 0; c < 16; c++) {
            if (!(e->orch.mask & (1u << c)) || e->lane_of[c] >= 0) { continue; }
            for (int k = 15; k >= 0; k--) {
                if ((level_mask | taken) & (1u << k)) { continue; }
                e->lane_of[c] = (int8_t)(16 + k);
                taken |= (uint16_t)(1u << k);
                changed = 1;
                break;
            }
        }
    }
    if (changed) { e->layout++; }
    return changed;
}
int sm64_event_music_lane(const sm64_event_music *e, int c) { return c >= 0 && c < 16 ? e->lane_of[c] : -1; }
uint32_t sm64_event_music_lane_id(const sm64_event_music *e, int c) {
    return 0x40000000u | ((e->layout & 0x3FFFFFFu) << 4) | (uint32_t)(c & 15);
}
int sm64_event_music_seat(const sm64_event_music *e, int c, int *count) { return sm64_orchestra_seat(&e->orch, c, count); }
float sm64_event_music_seat_offset(int level_seats, int count) {
    const int e = count > 0 ? count : 1, s = level_seats > 0 ? level_seats : e;
    int a = s, b = e;
    while (b) { const int t = a % b; a = b; b = t; }
    return 180.f / (float)(s / a * e);
}

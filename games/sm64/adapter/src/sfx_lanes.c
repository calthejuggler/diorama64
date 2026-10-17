#include "sm64_sfx_lanes.h"
#include <string.h>
void sm64_sfx_lanes_reset(sm64_sfx_lanes *s) {
    memset(s, 0, sizeof *s);
    for (int l = 0; l < (int)SM64_SFX_LANES; l++) { s->lane[l].channel = -1; }
    s->next_id = 1;
    s->limit = SM64_SFX_LANES;
}
static uint32_t new_id(sm64_sfx_lanes *s) {
    const uint32_t id = s->next_id++;
    if (s->next_id == 0) { s->next_id = 1; }
    return id;
}
static int louder(const sm64_sfx_voice *a, const sm64_sfx_voice *b) {
    return a->energy > b->energy || (a->energy == b->energy && a->channel < b->channel);
}
uint32_t sm64_sfx_lanes_assign(sm64_sfx_lanes *s, const sm64_sfx_voice *v, uint32_t n, int *lane_of) {
    const uint32_t cap = s->limit >= 1 && s->limit < SM64_SFX_LANES ? s->limit : SM64_SFX_LANES;
    uint32_t order[SM64_SFX_MAX_VOICES], m = 0, rejected = 0;
    for (uint32_t i = 0; i < n; i++) {
        lane_of[i] = -1;
        if (v[i].channel >= SM64_SFX_MAX_VOICES || m == SM64_SFX_MAX_VOICES) { rejected++; continue; }
        uint32_t j = m++;
        while (j > 0 && louder(&v[i], &v[order[j - 1]])) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    int winner_of_channel[SM64_SFX_MAX_VOICES];
    uint8_t sounded[SM64_SFX_MAX_VOICES];
    for (uint32_t c = 0; c < SM64_SFX_MAX_VOICES; c++) { winner_of_channel[c] = -1; sounded[c] = 0; }
    for (uint32_t k = 0; k < m; k++) {
        const uint8_t c = v[order[k]].channel;
        sounded[c] = 1;
        if (k < cap) { winner_of_channel[c] = (int)order[k]; }
    }
    for (int l = 0; l < (int)SM64_SFX_LANES; l++) {
        sm64_sfx_lane *ln = &s->lane[l];
        ln->flags = 0;
        if (ln->channel < 0) { continue; }
        if ((uint32_t)l >= cap) { ln->channel = -1; ln->id = 0; continue; }
        const int i = winner_of_channel[ln->channel];
        if (i >= 0) {
            lane_of[i] = l;
            ln->flags = AR_SRC_ACTIVE;
            if (ln->identity != v[i].identity) { ln->identity = v[i].identity; ln->id = new_id(s); ln->flags |= AR_SRC_NEW; }
        } else {
            if (sounded[ln->channel]) { s->stolen++; }
            ln->channel = -1;
            ln->id = 0;
        }
    }
    int free_lane = 0;
    for (uint32_t k = 0; k < m && k < cap; k++) {
        const uint32_t i = order[k];
        if (lane_of[i] >= 0) { continue; }
        while (free_lane < (int)cap && s->lane[free_lane].channel >= 0) { free_lane++; }
        if (free_lane == (int)cap) { break; }
        sm64_sfx_lane *ln = &s->lane[free_lane];
        ln->channel = v[i].channel;
        ln->identity = v[i].identity;
        ln->id = new_id(s);
        ln->flags = AR_SRC_ACTIVE | AR_SRC_NEW;
        lane_of[i] = free_lane;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (lane_of[i] >= 0 && v[i].no_distance_loss) { s->lane[lane_of[i]].flags |= AR_SRC_NO_DISTANCE_LOSS; }
    }
    const uint32_t dropped = rejected + (m > cap ? m - cap : 0);
    s->dropped += dropped;
    return dropped;
}

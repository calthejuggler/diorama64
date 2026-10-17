#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "pc/audio/audio_api.h"
#include "audio/ar_audio_hooks.h"
#include "audio/internal.h"
#include "audio/load.h"
#include "audio/external.h"
#include <stdlib.h>
#include <string.h>
#include "ar_scene_ring.h"
#include "sm64_audio.h"
#include "ar_view.h"
#include "sm64_ar_view.h"
#include "sm64_sfx_lanes.h"
#include "sm64_relevance.h"
#include "sm64_orchestra.h"
#include "sm64_event_music.h"
#include "sm64_host.h"
#include "ar_audio_tuning.h"

extern struct AudioAPI *gAudioApi;
extern float gMasterVolume;           /* src/pc/pc_main.c: stays 0 (= muted) when no config file was loaded */
extern unsigned char gLuaVolumeLevel;
extern bool configMuteFocusLoss;

#define RATE 32000u
static ar_scene_ring *sRing;
static uint64_t sGenerated, sTicks;
static float sGain = 1.f, sScale = 1.f;

static uint32_t sMode = AR_AUDIO_MODE_STEREO;
static _Atomic int sAudioState = -1;
static sm64_audio_options sOpt = { 1, 1, 0, 1 };
static int sOptSet;
static float sOutGain = 1.f, sOutTarget = 1.f;
static sm64_audio_frame sFrame = { 8000.f, 0.f, { { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f, 1.f } }, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f } };
static void fill_meta(ar_audio_tick_meta *m) {
    memset(m, 0, sizeof *m);
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) { m->lane[l].gain = 1.f; }
    m->listener = sFrame.head_in_table;
    for (int k = 0; k < 3; k++) {
        m->bed_anchor[k] = 0.5f * (sFrame.box_min_m[k] + sFrame.box_max_m[k]);
        m->bed_half_extent[k] = 0.5f * fabsf(sFrame.box_max_m[k] - sFrame.box_min_m[k]);
    }
}
void sm64_audio_set_mode(uint32_t mode) {
    if (!sRing) {
        sMode = mode == AR_AUDIO_MODE_SOURCES ? mode : AR_AUDIO_MODE_STEREO;
        gArSpatialSfx = sMode == AR_AUDIO_MODE_SOURCES;
    }
}
uint32_t sm64_audio_mode(void) { return sMode; }
int sm64_audio_state(void) { return atomic_load(&sAudioState); }
void sm64_audio_set_tick_frame(const sm64_audio_frame *f) { sFrame = *f; }

static double sTestEnergy;
static uint32_t sFrameMismatch;
#define TEST_MONO_MAX (1u << 17)
static float *sTestMono;
static uint32_t sTestMonoFrames;
static float (*sTestChan)[AR_SFX_TICK_FRAMES];
static uint32_t sTestChanFrames[AR_SFX_CHANNELS];
static sm64_sfx_lanes sLanes;
static float sHeldPos[AR_SFX_CHANNELS][3];
static uint64_t sHeldIdentity[AR_SFX_CHANNELS];
static uint8_t sHeldNoLoss[AR_SFX_CHANNELS];
static uint8_t sHeldFocus[AR_SFX_CHANNELS];
static float sPlayerFocus;
void sm64_audio_set_player_focus(float s) { sPlayerFocus = s > 0.f ? (s < 1.f ? s : 1.f) : 0.f; }
static float sObjectFocus;
void sm64_audio_set_object_focus(float s) { sObjectFocus = s > 0.f ? (s < 1.f ? s : 1.f) : 0.f; }
void sm64_audio_set_music_split(int on) { gArSpatialMusic = on && sMode == AR_AUDIO_MODE_SOURCES && gArSpatialSfx; }
int sm64_audio_music_split(void) { return gArSpatialMusic ? 1 : 0; }
/* Game thread only. */
static sm64_orchestra sOrch;
static int sRingOn;
static float sRingSize = 1.f, sRingRotate, sRingAngle;
static uint32_t sMusicLastId[SM64_MUSIC_CHANNELS];
static float sRingBox[4];
static int sRingLevel = -1, sRingArea = -1;
static int sTestUnseat;
static sm64_event_music sEvent;
static uint32_t sEventLastId[16];
static float sEventShare0, sEventShare1;
static int sEventWant, sEventSong = -1;
static uint32_t sOrchLayoutSeen;
static int sTestEventFull;
void sm64_audio_test_event_lanes_full(int on) { sTestEventFull = on != 0; }
static int sRingAtHead;
static float sRingFocus;
static float sRingY = NAN;
static int sRingSeats;
static float sBedAdd[AR_SFX_TICK_FRAMES];
static int sBedAddOn;
static int16_t sBedMix[2 * AR_SFX_TICK_FRAMES];
static uint8_t sLaneHidden[SM64_SFX_LANES];
static int sTestUnplace;
/* a sound that cannot be placed plays centred in the bed at -3 dB (c_play mixes sBedAdd). Each accumulator is
 * either here or in a lane, never both, so nothing is heard twice and nothing is lost. */
static void bed_add(const float *acc, uint32_t frames) {
    if (!sBedAddOn) { memset(sBedAdd, 0, sizeof sBedAdd); sBedAddOn = 1; }
    for (uint32_t f = 0; f < frames; f++) { sBedAdd[f] += acc[f]; }
}
void sm64_audio_test_sfx_lane_limit(int n) { sLanes.limit = n >= 1 && n < (int)SM64_SFX_LANES ? (uint32_t)n : SM64_SFX_LANES; }
void sm64_audio_test_unplace_sfx(int on) { sTestUnplace = on != 0; }
static double sTestBedAddEnergy;
double sm64_audio_test_take_bed_add_energy(void) { const double e = sTestBedAddEnergy; sTestBedAddEnergy = 0.0; return e; }
static int placeable(const float p[3]) { return !sTestUnplace && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]); }
void sm64_audio_test_unseat_music(int on) { sTestUnseat = on != 0; }
int sm64_audio_test_music_song(void) { return sOrch.seq_id; }
void sm64_audio_set_music_ring(int on, float size, float rotate_dps) {
    sRingOn = on != 0;
    if (!sRingOn) { sRingAngle = 0.f; }
    sRingSize = size >= 0.5f && size <= 2.f ? size : 1.f;
    sRingRotate = rotate_dps >= 0.f && rotate_dps <= 30.f ? rotate_dps : 0.f;
    sm64_audio_set_music_split(sRingOn);
}
void sm64_audio_set_ring_listener(int at_head, float focus) {
    sRingAtHead = at_head != 0;
    sRingFocus = focus > 0.f ? (focus < 1.f ? focus : 1.f) : 0.f;
}
/* A note is in the bed or in a lane, never both, so a routing switch cannot crossfade: the scene fades out on the old
 * routing, the next tick routes anew and fades in. */
void sm64_audio_set_options(const sm64_audio_options *o) {
    const int route_now = sMode == AR_AUDIO_MODE_SOURCES && sOpt.spatial, route_req = sMode == AR_AUDIO_MODE_SOURCES && o->spatial;
    const int hold = sOptSet && route_req != route_now && sOutGain > 0.f;
    const int spatial = hold ? sOpt.spatial : o->spatial;
    if (!sOptSet) { sOutGain = o->game_sound ? 1.f : 0.f; sOptSet = 1; }
    sOpt = *o;
    sOpt.spatial = spatial;
    sOutTarget = hold || !sOpt.game_sound ? 0.f : 1.f;
    gArSpatialSfx = sMode == AR_AUDIO_MODE_SOURCES && sOpt.spatial;
    sm64_audio_set_music_split(sRingOn);
    gArReverbOriginal = sOpt.reverb_original != 0;
}
int sm64_audio_test_tagged_notes(void) {
    int n = 0;
    for (s32 i = 0; i < gMaxSimultaneousNotes; i++) {
        if (gNotes[i].enabled && (gNotes[i].arSfxChannel || gNotes[i].arMusicChannel)) { n++; }
    }
    return n;
}
int sm64_audio_test_ring_seats(void) { return sRingSeats; }
void sm64_audio_test_move_ring(void) { sRingLevel = -2; }
static int sTestJoin = -1;
int sm64_audio_test_join_level_channel(int on) {
    sTestJoin = -1;
    for (int c = 0; on && c < (int)SM64_MUSIC_CHANNELS && sTestJoin < 0; c++) {
        if (!(sOrch.mask & (1u << c))) { sTestJoin = c; }
    }
    return sTestJoin;
}
int sm64_audio_test_env_song(void) {
    const struct SequencePlayer *ep = &gSequencePlayers[SEQ_PLAYER_ENV];
    return ep->enabled ? (int)ep->seqId : -1;
}
int sm64_audio_test_env_notes(void) {
    int n = 0;
    for (s32 i = 0; i < gMaxSimultaneousNotes; i++) {
        if (gNotes[i].enabled && gNotes[i].arMusicChannel > AR_MUSIC_CHANNELS) { n++; }
    }
    return n;
}
static double sTestMusicEnergy, sTestBedEnergy;
double sm64_audio_test_take_music_energy(void) { const double e = sTestMusicEnergy; sTestMusicEnergy = 0.0; return e; }
double sm64_audio_test_take_bed_energy(void) { const double e = sTestBedEnergy; sTestBedEnergy = 0.0; return e; }
static int16_t sLaneBuf[AR_SFX_TICK_FRAMES * AR_AUDIO_MAX_SOURCES];
static int sLaneChannel[AR_AUDIO_MAX_SOURCES];
static sm64_audio_spatial_stats sStats;
static int16_t clamp16(float x) { const long v = lrintf(x); return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
typedef struct ring_geom { float radius, cx, cz, y, near_d, focus; const float *head; } ring_geom;
static float ramp_at(float g0, float g1, uint32_t f, uint32_t frames) {
    return f + 1 == frames ? g1 : g0 + (g1 - g0) * (float)(f + 1) / (float)frames;
}
static void seat_music_lane(ar_audio_tick_meta *m, int l, uint32_t id, uint32_t *last_id, int seat, int count, float angle,
                            const float *acc, uint32_t frames, const ring_geom *r) {
    ar_audio_source *src = &m->lane[l];
    src->id = id;
    src->flags = AR_SRC_ACTIVE | AR_SRC_MUSIC | AR_SRC_NO_DISTANCE_LOSS | (id != *last_id ? AR_SRC_NEW : 0u);
    sm64_ring_point(seat, count, r->radius, angle, r->cx, r->cz, src->pos);
    src->pos[1] = r->y;
    const float dx = src->pos[0] - r->head[0], dy = src->pos[1] - r->head[1], dz = src->pos[2] - r->head[2];
    src->gain = ar_audio_ring_focus_gain(sqrtf(dx * dx + dy * dy + dz * dz), r->near_d, r->focus);
    *last_id = id;
    for (uint32_t f = 0; f < frames; f++) { sLaneBuf[f * AR_AUDIO_MAX_SOURCES + (uint32_t)l] = clamp16(acc[f]); }
    m->active++;
}
/* A channel's position, identity and no-loss flag are refreshed only while it has a current sound; during a release
 * tail they hold. */
static void build_sources(uint32_t frames, ar_audio_tick_meta *m) {
    sBedAddOn = 0;   /* first, so SFX bed adds made before the music loop are kept */
    if (gArSpatialSfx && gArSfxFrames != (s32)frames) { sFrameMismatch++; sStats.frame_mismatches++; }
    /* Unreachable: pc_main.c's _Static_assert(AR_SFX_TICK_FRAMES == 2 * SAMPLES_HIGH) caps every play(). A defensive
     * clamp only. */
    if (frames > AR_SFX_TICK_FRAMES) { frames = AR_SFX_TICK_FRAMES; }
    const void *mario_src = sm64_host_mario_sound_source();
    sm64_sfx_voice v[AR_SFX_CHANNELS];
    uint32_t n = 0;
    for (int c = 0; c < AR_SFX_CHANNELS; c++) {
        struct ArSfxChannelInfo info;
        ar_sfx_channel_info(c, &info);
        /* 0004-Viewer-relative-camera-controls-and-listener.patch computes cameraToObject before the frustum test, so
         * it is current for culled objects too; stale only for an inactive graph node or another area. */
        if (info.has_sound && info.spatial) {
            ar_listener_point(sFrame.head_in_table.pos, sFrame.head_in_table.quat, info.vec,
                              sm64_ar_listener_scale(sFrame.units_per_meter) * sFrame.units_per_meter, sHeldPos[c]);
            /* Identity is the source pointer plus the sound bits: a pool slot reused with the same sound, without a
             * gap, keeps the id (rare). */
            sHeldIdentity[c] = (uint64_t)(uintptr_t)info.source ^ ((uint64_t)(info.soundBits & 0xFFFF0000u) << 32);
            sHeldNoLoss[c] = info.no_volume_loss;
            sHeldFocus[c] = mario_src && (const void *)info.source == mario_src;
        }
        if (!gArSfxActive[c]) { continue; }
        double e = 0.0;
        for (uint32_t f = 0; f < frames; f++) { e += (double)gArSfxAccum[c][f] * gArSfxAccum[c][f]; }
        sTestEnergy += e;
        v[n].channel = (uint8_t)c;
        v[n].no_distance_loss = sHeldNoLoss[c];
        v[n].identity = sHeldIdentity[c];
        v[n].energy = e;
        n++;
    }
    if (sTestMono) {
        for (uint32_t f = 0; f < frames && sTestMonoFrames < TEST_MONO_MAX; f++) {
            float x = 0.f;
            for (uint32_t i = 0; i < n; i++) { x += gArSfxAccum[v[i].channel][f]; }
            sTestMono[sTestMonoFrames++] = x;
        }
        for (int c = 0; c < AR_SFX_CHANNELS; c++) { sTestChanFrames[c] = 0; }
        for (uint32_t i = 0; i < n; i++) {
            memcpy(sTestChan[v[i].channel], gArSfxAccum[v[i].channel], sizeof(float) * frames);
            sTestChanFrames[v[i].channel] = frames;
        }
    }
    int lane_of[AR_SFX_CHANNELS];
    m->dropped = sm64_sfx_lanes_assign(&sLanes, v, n, lane_of);
    /* Never silent: a voice without a lane or a finite position plays in the bed, so it is in exactly one place. */
    for (uint32_t i = 0; i < n; i++) {
        if (lane_of[i] >= 0 && placeable(sHeldPos[v[i].channel])) { continue; }
        bed_add(gArSfxAccum[v[i].channel], frames);
        sStats.to_bed++;
        sStats.to_bed_energy += v[i].energy;
        if (lane_of[i] >= 0) { sLaneHidden[lane_of[i]] = 1; lane_of[i] = -1; }
    }
    memset(sLaneBuf, 0, sizeof(int16_t) * frames * AR_AUDIO_MAX_SOURCES);
    for (int l = 0; l < (int)AR_AUDIO_MAX_SOURCES; l++) { sLaneChannel[l] = -1; }
    for (uint32_t i = 0; i < n; i++) {
        const int l = lane_of[i];
        if (l < 0) { continue; }
        const float *acc = gArSfxAccum[v[i].channel];
        for (uint32_t f = 0; f < frames; f++) { sLaneBuf[f * AR_AUDIO_MAX_SOURCES + (uint32_t)l] = clamp16(acc[f]); }
        sLaneChannel[l] = v[i].channel;
    }
    m->active = 0;
    const float floor_db = sm64_player_focus_floor_db(sPlayerFocus);
    float mario_t[3] = { 0.f, 0.f, 0.f };
    if (mario_src) {
        float mg[3];
        sm64_host_mario_gfx_pos(mg);
        mario_t[0] = mg[0] / sFrame.units_per_meter;
        mario_t[1] = (mg[1] - sFrame.ground_y) / sFrame.units_per_meter;
        mario_t[2] = mg[2] / sFrame.units_per_meter;
    }
    for (int l = 0; l < (int)SM64_SFX_LANES; l++) {
        const sm64_sfx_lane *ln = &sLanes.lane[l];
        m->lane[l].id = ln->id;
        m->lane[l].flags = ln->flags;
        m->lane[l].gain = 1.f;
        if ((ln->flags & AR_SRC_ACTIVE) && !placeable(sHeldPos[ln->channel])) {
            m->lane[l].id = 0; m->lane[l].flags = 0;
            continue;
        }
        if ((ln->flags & AR_SRC_ACTIVE) && sLaneHidden[l]) { m->lane[l].flags |= AR_SRC_NEW; sLaneHidden[l] = 0; }
        if (ln->flags & AR_SRC_ACTIVE) {
            memcpy(m->lane[l].pos, sHeldPos[ln->channel], sizeof m->lane[l].pos);
            m->active++;
            if (mario_src && sHeldFocus[ln->channel]) { m->lane[l].flags |= AR_SRC_FOCUS; }
            else if (mario_src && floor_db < 0.f && !(m->lane[l].flags & AR_SRC_NO_DISTANCE_LOSS)) {
                const float *p = m->lane[l].pos;
                const float dx = p[0] - mario_t[0], dy = p[1] - mario_t[1], dz = p[2] - mario_t[2];
                m->lane[l].gain = sm64_relevance_gain(sqrtf(dx * dx + dy * dy + dz * dz) * sFrame.units_per_meter, floor_db);
            }
        }
    }
    ar_audio_object_focus_apply(m->lane, (int)SM64_SFX_LANES, sFrame.head_in_table.pos, sObjectFocus);
    const struct SequencePlayer *sp = &gSequencePlayers[SEQ_PLAYER_LEVEL];
    uint16_t in_use = 0;
    for (int c = 0; c < (int)SM64_MUSIC_CHANNELS; c++) {
        if (sp->enabled && IS_SEQUENCE_CHANNEL_VALID(sp->channels[c])) { in_use |= (uint16_t)(1u << c); }
    }
    if (sTestJoin >= 0 && sp->enabled) { in_use |= (uint16_t)(1u << sTestJoin); }
    const int song = sOrch.seq_id;
    sm64_orchestra_update(&sOrch, sp->enabled ? (int)sp->seqId : -1, in_use);
    if (sOrch.seq_id != song) { sRingAngle = 0.f; }
    const float box[4] = { sFrame.box_min_m[0], sFrame.box_min_m[2], sFrame.box_max_m[0], sFrame.box_max_m[2] };
    const int lv = sm64_host_level(), area = sm64_host_area();
    if (memcmp(box, sRingBox, sizeof box) != 0 || lv != sRingLevel || area != sRingArea) {
        memcpy(sRingBox, box, sizeof box); sRingLevel = lv; sRingArea = area;
        sm64_orchestra_new_layout(&sOrch);
    }
    if (sOrch.layout != sOrchLayoutSeen) { sOrchLayoutSeen = sOrch.layout; sEvent.layout++; }
    const float radius = sm64_ring_radius(sFrame.box_min_m, sFrame.box_max_m, sRingSize);
    const float cx = 0.5f * (sFrame.box_min_m[0] + sFrame.box_max_m[0]), cz = 0.5f * (sFrame.box_min_m[2] + sFrame.box_max_m[2]);
    const float *head = sFrame.head_in_table.pos;
    sRingY = sm64_ring_height_step(sRingY, sRingAtHead, head);
    const float ring_y = isfinite(sRingY) ? sRingY : SM64_RING_HEIGHT_M;
    int seats = 0;
    (void)sm64_orchestra_seat(&sOrch, 0, &seats);
    sRingSeats = seats;
    const float near_d = sRingFocus > 0.f ? sm64_ring_nearest_seat(seats, radius, sRingAngle, cx, cz, ring_y, head) : NAN;
    const ring_geom ring = { radius, cx, cz, ring_y, near_d, sRingFocus, head };
    for (int c = 0; c < (int)SM64_MUSIC_CHANNELS; c++) {
        const int l = (int)SM64_MUSIC_LANE0 + c;
        ar_audio_source *src = &m->lane[l];
        int count = 0;
        const int seat = sTestUnseat ? -1 : sm64_orchestra_seat(&sOrch, c, &count);
        if (!(gArSpatialMusic && gArMusicActive[c]) || seat < 0) {
            src->flags = 0; src->id = 0; src->gain = 1.f;
            if (gArSpatialMusic && gArMusicActive[c]) {
                bed_add(gArMusicAccum[c], frames);
            }
            continue;
        }
        seat_music_lane(m, l, sm64_orchestra_lane_id(&sOrch, c), &sMusicLastId[c], seat, count, sRingAngle, gArMusicAccum[c],
                        frames, &ring);
    }
    {
        const struct SequencePlayer *ep = &gSequencePlayers[SEQ_PLAYER_ENV];
        uint16_t env_in_use = 0;
        for (int c = 0; c < AR_ENV_CHANNELS; c++) {
            if (ep->enabled && IS_SEQUENCE_CHANNEL_VALID(ep->channels[c])) { env_in_use |= (uint16_t)(1u << c); }
        }
        /* a fade-out tick keeps the song it fades (the ENV player may have left it already) */
        sm64_event_music_update(&sEvent, gArSpatialEnvMusic ? (sEventWant ? (int)ep->seqId : sEventSong) : -1, env_in_use,
                                sTestEventFull ? (uint16_t)0xFFFFu : sOrch.mask);
        const float g0 = sEventShare0, g1 = sEventShare1;
        for (int c = 0; c < AR_ENV_CHANNELS; c++) {
            if (!gArEnvActive[c]) { continue; }
            const int l = sm64_event_music_lane(&sEvent, c);
            int count = 0;
            const int seat = sm64_event_music_seat(&sEvent, c, &count);
            if (l < 0 || seat < 0 || (m->lane[l].flags & AR_SRC_ACTIVE)) {
                double e = 0.0;
                for (uint32_t f = 0; f < frames; f++) { e += (double)gArEnvAccum[c][f] * gArEnvAccum[c][f]; }
                bed_add(gArEnvAccum[c], frames);
                sStats.to_bed++;
                sStats.to_bed_event++;
                sStats.to_bed_energy += e;
                continue;
            }
            seat_music_lane(m, l, sm64_event_music_lane_id(&sEvent, c), &sEventLastId[c], seat, count,
                            sRingAngle + sm64_event_music_seat_offset(seats, count), gArEnvAccum[c], frames, &ring);
            if (g0 != 1.f || g1 != 1.f) {
                float rest[AR_SFX_TICK_FRAMES];
                for (uint32_t f = 0; f < frames; f++) {
                    const float k = ramp_at(g0, g1, f, frames);
                    sLaneBuf[f * AR_AUDIO_MAX_SOURCES + (uint32_t)l] = clamp16(gArEnvAccum[c][f] * k);
                    rest[f] = gArEnvAccum[c][f] * (1.f - k);
                }
                bed_add(rest, frames);
            }
        }
    }
    sStats.ticks++;
    sStats.dropped = sLanes.dropped;
    sStats.stolen = sLanes.stolen;
    sStats.overflow_frames = gArSfxOverflow;
    if (m->active > sStats.max_active) { sStats.max_active = m->active; }
    for (int c = 0; c < AR_MUSIC_CHANNELS; c++) {
        if (!gArMusicActive[c]) { continue; }
        for (uint32_t f = 0; f < frames; f++) { sTestMusicEnergy += (double)gArMusicAccum[c][f] * gArMusicAccum[c][f]; }
    }
    ar_sfx_tick_reset();
}
sm64_audio_spatial_stats sm64_audio_get_spatial_stats(void) { return sStats; }
int sm64_audio_test_lane_channel(int lane) { return lane >= 0 && lane < (int)AR_AUDIO_MAX_SOURCES ? sLaneChannel[lane] : -1; }
void sm64_audio_test_allow_spatial(int on) { gArSfxAllowSpatial = on != 0; }
void sm64_audio_test_mark_spatial(int first, int count) {
    for (int c = first; c < first + count; c++) { if (c >= 0 && c < AR_SFX_CHANNELS) { gArSfxChannelSpatial[c] = 1; } }
}
double sm64_audio_test_take_sfx_energy(void) { const double e = sTestEnergy; sTestEnergy = 0.0; return e; }
uint32_t sm64_audio_test_frame_mismatches(void) { return sFrameMismatch; }
uint32_t sm64_audio_test_sfx_overflow(void) { return gArSfxOverflow; }
void sm64_audio_test_record_sfx_mono(void) {
    if (!sTestMono) {
        sTestMono = (float *)calloc(TEST_MONO_MAX, sizeof(float));
        sTestChan = (float (*)[AR_SFX_TICK_FRAMES])calloc(AR_SFX_CHANNELS, sizeof *sTestChan);
    }
    sTestMonoFrames = 0;
}
uint32_t sm64_audio_test_sfx_mono(const float **samples) { *samples = sTestMono; return sTestMono ? sTestMonoFrames : 0; }
uint32_t sm64_audio_test_channel_samples(int c, const float **samples) {
    *samples = NULL;
    if (!sTestChan || c < 0 || c >= AR_SFX_CHANNELS) { return 0; }
    *samples = sTestChan[c];
    return sTestChanFrames[c];
}
int sm64_audio_test_channel(int c, sm64_audio_channel_view *v) {
    memset(v, 0, sizeof *v);
    if (c < 0 || c >= AR_SFX_CHANNELS) { return 0; }
    struct ArSfxChannelInfo i;
    ar_sfx_channel_info(c, &i);
    v->has_sound = i.has_sound; v->spatial = i.spatial; v->bank = i.bank; v->no_volume_loss = i.no_volume_loss;
    v->sound_bits = i.soundBits;
    v->source = (const void *)i.source;
    struct SequenceChannel *ch = gSequencePlayers[SEQ_PLAYER_SFX].channels[c];
    if (IS_SEQUENCE_CHANNEL_VALID(ch)) { v->volume = ch->volume; v->pan = ch->pan; v->freq_scale = ch->freqScale; v->reverb = ch->reverb; }
    return 1;
}

static bool c_init(void) { return true; }
/* buffer_audio() makes SAMPLES_HIGH frames when buffered() < desired, else SAMPLES_LOW: report how far generation is
 * ahead of 32000/30 per tick so the average is exact whatever the host pulls. */
static int c_buffered(void) { return (int)((int64_t)sGenerated - (int64_t)(sTicks * RATE / 30u)); }
static int c_desired(void) { return 0; }
static void c_play(const uint8_t *buf, size_t len) {
    const uint32_t frames = (uint32_t)(len / 4u);
    const float g0 = sOutGain, g1 = sOutTarget;
    sOutGain = g1;
    if (sRing) {
        ar_audio_tick_meta m;
        fill_meta(&m);
        const int16_t *lanes = NULL;
        if (sMode == AR_AUDIO_MODE_SOURCES) {
            build_sources(frames, &m);
            lanes = frames <= AR_SFX_TICK_FRAMES ? sLaneBuf : NULL;
        }
        const int16_t *bed = (const int16_t *)buf;
        if (sMode == AR_AUDIO_MODE_SOURCES && sBedAddOn && frames <= AR_SFX_TICK_FRAMES) {
            /* The split's mono is (L + R) / sqrt(2) of a centre-panned pair. */
            for (uint32_t f = 0; f < frames; f++) {
                sTestBedAddEnergy += (double)sBedAdd[f] * sBedAdd[f];
                const float x = sBedAdd[f] * 0.70710678f;
                sBedMix[2 * f] = clamp16((float)bed[2 * f] + x);
                sBedMix[2 * f + 1] = clamp16((float)bed[2 * f + 1] + x);
            }
            bed = sBedMix;
        }
        sBedAddOn = 0;
        if ((g0 != 1.f || g1 != 1.f) && frames <= AR_SFX_TICK_FRAMES) {
            if (g0 == 0.f && g1 == 0.f) {
                memset(sBedMix, 0, sizeof(int16_t) * 2u * frames);
                if (lanes) { memset(sLaneBuf, 0, sizeof(int16_t) * frames * AR_AUDIO_MAX_SOURCES); }
            } else {
                const float dg = (g1 - g0) / (float)frames;
                for (uint32_t f = 0; f < frames; f++) {
                    const float g = f + 1 == frames ? g1 : g0 + dg * (float)(f + 1);
                    sBedMix[2 * f] = clamp16((float)bed[2 * f] * g);
                    sBedMix[2 * f + 1] = clamp16((float)bed[2 * f + 1] * g);
                    if (lanes) {
                        int16_t *x = &sLaneBuf[f * AR_AUDIO_MAX_SOURCES];
                        for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) { x[l] = clamp16((float)x[l] * g); }
                    }
                }
            }
            bed = sBedMix;   /* sBedMix may be the bed itself (the bed-add above): each frame is read before it is written */
        }
        ar_scene_ring_write(sRing, bed, lanes, frames, &m);
        for (uint32_t i = 0; i < 2u * frames; i++) { sTestBedEnergy += (double)bed[i] * bed[i]; }
    }
    sGenerated += frames;
}
static void c_shutdown(void) {}
static struct AudioAPI sApi = { c_init, c_buffered, c_desired, c_play, c_shutdown };

static void apply_volume(void) {
    float g = sGain * sScale;
    if (!(g >= 0.f)) { g = 0.f; }
    if (g > 1.f) { g = 1.f; }
    gLuaVolumeLevel = (unsigned char)lrintf(127.f * g);
}
void sm64_audio_install(void) {
    if (!sRing) {
        sm64_sfx_lanes_reset(&sLanes);
        memset(sLaneHidden, 0, sizeof sLaneHidden);
        sm64_orchestra_reset(&sOrch);
        sm64_event_music_reset(&sEvent);
        for (int l = 0; l < (int)AR_AUDIO_MAX_SOURCES; l++) { sLaneChannel[l] = -1; }
        sRing = ar_scene_ring_create(sMode == AR_AUDIO_MODE_SOURCES, sMode == AR_AUDIO_MODE_SOURCES ? (1u << 19) : (1u << 20), 2048);
        atomic_store(&sAudioState, sRing ? (sMode == AR_AUDIO_MODE_SOURCES ? 1 : 0) : -1);
    }
    gAudioApi = &sApi;
    gMasterVolume = 1.f;
    apply_volume();
}
/* Reassert full-scale capture every tick: a config reload, Lua or a djui slider can set gMasterVolume to 0, and then
 * buffer_audio() skips play() altogether (gaps in the capture, not silence). */
static bool sMuteFocusLoss;
void sm64_audio_tick(void) {
    sTicks++;
    {
        const struct SequencePlayer *ep = &gSequencePlayers[SEQ_PLAYER_ENV];
        sEventWant = gArSpatialMusic && sOpt.event_music_ring && ep->enabled && sm64_event_music_is_song((int)ep->seqId);
        if (sEventWant) { sEventSong = (int)ep->seqId; }
        sEventShare0 = gArSpatialMusic ? sEventShare1 : 0.f;
        sEventShare1 = sEventWant ? 1.f : 0.f;
        gArSpatialEnvMusic = sEventWant || sEventShare0 > 0.f;
    }
    if (sRingOn) { sRingAngle = sm64_ring_angle_step(sRingAngle, sRingRotate); }
    gMasterVolume = 1.f;
    apply_volume();
    /* AR never mutes for lost focus (buffer_audio() would skip play()); the configured value stays as it was. */
    sMuteFocusLoss = configMuteFocusLoss;
    configMuteFocusLoss = false;
}
void sm64_audio_tick_end(void) { configMuteFocusLoss = sMuteFocusLoss; }
uint32_t sm64_audio_pull_scene(ar_audio_pull *p) { if (!sRing) { p->meta_count = 0; return 0; } return ar_scene_ring_read(sRing, p); }
uint32_t sm64_audio_pull(int16_t *stereo, uint32_t frames) {
    ar_audio_pull p; memset(&p, 0, sizeof p);
    p.bed = stereo; p.frames = frames;
    return sm64_audio_pull_scene(&p);
}
void sm64_audio_set_music_gain(float gain) { sGain = gain; }
void sm64_audio_set_music_scale(float scale) { sScale = scale; apply_volume(); }

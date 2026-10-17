#pragma once
#include <stdint.h>
#include "ar_audio_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_MUSIC_FALLOFF_PER_M 0.5f
void sm64_audio_install(void);
void sm64_audio_tick(void);
void sm64_audio_tick_end(void);
typedef struct sm64_audio_frame {
    float units_per_meter, ground_y;
    ar_pose head_in_table;
    float box_min_m[3], box_max_m[3];
} sm64_audio_frame;
/* Before sm64_audio_install (ignored after it). */
void sm64_audio_set_mode(uint32_t mode);
uint32_t sm64_audio_mode(void);
int sm64_audio_state(void);
void sm64_audio_set_tick_frame(const sm64_audio_frame *f);
uint32_t sm64_audio_pull_scene(ar_audio_pull *p);
uint32_t sm64_audio_pull(int16_t *stereo, uint32_t frames);
void sm64_audio_set_music_gain(float gain);
void sm64_audio_set_music_scale(float scale);
void sm64_audio_set_player_focus(float strength);
void sm64_audio_set_object_focus(float strength);
/* Game thread, before a tick: gArSpatialMusic (0011-Per-channel-sound-and-music.patch). Only in AR_AUDIO_MODE_SOURCES. */
void sm64_audio_set_music_split(int on);
int  sm64_audio_music_split(void);
void sm64_audio_set_music_ring(int on, float size, float rotate_dps);
void sm64_audio_set_ring_listener(int at_head, float focus);
typedef struct sm64_audio_options { int spatial, game_sound, reverb_original, event_music_ring; } sm64_audio_options;
void sm64_audio_set_options(const sm64_audio_options *o);
int  sm64_audio_test_tagged_notes(void);
int  sm64_audio_test_ring_seats(void);
int  sm64_audio_test_music_song(void);
void sm64_audio_test_unseat_music(int on);
void sm64_audio_test_move_ring(void);
int  sm64_audio_test_join_level_channel(int on);
int  sm64_audio_test_env_song(void);
int  sm64_audio_test_env_notes(void);
void sm64_audio_test_event_lanes_full(int on);
void sm64_audio_test_allow_spatial(int on);
void sm64_audio_test_mark_spatial(int first_channel, int count);
double sm64_audio_test_take_sfx_energy(void);
double sm64_audio_test_take_music_energy(void);
double sm64_audio_test_take_bed_energy(void);
uint32_t sm64_audio_test_frame_mismatches(void);
uint32_t sm64_audio_test_sfx_overflow(void);
void sm64_audio_test_record_sfx_mono(void);
uint32_t sm64_audio_test_sfx_mono(const float **samples);
uint32_t sm64_audio_test_channel_samples(int channel, const float **samples);
typedef struct sm64_audio_channel_view {
    int has_sound, spatial, bank, no_volume_loss;
    uint32_t sound_bits;
    float volume, pan, freq_scale;
    int reverb;
    const void *source;
} sm64_audio_channel_view;
int sm64_audio_test_channel(int channel, sm64_audio_channel_view *out);
typedef struct sm64_audio_spatial_stats {
    uint64_t ticks, dropped, stolen, overflow_frames, frame_mismatches;
    uint32_t max_active;
    uint64_t to_bed; double to_bed_energy;
    uint64_t to_bed_event;
} sm64_audio_spatial_stats;
sm64_audio_spatial_stats sm64_audio_get_spatial_stats(void);
void sm64_audio_test_sfx_lane_limit(int n);
void sm64_audio_test_unplace_sfx(int on);
double sm64_audio_test_take_bed_add_energy(void);
int sm64_audio_test_lane_channel(int lane);
#ifdef __cplusplus
}
#endif

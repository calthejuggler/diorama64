#pragma once
#include <stdint.h>
#include "ar_types.h"
#include "ar_texture_fifo.h"
#include "fc_frame.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AR_ADAPTER_ABI_VERSION 5u
#define AR_MAX_PLAYERS 4
#include "ar_audio_types.h"
typedef struct ar_player_input {
    uint8_t present, pad_connected, _reserved[2];
    ar_pad pad;
    ar_pose head_in_table;            /* metres, table space (level axes; scale and ground offset only) */
    ar_pose input_frame_in_table;     /* v2: the pose the stick is relative to. Live it is head_in_table; a replay of a
                                         recording with SAREC_FLAG_INPUT_FRAME pins the recorded one, so a different
                                         viewpoint does not change the game (the view still follows head_in_table). */
} ar_player_input;
typedef struct ar_adapter {
    uint32_t abi_version;
    const char *name;
    int  (*init)(const char *rom_path, const char *save_dir);      /* 1 ok; once per process */
    const char *(*last_error)(void);
    void (*tick)(const ar_player_input *players, uint32_t count);
    const fc_frame *(*acquire_snapshot)(void);                     /* latest published, +1 ref; NULL if none */
    void (*release_snapshot)(const fc_frame *frame);               /* any thread */
    uint32_t (*drain_texture_uploads)(uint64_t up_to_seq, ar_texture_upload_fn fn, void *user);
    ar_level_info (*level_info)(void);                              /* game thread */
    /* The bed, and in AR_AUDIO_MODE_SOURCES the lanes and per-tick metadata, aligned. Never blocks; one consumer
     * thread. Optional (NULL: no audio). */
    uint32_t (*audio_pull)(ar_audio_pull *p);
    uint64_t (*state_hash)(void);
    const char *(*status_line)(void);
    /* v3, optional (NULL: stereo only). Setup thread, before init: AR_AUDIO_MODE_STEREO or AR_AUDIO_MODE_SOURCES. */
    void (*audio_set_mode)(uint32_t mode);
} ar_adapter;
#ifdef __cplusplus
}
#endif

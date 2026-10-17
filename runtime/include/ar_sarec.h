/* Files are the structs' raw bytes: little-endian, IEEE-754. v1 (16-byte header + 72-byte ticks) still loads; its pads
 * play, nothing is checked. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SAREC_MAGIC 0x43455241u
#define SAREC_SETUP_MAX 87                /* the longest setup string a recording holds (its field less the NUL): every
                                             place that builds a setup (harness, app, ar_game_args.h) checks this bound */
#define SAREC_VERSION 2u
#define SAREC_MAX_TICKS 10000000u
#define SAREC_FLAG_HASHES 1u
#define SAREC_FLAG_INPUT_FRAME 2u
#define SAREC_FLAGS_KNOWN (SAREC_FLAG_HASHES | SAREC_FLAG_INPUT_FRAME)
#define SAREC_EV_CHECKPOINT 1u
/* One game tick. Poses are metres, table space, except `table` (tracking_from_table; identity when synthetic). */
typedef struct sarec_tick {
    uint32_t tick;
    uint32_t events;
    ar_pad pad;
    ar_pose head;
    ar_pose input_frame;
    ar_pose table;
    uint64_t state_hash;
} sarec_tick;
typedef struct sarec_header {
    uint32_t magic, version, count, tick_size, flags, reserved;
    char game[16];
    char setup[SAREC_SETUP_MAX + 1];
} sarec_header;
typedef struct sarec {
    uint32_t count, cap;
    uint32_t version;
    uint32_t flags;
    char game[16];
    char setup[SAREC_SETUP_MAX + 1];
    sarec_tick *ticks;
    uint32_t dropped;
} sarec;
void sarec_init(sarec *r, const char *game, const char *setup);
int  sarec_save(const sarec *r, const char *path);
int  sarec_load_err(sarec *r, const char *path, char *err, size_t err_cap);
int  sarec_load(sarec *r, const char *path);
int  sarec_push(sarec *r, sarec_tick t);
void sarec_free(sarec *r);
#ifdef __cplusplus
}
#endif

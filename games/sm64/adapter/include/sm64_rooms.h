#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_ROOMS_LEVELS 64
#define SM64_ROOMS_AREAS 8
#define SM64_ROOMS_ROOMS 128        /* a surface's room is an s8: rooms 1..127 */
#define SM64_ROOMS_FADE_TICKS 15
#define SM64_ROOMS_FILE_VERSION 1
/* The file: "ARVR", u16 version (little-endian), u16 count, then count entries of 3 bytes: level, area, room. */
#define SM64_ROOMS_FILE_HEADER 8
#define SM64_ROOMS_FILE_MAX (SM64_ROOMS_FILE_HEADER + 3 * SM64_ROOMS_LEVELS * SM64_ROOMS_AREAS * (SM64_ROOMS_ROOMS - 1))
typedef struct sm64_rooms {
    uint8_t visited[SM64_ROOMS_LEVELS][SM64_ROOMS_AREAS][SM64_ROOMS_ROOMS / 8];
    int dirty;
    int level, area, have;
    uint8_t shown[SM64_ROOMS_ROOMS / 8];
    uint32_t since[SM64_ROOMS_ROOMS];
    uint8_t doorway[SM64_ROOMS_ROOMS][2];
} sm64_rooms;
void sm64_rooms_clear(sm64_rooms *r);
int  sm64_rooms_visited(const sm64_rooms *r, int level, int area, int room);
int  sm64_rooms_visit(sm64_rooms *r, int level, int area, int room);
int  sm64_rooms_count(const sm64_rooms *r);
void sm64_rooms_update(sm64_rooms *r, int level, int area, int mario_room, const int *entering, int n_entering, uint32_t now);
void sm64_rooms_restart_view(sm64_rooms *r);
uint8_t sm64_rooms_coverage(const sm64_rooms *r, int room, uint32_t now);
/* A doorway room (vanilla's room switch draws it as both rooms the door joins: castle 10..17, BBH 14..32, HMC 9..18) is
 * drawn only as far as both its rooms are. */
void sm64_rooms_clear_doorways(sm64_rooms *r);
void sm64_rooms_doorway(sm64_rooms *r, int room, int front, int back);
int  sm64_rooms_door_room(const sm64_rooms *r, int front, int back, uint32_t now);
int  sm64_rooms_spawn_room(const sm64_rooms *r, int room, uint32_t now);
size_t sm64_rooms_encode(const sm64_rooms *r, uint8_t *buf, size_t cap);
int  sm64_rooms_decode(sm64_rooms *r, const uint8_t *buf, size_t len);
int  sm64_rooms_path(char *buf, size_t cap, const char *save_dir, int file);
int  sm64_rooms_load(sm64_rooms *r, const char *path);
int  sm64_rooms_save(sm64_rooms *r, const char *path);
#ifdef __cplusplus
}
#endif

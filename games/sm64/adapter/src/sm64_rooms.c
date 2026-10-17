#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "sm64_rooms.h"

static int in_range(int level, int area, int room) {
    return level >= 0 && level < SM64_ROOMS_LEVELS && area >= 0 && area < SM64_ROOMS_AREAS && room >= 1 && room < SM64_ROOMS_ROOMS;
}
static int bit(const uint8_t *bits, int i) { return (bits[i >> 3] >> (i & 7)) & 1; }
static void set_bit(uint8_t *bits, int i) { bits[i >> 3] |= (uint8_t)(1u << (i & 7)); }

void sm64_rooms_clear(sm64_rooms *r) { memset(r, 0, sizeof *r); }
int sm64_rooms_visited(const sm64_rooms *r, int level, int area, int room) {
    return in_range(level, area, room) && bit(r->visited[level][area], room);
}
int sm64_rooms_visit(sm64_rooms *r, int level, int area, int room) {
    if (!in_range(level, area, room) || bit(r->visited[level][area], room)) { return 0; }
    set_bit(r->visited[level][area], room);
    r->dirty = 1;
    return 1;
}
int sm64_rooms_count(const sm64_rooms *r) {
    int n = 0;
    for (int l = 0; l < SM64_ROOMS_LEVELS; l++)
        for (int a = 0; a < SM64_ROOMS_AREAS; a++)
            for (int k = 1; k < SM64_ROOMS_ROOMS; k++) { n += bit(r->visited[l][a], k); }
    return n;
}
void sm64_rooms_restart_view(sm64_rooms *r) { r->have = 0; }
void sm64_rooms_update(sm64_rooms *r, int level, int area, int mario_room, const int *entering, int n_entering, uint32_t now) {
    if (level < 0 || level >= SM64_ROOMS_LEVELS || area < 0 || area >= SM64_ROOMS_AREAS) { r->have = 0; return; }
    const int first = !r->have || r->level != level || r->area != area;
    sm64_rooms_visit(r, level, area, mario_room);
    uint8_t now_shown[SM64_ROOMS_ROOMS / 8];
    memcpy(now_shown, r->visited[level][area], sizeof now_shown);
    for (int i = 0; entering && i < n_entering; i++) {
        if (entering[i] >= 1 && entering[i] < SM64_ROOMS_ROOMS) { set_bit(now_shown, entering[i]); }
    }
    for (int k = 1; k < SM64_ROOMS_ROOMS; k++) {
        if (!bit(now_shown, k)) { continue; }
        if (first) { r->since[k] = now - SM64_ROOMS_FADE_TICKS; }
        else if (!bit(r->shown, k)) { r->since[k] = now; }
    }
    memcpy(r->shown, now_shown, sizeof now_shown);
    r->level = level; r->area = area; r->have = 1;
}
static uint8_t room_coverage(const sm64_rooms *r, int room, uint32_t now);
uint8_t sm64_rooms_coverage(const sm64_rooms *r, int room, uint32_t now) {
    if (!r->have || room < 1 || room >= SM64_ROOMS_ROOMS) { return 255; }
    if (r->doorway[room][0]) {
        const uint8_t a = room_coverage(r, r->doorway[room][0], now), b = room_coverage(r, r->doorway[room][1], now);
        return a < b ? a : b;
    }
    return room_coverage(r, room, now);
}
static uint8_t room_coverage(const sm64_rooms *r, int room, uint32_t now) {
    if (!bit(r->shown, room)) { return 0; }
    const uint32_t t = now - r->since[room];
    if (t >= SM64_ROOMS_FADE_TICKS) { return 255; }
    const uint32_t c = (t + 1) * 255u / SM64_ROOMS_FADE_TICKS;
    return (uint8_t)(c < 255u ? c : 254u);
}
void sm64_rooms_clear_doorways(sm64_rooms *r) { memset(r->doorway, 0, sizeof r->doorway); }
void sm64_rooms_doorway(sm64_rooms *r, int room, int front, int back) {
    const int ok = room >= 1 && room < SM64_ROOMS_ROOMS && front >= 1 && front < SM64_ROOMS_ROOMS && back >= 1 &&
                   back < SM64_ROOMS_ROOMS && front != room && back != room;
    if (ok) { r->doorway[room][0] = (uint8_t)front; r->doorway[room][1] = (uint8_t)back; }
}
int sm64_rooms_spawn_room(const sm64_rooms *r, int room, uint32_t now) {
    return room >= 1 && room < SM64_ROOMS_ROOMS && r->doorway[room][0] ? sm64_rooms_door_room(r, r->doorway[room][0], r->doorway[room][1], now)
                                                                       : (room > 0 ? room : 0);
}
int sm64_rooms_door_room(const sm64_rooms *r, int front, int back, uint32_t now) {
    if (front <= 0) { return back > 0 ? back : 0; }
    if (back <= 0) { return front; }
    return sm64_rooms_coverage(r, back, now) > sm64_rooms_coverage(r, front, now) ? back : front;
}

size_t sm64_rooms_encode(const sm64_rooms *r, uint8_t *buf, size_t cap) {
    const int n = sm64_rooms_count(r);
    const size_t need = SM64_ROOMS_FILE_HEADER + 3u * (size_t)n;
    if (!buf || cap < need) { return need; }
    memcpy(buf, "ARVR", 4);
    buf[4] = SM64_ROOMS_FILE_VERSION & 0xFF; buf[5] = SM64_ROOMS_FILE_VERSION >> 8;
    buf[6] = (uint8_t)(n & 0xFF); buf[7] = (uint8_t)(n >> 8);
    size_t at = SM64_ROOMS_FILE_HEADER;
    for (int l = 0; l < SM64_ROOMS_LEVELS; l++)
        for (int a = 0; a < SM64_ROOMS_AREAS; a++)
            for (int k = 1; k < SM64_ROOMS_ROOMS; k++) {
                if (!bit(r->visited[l][a], k)) { continue; }
                buf[at++] = (uint8_t)l; buf[at++] = (uint8_t)a; buf[at++] = (uint8_t)k;
            }
    return need;
}
int sm64_rooms_decode(sm64_rooms *r, const uint8_t *buf, size_t len) {
    sm64_rooms_clear(r);
    if (!buf || len < SM64_ROOMS_FILE_HEADER || len > SM64_ROOMS_FILE_MAX || memcmp(buf, "ARVR", 4) != 0) { return 0; }
    const unsigned version = buf[4] | (unsigned)buf[5] << 8, count = buf[6] | (unsigned)buf[7] << 8;
    if (version != SM64_ROOMS_FILE_VERSION || len != SM64_ROOMS_FILE_HEADER + 3u * (size_t)count) { return 0; }
    for (size_t i = SM64_ROOMS_FILE_HEADER; i < len; i += 3) {
        if (!in_range(buf[i], buf[i + 1], buf[i + 2])) { sm64_rooms_clear(r); return 0; }
        set_bit(r->visited[buf[i]][buf[i + 1]], buf[i + 2]);
    }
    return 1;
}
int sm64_rooms_path(char *buf, size_t cap, const char *save_dir, int file) {
    if (!buf || !cap || !save_dir || !save_dir[0] || file < 1 || file > 99) { return 0; }
    const int n = snprintf(buf, cap, "%s/ar-visited-%d.bin", save_dir, file);
    return n > 0 && (size_t)n < cap;
}
int sm64_rooms_load(sm64_rooms *r, const char *path) {
    static uint8_t buf[SM64_ROOMS_FILE_MAX + 1];   /* one more: a longer file is malformed, not cut short */
    sm64_rooms_clear(r);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) { return 0; }
    const size_t len = fread(buf, 1, sizeof buf, f);
    const int err = ferror(f);
    fclose(f);
    return !err && sm64_rooms_decode(r, buf, len);
}
int sm64_rooms_save(sm64_rooms *r, const char *path) {
    static uint8_t buf[SM64_ROOMS_FILE_MAX];
    char tmp[1100];
    if (!path || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) { return 0; }
    const size_t len = sm64_rooms_encode(r, buf, sizeof buf);
    FILE *f = fopen(tmp, "wb");
    if (!f) { return 0; }
    const int ok = fwrite(buf, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;   /* on disk before the rename */
    if (fclose(f) != 0 || !ok || rename(tmp, path) != 0) { remove(tmp); return 0; }
    r->dirty = 0;
    return 1;
}

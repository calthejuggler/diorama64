#include "ar_sarec.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(sarec_tick) == 128, "sarec_tick v2 on-disk layout");
_Static_assert(sizeof(sarec_header) == 128, "sarec v2 header");
typedef struct { uint32_t tick, reserved; ar_pad pad; ar_pose head; uint64_t state_hash; } sarec_tick_v1;
_Static_assert(sizeof(sarec_tick_v1) == 72, "sarec_tick v1 on-disk layout");

static int fail(sarec *r, FILE *f, char *err, size_t cap, const char *fmt, ...) {
    if (f) fclose(f);
    sarec_free(r);
    if (err && cap) { va_list ap; va_start(ap, fmt); vsnprintf(err, cap, fmt, ap); va_end(ap); }
    return 0;
}
static void copy_str(char *dst, size_t cap, const char *src) {
    memset(dst, 0, cap);
    if (src) strncpy(dst, src, cap - 1);
}
void sarec_init(sarec *r, const char *game, const char *setup) {
    memset(r, 0, sizeof *r);
    r->version = SAREC_VERSION;
    copy_str(r->game, sizeof r->game, game);
    copy_str(r->setup, sizeof r->setup, setup);
}
int sarec_push(sarec *r, sarec_tick t) {
    if (r->count == r->cap) {
        uint32_t ncap = r->cap ? r->cap * 2 : 256;
        sarec_tick *n = realloc(r->ticks, ncap * sizeof *r->ticks);
        if (!n) { r->dropped++; return 0; }
        r->ticks = n; r->cap = ncap;
    }
    r->ticks[r->count++] = t;
    return 1;
}
void sarec_free(sarec *r) { free(r->ticks); memset(r, 0, sizeof *r); }
int sarec_save(const sarec *r, const char *path) {
    if (r->count > SAREC_MAX_TICKS || (r->count && !r->ticks)) return 0;
    sarec_header h;
    memset(&h, 0, sizeof h);
    h.magic = SAREC_MAGIC; h.version = SAREC_VERSION; h.count = r->count;
    h.tick_size = (uint32_t)sizeof(sarec_tick); h.flags = r->flags;
    copy_str(h.game, sizeof h.game, r->game);
    copy_str(h.setup, sizeof h.setup, r->setup);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && (r->count == 0 || fwrite(r->ticks, sizeof *r->ticks, r->count, f) == r->count);
    return fclose(f) == 0 && ok;
}
static long size_of(FILE *f) {
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long n = ftell(f);
    return fseek(f, 0, SEEK_SET) == 0 ? n : -1;
}
static const ar_pose kIdentity = { { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f, 1.f } };
int sarec_load_err(sarec *r, const char *path, char *err, size_t cap) {
    memset(r, 0, sizeof *r);
    if (err && cap) err[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(r, NULL, err, cap, "cannot open %s", path);
    const long size = size_of(f);
    uint32_t head[4];
    if (size < 16 || fread(head, sizeof head, 1, f) != 1 || head[0] != SAREC_MAGIC)
        return fail(r, f, err, cap, "not a .sarec (bad magic or shorter than a header)");
    if (head[1] == 1u) {
        const uint32_t n = head[2];
        if (n > SAREC_MAX_TICKS || (unsigned long long)size != 16ull + (unsigned long long)n * sizeof(sarec_tick_v1))
            return fail(r, f, err, cap, "v1 file size %ld does not match its %u ticks", size, n);
        r->ticks = malloc((n ? n : 1) * sizeof *r->ticks);
        if (!r->ticks) return fail(r, f, err, cap, "out of memory");
        for (uint32_t i = 0; i < n; i++) {
            sarec_tick_v1 t;
            if (fread(&t, sizeof t, 1, f) != 1) return fail(r, f, err, cap, "v1 read failed at tick %u", i);
            sarec_tick *o = &r->ticks[i];
            memset(o, 0, sizeof *o);
            o->tick = t.tick; o->pad = t.pad; o->head = t.head; o->input_frame = t.head; o->table = kIdentity;
            o->state_hash = t.state_hash;
        }
        fclose(f);
        r->count = r->cap = n; r->version = 1; r->flags = 0;   /* v1 hashes and heads are not trusted for checks */
        return 1;
    }
    if (head[1] != SAREC_VERSION) return fail(r, f, err, cap, "unsupported .sarec version %u", head[1]);
    sarec_header h;
    if (size < (long)sizeof h || fseek(f, 0, SEEK_SET) != 0 || fread(&h, sizeof h, 1, f) != 1)
        return fail(r, f, err, cap, "v2 header truncated");
    if (h.tick_size != sizeof(sarec_tick)) return fail(r, f, err, cap, "tick size %u, expected %u", h.tick_size, (unsigned)sizeof(sarec_tick));
    if (h.count > SAREC_MAX_TICKS || (unsigned long long)size != sizeof h + (unsigned long long)h.count * sizeof(sarec_tick))
        return fail(r, f, err, cap, "file size %ld does not match %u ticks", size, h.count);
    if (h.flags & ~SAREC_FLAGS_KNOWN) return fail(r, f, err, cap, "unknown flags 0x%x", h.flags);
    if (h.game[sizeof h.game - 1] || h.setup[sizeof h.setup - 1]) return fail(r, f, err, cap, "unterminated header string");
    r->ticks = malloc((h.count ? h.count : 1) * sizeof *r->ticks);
    if (!r->ticks) return fail(r, f, err, cap, "out of memory");
    if (h.count && fread(r->ticks, sizeof *r->ticks, h.count, f) != h.count) return fail(r, f, err, cap, "read failed");
    fclose(f);
    r->count = r->cap = h.count; r->version = 2; r->flags = h.flags;
    memcpy(r->game, h.game, sizeof r->game);
    memcpy(r->setup, h.setup, sizeof r->setup);
    return 1;
}
int sarec_load(sarec *r, const char *path) { return sarec_load_err(r, path, NULL, 0); }

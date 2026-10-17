#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*ar_texture_upload_fn)(void *user, uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba);
typedef struct ar_texture_fifo ar_texture_fifo;
ar_texture_fifo *ar_texfifo_create(void);
void ar_texfifo_destroy(ar_texture_fifo *f);
void ar_texfifo_push(ar_texture_fifo *f, uint64_t seq, uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba);
uint32_t ar_texfifo_drain(ar_texture_fifo *f, uint64_t up_to_seq, ar_texture_upload_fn fn, void *user);
uint32_t ar_texfifo_pending(ar_texture_fifo *f);
void ar_texfifo_stats(ar_texture_fifo *f, uint64_t *pushed_bytes, uint64_t *peak_pending_bytes);
#ifdef __cplusplus
}
#endif

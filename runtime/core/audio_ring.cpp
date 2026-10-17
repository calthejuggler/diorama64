#include "ar_audio_ring.h"
#include <atomic>
#include <mutex>
#include <vector>
struct ar_audio_ring { std::mutex m; std::vector<int16_t> buf; uint32_t cap = 0, head = 0, size = 0; std::atomic<uint64_t> contended{0}; };
extern "C" {
ar_audio_ring *ar_audio_ring_create(uint32_t cap) {
    auto *r = new ar_audio_ring();
    r->cap = cap ? cap : 1;
    r->buf.assign((size_t)r->cap * 2, 0);
    return r;
}
void ar_audio_ring_destroy(ar_audio_ring *r) { delete r; }
uint32_t ar_audio_ring_write(ar_audio_ring *r, const int16_t *s, uint32_t n) {
    std::lock_guard<std::mutex> g(r->m);
    for (uint32_t i = 0; i < n; i++) {
        if (r->size == r->cap) { r->head = (r->head + 1) % r->cap; r->size--; }
        const uint32_t idx = (r->head + r->size) % r->cap;
        r->buf[(size_t)idx * 2] = s[i * 2]; r->buf[(size_t)idx * 2 + 1] = s[i * 2 + 1];
        r->size++;
    }
    return n;
}
uint32_t ar_audio_ring_read(ar_audio_ring *r, int16_t *s, uint32_t n) {
    std::unique_lock<std::mutex> g(r->m, std::try_to_lock);   // the reader may be a real-time thread: never wait
    if (!g.owns_lock()) { r->contended.fetch_add(1); return 0; }
    const uint32_t k = n < r->size ? n : r->size;
    for (uint32_t i = 0; i < k; i++) {
        const uint32_t idx = (r->head + i) % r->cap;
        s[i * 2] = r->buf[(size_t)idx * 2]; s[i * 2 + 1] = r->buf[(size_t)idx * 2 + 1];
    }
    r->head = (r->head + k) % r->cap; r->size -= k;
    return k;
}
uint64_t ar_audio_ring_contended(ar_audio_ring *r) { return r->contended.load(); }
uint32_t ar_audio_ring_available(ar_audio_ring *r) { std::lock_guard<std::mutex> g(r->m); return r->size; }
}

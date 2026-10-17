#include "ar_scene_ring.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
namespace { constexpr uint32_t L = AR_AUDIO_MAX_SOURCES; }
struct ar_scene_ring {
    std::mutex m;
    bool lanes = false;
    uint32_t cap = 1, head = 0, size = 0;
    uint64_t head_abs = 0;
    std::vector<int16_t> bed, lane;
    std::vector<ar_audio_tick_meta> meta;
    uint32_t mcap = 1, mhead = 0, msize = 0;
    std::atomic<uint64_t> contended{0}, dropped{0}, meta_dropped{0};
    const ar_audio_tick_meta &meta_at(uint32_t i) const { return meta[(mhead + i) % mcap]; }
    void meta_pop() { mhead = (mhead + 1) % mcap; msize--; }
};
extern "C" {
ar_scene_ring *ar_scene_ring_create(int with_lanes, uint32_t capacity, uint32_t meta_capacity) {
    auto *r = new ar_scene_ring();
    r->lanes = with_lanes != 0;
    r->cap = capacity ? capacity : 1;
    r->bed.assign((size_t)r->cap * 2, 0);
    if (r->lanes) r->lane.assign((size_t)r->cap * L, 0);
    r->mcap = meta_capacity ? meta_capacity : 1;
    r->meta.assign(r->mcap, ar_audio_tick_meta{});
    return r;
}
void ar_scene_ring_destroy(ar_scene_ring *r) { delete r; }
uint64_t ar_scene_ring_write(ar_scene_ring *r, const int16_t *bed, const int16_t *lanes, uint32_t n, const ar_audio_tick_meta *m) {
    std::lock_guard<std::mutex> g(r->m);
    const uint64_t first = r->head_abs + r->size;
    for (uint32_t i = 0; i < n; i++) {
        if (r->size == r->cap) { r->head = (r->head + 1) % r->cap; r->size--; r->head_abs++; r->dropped.fetch_add(1); }
        const size_t at = (r->head + r->size) % r->cap;
        r->bed[at * 2] = bed ? bed[(size_t)i * 2] : 0;
        r->bed[at * 2 + 1] = bed ? bed[(size_t)i * 2 + 1] : 0;
        if (r->lanes) {
            if (lanes) std::memcpy(&r->lane[at * L], &lanes[(size_t)i * L], sizeof(int16_t) * L);
            else std::memset(&r->lane[at * L], 0, sizeof(int16_t) * L);
        }
        r->size++;
    }
    if (m && n) {
        if (r->msize == r->mcap) { r->meta_pop(); r->meta_dropped.fetch_add(1); }
        ar_audio_tick_meta c = *m;
        c.first_frame = first;
        c.frames = n;
        r->meta[(r->mhead + r->msize) % r->mcap] = c;
        r->msize++;
    }
    while (r->msize && r->meta_at(0).first_frame < r->head_abs) { r->meta_pop(); r->meta_dropped.fetch_add(1); }
    return first;
}
uint32_t ar_scene_ring_read(ar_scene_ring *r, ar_audio_pull *p) {
    p->meta_count = 0;
    std::unique_lock<std::mutex> g(r->m, std::try_to_lock);   // the consumer may be a real-time thread: never wait
    if (!g.owns_lock()) { r->contended.fetch_add(1); p->first_frame = 0; return 0; }
    p->first_frame = r->head_abs;
    uint32_t k = p->frames < r->size ? p->frames : r->size;
    const bool keep = p->meta != nullptr && p->meta_cap > 0;
    if (keep) {
        uint32_t in = 0;
        for (uint32_t i = 0; i < r->msize; i++) {
            const uint64_t f = r->meta_at(i).first_frame;
            if (f >= r->head_abs + k) break;
            if (in == p->meta_cap) { k = (uint32_t)(f - r->head_abs); break; }
            in++;
        }
    }
    for (uint32_t i = 0; i < k; i++) {
        const size_t at = (r->head + i) % r->cap;
        if (p->bed) { p->bed[(size_t)i * 2] = r->bed[at * 2]; p->bed[(size_t)i * 2 + 1] = r->bed[at * 2 + 1]; }
        if (p->lanes) {
            if (r->lanes) std::memcpy(&p->lanes[(size_t)i * L], &r->lane[at * L], sizeof(int16_t) * L);
            else std::memset(&p->lanes[(size_t)i * L], 0, sizeof(int16_t) * L);
        }
    }
    r->head = (r->head + k) % r->cap;
    r->size -= k;
    r->head_abs += k;
    while (r->msize && r->meta_at(0).first_frame < r->head_abs) {
        if (keep && p->meta_count < p->meta_cap) p->meta[p->meta_count++] = r->meta_at(0);
        r->meta_pop();
    }
    return k;
}
uint64_t ar_scene_ring_contended(ar_scene_ring *r) { return r->contended.load(); }
uint64_t ar_scene_ring_dropped(ar_scene_ring *r) { return r->dropped.load(); }
uint64_t ar_scene_ring_meta_dropped(ar_scene_ring *r) { return r->meta_dropped.load(); }
uint32_t ar_scene_ring_available(ar_scene_ring *r) { std::lock_guard<std::mutex> g(r->m); return r->size; }
}

#include "ar_snapshot_ring.h"
#include <mutex>
namespace { constexpr int kSlots = 4; }
struct ar_snapshot_ring {
    std::mutex m;
    fc_builder *b[kSlots] = {};
    const fc_frame *frame[kSlots] = {};
    int refs[kSlots] = {};
    int writing = -1, latest = -1;
    uint32_t dropped = 0;
};
extern "C" {
ar_snapshot_ring *ar_ring_create(void) {
    auto *r = new ar_snapshot_ring();
    for (auto &b : r->b) b = fc_builder_create();
    return r;
}
void ar_ring_destroy(ar_snapshot_ring *r) {
    if (!r) return;
    for (auto &b : r->b) fc_builder_destroy(b);
    delete r;
}
fc_builder *ar_ring_begin_write(ar_snapshot_ring *r) {
    std::lock_guard<std::mutex> g(r->m);
    if (r->writing >= 0) return r->b[r->writing];
    for (int i = 0; i < kSlots; i++) {
        if (i != r->latest && r->refs[i] == 0) { r->writing = i; r->frame[i] = nullptr; return r->b[i]; }
    }
    r->dropped++;
    return nullptr;
}
void ar_ring_publish(ar_snapshot_ring *r) {
    std::lock_guard<std::mutex> g(r->m);
    if (r->writing < 0) return;
    r->frame[r->writing] = fc_builder_finish(r->b[r->writing]);
    r->latest = r->writing;
    r->writing = -1;
}
const fc_frame *ar_ring_acquire_latest(ar_snapshot_ring *r) {
    std::lock_guard<std::mutex> g(r->m);
    if (r->latest < 0) return nullptr;
    r->refs[r->latest]++;
    return r->frame[r->latest];
}
void ar_ring_release(ar_snapshot_ring *r, const fc_frame *f) {
    if (!f) return;
    std::lock_guard<std::mutex> g(r->m);
    for (int i = 0; i < kSlots; i++) {
        if (r->frame[i] == f && r->refs[i] > 0) { r->refs[i]--; return; }
    }
}
uint32_t ar_ring_dropped(ar_snapshot_ring *r) { std::lock_guard<std::mutex> g(r->m); return r->dropped; }
}

#include "ar_texture_fifo.h"
#include <deque>
#include <mutex>
#include <vector>
namespace { struct Upload { uint64_t seq; uint32_t id, w, h; std::vector<uint8_t> rgba; }; }
struct ar_texture_fifo { std::mutex m; std::deque<Upload> q; uint64_t pushed = 0, pending = 0, peak = 0; };
extern "C" {
ar_texture_fifo *ar_texfifo_create(void) { return new ar_texture_fifo(); }
void ar_texfifo_destroy(ar_texture_fifo *f) { delete f; }
void ar_texfifo_push(ar_texture_fifo *f, uint64_t seq, uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba) {
    Upload u{seq, id, w, h, std::vector<uint8_t>(rgba, rgba + (size_t)w * h * 4)};
    std::lock_guard<std::mutex> g(f->m);
    const uint64_t bytes = (uint64_t)w * h * 4;
    f->q.push_back(std::move(u));
    f->pushed += bytes; f->pending += bytes;
    if (f->pending > f->peak) f->peak = f->pending;
}
uint32_t ar_texfifo_drain(ar_texture_fifo *f, uint64_t up_to, ar_texture_upload_fn fn, void *user) {
    std::deque<Upload> out;
    {
        std::lock_guard<std::mutex> g(f->m);
        while (!f->q.empty() && f->q.front().seq <= up_to) {
            f->pending -= (uint64_t)f->q.front().w * f->q.front().h * 4;
            out.push_back(std::move(f->q.front())); f->q.pop_front();
        }
    }
    for (auto &u : out) fn(user, u.id, u.w, u.h, u.rgba.data());   // outside the lock: fn may be slow (GPU upload)
    return (uint32_t)out.size();
}
void ar_texfifo_stats(ar_texture_fifo *f, uint64_t *pushed_bytes, uint64_t *peak_pending_bytes) {
    std::lock_guard<std::mutex> g(f->m);
    if (pushed_bytes) *pushed_bytes = f->pushed;
    if (peak_pending_bytes) *peak_pending_bytes = f->peak;
}
uint32_t ar_texfifo_pending(ar_texture_fifo *f) { std::lock_guard<std::mutex> g(f->m); return (uint32_t)f->q.size(); }
}

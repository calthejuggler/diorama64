#include "vos_state.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>
namespace ar {
ar_session_state vos_session_state(bool running, bool table_valid, bool holding, double untracked_s, uint64_t frames,
                                   bool pad_lost) {
    if (!running) return AR_SESSION_IDLE;
    if (untracked_s >= kTrackingLostPauseS) return AR_SESSION_LOST;
    if (!table_valid || holding || frames < kResumeSettleFrames) return AR_SESSION_VISIBLE;
    if (pad_lost) return AR_SESSION_VISIBLE;
    return AR_SESSION_FOCUSED;
}
void PadWatch::update(bool c, double t) {
    if (c) { ever_ = true; lost_ = false; gone_since_ = -1.0; return; }
    if (!ever_) return;
    if (gone_since_ < 0.0) gone_since_ = t;
    if (t - gone_since_ >= kPadLostS) lost_ = true;
}
bool vos_wait_while_paused(bool (*is_paused)(void *), void *ctx, const std::atomic<bool> &closed, double poll_s) {
    while (is_paused(ctx)) {
        if (closed) return false;
        std::this_thread::sleep_for(std::chrono::duration<double>(poll_s));
    }
    return true;
}
uint64_t HoldSchedule::limit() const { return next_ < ticks_.size() ? ticks_[next_] : std::numeric_limits<uint64_t>::max(); }
uint32_t vos_step_due_ticks(ar_runtime *rt, uint64_t limit, void (*on_tick)(void *, uint64_t), void *ctx) {
    uint32_t n = 0;
    while (ar_runtime_ticks_due(rt) && ar_runtime_ticks_done(rt) < limit) {
        ar_runtime_step_game(rt);
        n++;
        if (on_tick) on_tick(ctx, ar_runtime_ticks_done(rt));
    }
    return n;
}
bool vos_submit_if_empty(ar_runtime *rt, const ar_render_frame &rf, uint32_t *w, uint32_t *h) {
    *w = 0; *h = 0;
    for (uint32_t i = 0; i < rf.view_count; i++) { *w += rf.views[i].width; *h = std::max(*h, rf.views[i].height); }
    if (*w && *h) return false;
    *w = 0; *h = 0;
    ar_render_frame none = rf;
    none.view_count = 0;
    ar_runtime_submit_frame(rt, &none);
    ar_runtime_end_frame(rt, &rf);
    return true;
}
}

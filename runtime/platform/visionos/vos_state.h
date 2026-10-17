#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
extern "C" {
#include "ar_platform.h"
#include "ar_runtime.h"
}
namespace ar {
constexpr double kTrackingLostPauseS = 1.0;
constexpr uint64_t kResumeSettleFrames = 3;
constexpr double kPadLostS = 0.5;
// untracked_s: 0 if the head was never tracked (the simulator may never track).
ar_session_state vos_session_state(bool layer_running, bool table_valid, bool holding, double untracked_s,
                                   uint64_t frames_since_attach, bool pad_lost = false);
// A pad never connected (the simulator, .sarec replays) is never lost. After placing with A, A is usually still held:
// arm(mask) keeps those buttons from the game until a pad shows them all released.
class HeldButtonGate {
public:
    void arm(uint32_t mask) { mask_ = mask; }
    void filter(ar_pad *p) {
        if (!mask_) return;
        if ((p->buttons & mask_) == 0) { mask_ = 0; return; }
        p->buttons &= ~mask_;
    }
private:
    uint32_t mask_ = 0;
};
class PadWatch {
public:
    void update(bool connected, double t_s);
    bool lost() const { return lost_; }
private:
    bool ever_ = false, lost_ = false;
    double gone_since_ = -1.0;
};
// Never past the space closing: cp_layer_renderer_wait_until_running would wait on a dismissed layer forever while
// holding the one-layer-loop lock.
bool vos_wait_while_paused(bool (*is_paused)(void *ctx), void *ctx, const std::atomic<bool> &closed, double poll_s);
class HoldSchedule {
public:
    explicit HoldSchedule(std::vector<uint64_t> ticks) : ticks_(std::move(ticks)) {}
    uint64_t limit() const;
    bool holding(uint64_t ticks_done) const { return next_ < ticks_.size() && ticks_done >= ticks_[next_]; }
    uint64_t current() const { return limit(); }
    void release() { if (next_ < ticks_.size()) next_++; }
private:
    std::vector<uint64_t> ticks_;
    size_t next_ = 0;
};
// Never past limit: a frame granted more ticks than the pending hold allows stops exactly at the hold; the rest stay
// due.
uint32_t vos_step_due_ticks(ar_runtime *rt, uint64_t limit, void (*on_tick)(void *ctx, uint64_t ticks_done), void *ctx);
// Every begun frame is submitted (ar_runtime.h): an empty one with no views.
bool vos_submit_if_empty(ar_runtime *rt, const ar_render_frame &rf, uint32_t *w, uint32_t *h);
}

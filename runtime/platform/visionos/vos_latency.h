// Each pull first drains the ring into its own queue, so the queue's level is exactly the game audio waiting; delay
// above the standing target (a contended read, a late tick) is trimmed.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
namespace ar {
// Standing 800 frames = 25 ms of slack, so a pull sees at most ~58 ms queued. The 75 ms window spans a tick burst's
// trough even with a held-back tick, so bursts alone never trim.
constexpr uint32_t kAudioStandingFrames = 800, kAudioWindowFrames = 2400, kAudioQueueFrames = 16384;
class AudioLatencyBound {
public:
    using Source = uint32_t (*)(void *user, int16_t *stereo, uint32_t frames);
    AudioLatencyBound(uint32_t capacity, uint32_t standing, uint32_t window, Source upstream, void *user);
    // One consumer thread.
    static uint32_t pull(void *self, int16_t *stereo, uint32_t frames);
    uint64_t trimmed() const { return trimmed_.load(); }
private:
    uint32_t read(int16_t *stereo, uint32_t frames);
    void fill();
    void drop(uint32_t frames);
    std::vector<int16_t> buf_;
    uint32_t cap_, standing_, window_;
    Source src_;
    void *user_;
    uint32_t head_ = 0, size_ = 0;
    uint32_t win_min_ = UINT32_MAX, win_req_ = 0;
    std::atomic<uint64_t> trimmed_{0};
};
}

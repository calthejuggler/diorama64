#include "vos_latency.h"
#include <algorithm>
#include <cstring>
namespace ar {
AudioLatencyBound::AudioLatencyBound(uint32_t capacity, uint32_t standing, uint32_t window, Source upstream, void *user)
    : buf_(2 * (size_t)std::max<uint32_t>(capacity, 1), 0), cap_(std::max<uint32_t>(capacity, 1)),
      standing_(std::min(standing, cap_)), window_(window), src_(upstream), user_(user) {}

uint32_t AudioLatencyBound::pull(void *self, int16_t *stereo, uint32_t frames) {
    return static_cast<AudioLatencyBound *>(self)->read(stereo, frames);
}

void AudioLatencyBound::drop(uint32_t n) {
    n = std::min(n, size_);
    head_ = (head_ + n) % cap_;
    size_ -= n;
    trimmed_.fetch_add(n);
}

void AudioLatencyBound::fill() {
    for (int rounds = 0; rounds < 4; rounds++) {
        while (size_ < cap_) {
            const uint32_t tail = (head_ + size_) % cap_;
            const uint32_t chunk = std::min(cap_ - size_, cap_ - tail);
            const uint32_t got = src_ ? src_(user_, &buf_[2 * (size_t)tail], chunk) : 0;
            size_ += got;
            if (got < chunk) return;
        }
        drop(size_ - standing_);
    }
}

uint32_t AudioLatencyBound::read(int16_t *out, uint32_t n) {
    fill();
    const uint32_t k = std::min(n, size_);
    for (uint32_t done = 0; done < k;) {
        const uint32_t run = std::min(k - done, cap_ - head_);
        std::memcpy(out + 2 * (size_t)done, &buf_[2 * (size_t)head_], sizeof(int16_t) * 2 * run);
        head_ = (head_ + run) % cap_;
        size_ -= run;
        done += run;
    }
    if (window_) {
        win_min_ = std::min(win_min_, size_);
        win_req_ += n;
        if (win_req_ >= window_) {
            if (win_min_ > standing_) drop(win_min_ - standing_);
            win_min_ = UINT32_MAX;
            win_req_ = 0;
        }
    }
    return k;
}
}

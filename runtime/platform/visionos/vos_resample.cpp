#include "vos_resample.h"
#include <cmath>
#include <cstring>
namespace ar {
StereoResampler::StereoResampler(uint32_t in_hz, uint32_t out_hz, uint32_t max_out)
    : step_((double)in_hz / (double)out_hz), max_out_(max_out) {
    buf_.assign(2 * ((size_t)std::ceil(max_out * step_) + 4), 0);
}
uint32_t StereoResampler::render(float *out, uint32_t n, Source src, void *user) {
    if (n > max_out_) n = max_out_;
    if (n == 0) return 0;
    const double last = pos_ + (double)(n - 1) * step_;
    const uint32_t need = (uint32_t)std::floor(last) + 2;
    uint32_t missing = 0;
    if (need > have_) {
        const uint32_t want = need - have_;
        const uint32_t got = src ? src(user, &buf_[2 * (size_t)have_], want) : 0;
        if (got < want) {
            std::memset(&buf_[2 * ((size_t)have_ + got)], 0, sizeof(int16_t) * 2 * (want - got));
            missing = want - got;
        }
        have_ = need;
    }
    for (uint32_t j = 0; j < n; j++) {
        const double t = pos_ + (double)j * step_;
        const uint32_t k = (uint32_t)std::floor(t);
        const float f = (float)(t - (double)k);
        for (int ch = 0; ch < 2; ch++) {
            const float a = buf_[2 * (size_t)k + ch] / 32768.f, b = buf_[2 * ((size_t)k + 1) + ch] / 32768.f;
            out[2 * j + ch] = a + (b - a) * f;
        }
    }
    const double end = pos_ + (double)n * step_;
    const uint32_t used = (uint32_t)std::floor(end);              // <= have_ - 1 because in_hz < out_hz (step_ < 1)
    std::memmove(buf_.data(), &buf_[2 * (size_t)used], sizeof(int16_t) * 2 * (have_ - used));
    have_ -= used;
    pos_ = end - (double)used;
    return missing;
}
}

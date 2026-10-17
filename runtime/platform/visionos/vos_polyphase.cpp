// runtime/platform/visionos/vos_polyphase.cpp — see vos_polyphase.h. Output time m (1/L input frames) plays
// sum_k h[p + L k] x[b - k] with b = m / L, p = m % L (the prototype h at L x the input rate, centred at (K L - 1) / 2).
// buf_ keeps K - 1 frames of history in front of the input, so x[b - k] is buf_[b + (K - 1 - k)] and the coefficients
// are stored reversed: one forward dot product per output.
#include "vos_polyphase.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
namespace ar {
namespace {
double bessel_i0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 100; k++) {
        t *= (x / (2.0 * k)) * (x / (2.0 * k));
        s += t;
        if (t < 1e-12 * s) break;
    }
    return s;
}
}
Polyphase::Polyphase(uint32_t in_hz, uint32_t out_hz, uint32_t channels, uint32_t max_out) {
    if (channels == 0 || channels > 64) return;
    ch_ = channels;                                       // set first: a refused rate still renders silence on every channel
    if (in_hz == 0 || out_hz <= in_hz || max_out == 0) return;
    const uint32_t g = std::gcd(in_hz, out_hz);
    L_ = out_hz / g;
    M_ = in_hz / g;
    if (L_ > 16) { L_ = M_ = 1; return; }
    const uint32_t K = kTaps, N = K * L_;
    const double fc = kCutoffHz / ((double)in_hz * L_);
    const double c = 0.5 * (N - 1), i0b = bessel_i0(kBeta);
    std::vector<double> h(N);
    for (uint32_t m = 0; m < N; m++) {
        const double x = m - c, r = x / c;
        const double sinc = x == 0.0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
        h[m] = sinc * bessel_i0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
    }
    coef_.assign((size_t)L_ * K, 0.f);
    for (uint32_t p = 0; p < L_; p++) {
        double sum = 0.0;
        for (uint32_t k = 0; k < K; k++) sum += h[p + (size_t)L_ * k];
        for (uint32_t k = 0; k < K; k++) coef_[(size_t)p * K + k] = (float)(h[p + (size_t)L_ * (K - 1 - k)] / sum);
    }
    cap_ = K + (uint32_t)(((uint64_t)max_out * M_ + L_ - 1) / L_) + 2;
    buf_.assign((size_t)ch_ * cap_, 0.f);
    zrun_.assign(ch_, 0);
    ok_ = true;
    reset();
}
void Polyphase::reset() {
    if (!ok_) return;
    std::fill(buf_.begin(), buf_.end(), 0.f);
    have_ = kTaps - 1;
    std::fill(zrun_.begin(), zrun_.end(), have_);
    t_ = 0;
    consumed_ = 0;
}
uint32_t Polyphase::need(uint32_t n) const {
    if (!ok_ || n == 0) return 0;
    const uint64_t last = (t_ + (uint64_t)(n - 1) * M_) / L_ + kTaps;
    return last > have_ ? (uint32_t)(last - have_) : 0;
}
void Polyphase::push(const int16_t *frames, uint32_t n, uint32_t stride) {
    if (!ok_) return;
    n = std::min(n, cap_ - have_);
    for (uint32_t c = 0; c < ch_; c++) {
        float *x = &buf_[(size_t)c * cap_ + have_];
        uint32_t z = zrun_[c];
        for (uint32_t i = 0; i < n; i++) {
            const int16_t v = frames[(size_t)i * stride + c];
            x[i] = (float)v / 32768.f;
            z = v ? 0 : (z < cap_ ? z + 1 : z);
        }
        zrun_[c] = z;
    }
    have_ += n;
}
void Polyphase::render(uint32_t n, PolyMode mode, float *out, uint32_t stride, uint8_t *silent) {
    const uint32_t K = kTaps;
    if (!ok_) {
        for (uint32_t c = 0; c < ch_; c++) { std::memset(out + (size_t)c * stride, 0, sizeof(float) * n); if (silent) silent[c] = 1; }
        return;
    }
    const uint32_t first = (uint32_t)(t_ / L_);
    const double lin_off = (double)(K - 1) - delay();
    for (uint32_t c = 0; c < ch_; c++) {
        float *o = out + (size_t)c * stride;
        const float *x = &buf_[(size_t)c * cap_];
        if (zrun_[c] >= have_ - first) {
            std::memset(o, 0, sizeof(float) * n);
            if (silent) silent[c] = 1;
            continue;
        }
        if (silent) silent[c] = 0;
        uint64_t t = t_;
        for (uint32_t j = 0; j < n; j++, t += M_) {
            const uint32_t b = (uint32_t)(t / L_);
            if (mode == PolyMode::sinc) {
                const float *h = &coef_[(size_t)(t % L_) * K];
                const float *xx = x + b;
                float acc = 0.f;
                for (uint32_t k = 0; k < K; k++) acc += h[k] * xx[k];
                o[j] = acc;
            } else {
                const double pos = (double)t / L_ + lin_off;
                const uint32_t i = (uint32_t)pos;
                const float f = (float)(pos - (double)i);
                o[j] = x[i] + (x[i + 1] - x[i]) * f;
            }
        }
    }
    t_ += (uint64_t)n * M_;
    const uint32_t drop = (uint32_t)(t_ / L_);
    if (drop) {
        for (uint32_t c = 0; c < ch_; c++) {
            float *x = &buf_[(size_t)c * cap_];
            std::memmove(x, x + drop, sizeof(float) * (have_ - drop));
        }
        have_ -= drop;
        t_ -= (uint64_t)drop * L_;
        consumed_ += drop;
    }
}
}

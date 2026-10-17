#include "vos_perf.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace ar {
void PerfWindow::frame(int64_t ns, double cpu_ms, uint32_t k, double gpu_ms) {
    t_.push_back(ns);
    if (gpu_ms >= 0.0) gpu_.push_back(gpu_ms);
    cpu_.push_back(cpu_ms);
    tpf_[k > 3 ? 3 : k]++;
    ticks_ += k;
    since_++;
    if (k > 0) {
        if (seen_tick_) run_[since_ > 6 ? 5 : since_ - 1]++;
        seen_tick_ = true;
        since_ = 0;
    }
}
void PerfWindow::reset() { *this = PerfWindow(); }
PerfReport PerfWindow::report() const {
    PerfReport r;
    r.frames = t_.size();
    r.ticks = ticks_;
    std::copy(tpf_, tpf_ + 4, r.ticks_per_frame);
    std::copy(run_, run_ + 6, r.run_len);
    if (r.frames == 0) return r;
    if (r.frames > 1 && t_.back() > t_.front()) r.hz = double(r.frames - 1) * 1e9 / double(t_.back() - t_.front());
    auto pct = [](const std::vector<double> &c, double q) { return c[std::min(c.size() - 1, (size_t)std::floor(q * double(c.size() - 1) + 0.5))]; };
    std::vector<double> c = cpu_;
    std::sort(c.begin(), c.end());
    r.cpu_p50_ms = pct(c, 0.5); r.cpu_p95_ms = pct(c, 0.95); r.cpu_max_ms = c.back();
    if (!gpu_.empty()) {
        std::vector<double> g = gpu_;
        std::sort(g.begin(), g.end());
        r.gpu_frames = g.size();
        r.gpu_p50_ms = pct(g, 0.5); r.gpu_p95_ms = pct(g, 0.95); r.gpu_max_ms = g.back();
    }
    if (r.frames > 2) {
        std::vector<int64_t> d;
        for (size_t i = 1; i < t_.size(); i++) d.push_back(t_[i] - t_[i - 1]);
        std::vector<int64_t> s = d;
        std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end());
        const double med = double(s[s.size() / 2]);
        for (int64_t x : d) r.hitches += double(x) > 1.5 * med;
        r.max_gap_ms = double(*std::max_element(d.begin(), d.end())) * 1e-6;
    }
    return r;
}
std::string perf_line(const PerfReport &r) {
    char b[416];
    std::snprintf(b, sizeof b, "AR_PERF frames=%llu hz=%.1f ticks=%llu tpf=%u/%u/%u/%u run=1:%u,2:%u,3:%u,4:%u,5:%u,6+:%u "
                  "cpu_ms p50=%.2f p95=%.2f max=%.2f hitches=%u max_gap_ms=%.1f", (unsigned long long)r.frames, r.hz, (unsigned long long)r.ticks,
                  r.ticks_per_frame[0], r.ticks_per_frame[1], r.ticks_per_frame[2], r.ticks_per_frame[3], r.run_len[0], r.run_len[1],
                  r.run_len[2], r.run_len[3], r.run_len[4], r.run_len[5], r.cpu_p50_ms, r.cpu_p95_ms, r.cpu_max_ms, r.hitches,
                  r.max_gap_ms);
    std::string s = b;
    if (r.gpu_frames == 0) return s + " gpu_ms n/a";
    std::snprintf(b, sizeof b, " gpu_ms p50=%.2f p95=%.2f max=%.2f", r.gpu_p50_ms, r.gpu_p95_ms, r.gpu_max_ms);
    return s + b;
}
}

#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ar {
struct PerfReport {
    uint64_t frames = 0, ticks = 0;
    double hz = 0.0;
    uint32_t ticks_per_frame[4] = {};
    uint32_t run_len[6] = {};
    double cpu_p50_ms = 0.0, cpu_p95_ms = 0.0, cpu_max_ms = 0.0;
    uint32_t hitches = 0;
    uint64_t gpu_frames = 0;
    double gpu_p50_ms = 0.0, gpu_p95_ms = 0.0, gpu_max_ms = 0.0;
    double max_gap_ms = 0.0;
};
class PerfWindow {
public:
    void frame(int64_t display_ns, double cpu_ms, uint32_t ticks_stepped, double gpu_ms = -1.0);
    PerfReport report() const;
    void reset();
    uint64_t frames() const { return t_.size(); }
private:
    std::vector<int64_t> t_;
    std::vector<double> cpu_, gpu_;
    uint32_t tpf_[4] = {}, run_[6] = {};
    uint32_t since_ = 0;
    bool seen_tick_ = false;
    uint64_t ticks_ = 0;
};
std::string perf_line(const PerfReport &r);
}

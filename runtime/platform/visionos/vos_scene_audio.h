// Each output cycle is rendered once for all voices: PHASE calls one render block per voice, and the first call of a
// new cycle key renders every channel into a staging block the others copy from. Real-time safe after construction.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
extern "C" {
#include "ar_audio_types.h"
}
#include "vos_latency.h"
#include "vos_polyphase.h"
namespace ar {
constexpr uint32_t kSceneChannels = 2 + AR_AUDIO_MAX_SOURCES;
void mix_lanes_into_bed(int16_t *bed, const int16_t *lanes, uint32_t frames, uint32_t lane_count);
class SceneFeed {
public:
    using Source = uint32_t (*)(void *user, ar_audio_pull *p);
    static constexpr uint32_t kMetaQueue = 256;
    struct Config {
        // The resampler only upsamples: in_hz must be below out_hz (else every render is silence, counted as invalid).
        uint32_t in_hz = AR_AUDIO_RATE_HZ, out_hz = 48000, max_block = 4096;
        uint32_t queue = kAudioQueueFrames, standing = kAudioStandingFrames, window = kAudioWindowFrames;
        float tap_seconds = 0.f;
        bool polyphase = false;
        // Test seam only (no app sets it).
        uint32_t read_chunk = 0;
    };
    SceneFeed(const Config &c, Source src, void *user);
    static constexpr uint64_t kNoCycle = UINT64_MAX;
    // Keys only move forward: the first call with a newer key renders all channels; a call with the current or an older
    // key copies from the current cycle and consumes nothing.
    bool render(uint64_t key, uint32_t frames, uint32_t ch, float *out, bool *silent = nullptr);
    // Any thread. The next cycle ramps per sample from the gain the last one ended on (no zipper noise); snap starts it
    // at this gain.
    void set_lane_gain(uint32_t lane, float gain, bool snap = false);
    void set_bed_gain(float gain);
    uint64_t heard_frame() const { return heard_.load(std::memory_order_acquire); }
    void set_poly_mode(PolyMode m) { poly_mode_.store((uint8_t)m, std::memory_order_relaxed); }
    PolyMode poly_mode() const { return (PolyMode)poly_mode_.load(std::memory_order_relaxed); }
    bool poly_active() const { return c_.polyphase && poly_.ok(); }
    double delay_frames() const { return poly_active() ? poly_.delay() : 0.0; }
    bool pop_meta(ar_audio_tick_meta *out);
    uint64_t cycles() const { return cycles_.load(); }
    uint64_t missing() const { return missing_.load(); }
    uint64_t trimmed() const { return trimmed_.load(); }
    uint64_t busy() const { return busy_.load(); }
    uint64_t mismatched() const { return mismatched_.load(); }
    uint64_t stale() const { return stale_.load(); }
    uint64_t invalid() const { return invalid_.load(); }
    uint64_t meta_lost() const { return meta_lost_.load(); }
    // The tap: both on the layer thread only. write_tap writes once, and only after tap_ready() (the render thread is then
    // done with the buffer); before that it returns false.
    bool tap_ready() const;
    bool write_tap(const std::string &path);
private:
    void fill();
    void drop(uint32_t n);
    uint32_t read(int16_t *frames, uint32_t n, uint64_t *first_abs);
    void cycle(uint32_t n);
    void cycle_poly(uint32_t n);
    void begin_gains(uint32_t n, float *g0, float *dg);
    void finish_cycle(const float *st, uint32_t n, uint32_t s);
    Config c_;
    Source src_;
    void *user_;
    bool rates_ok_;
    std::vector<int16_t> q_;
    uint32_t qhead_ = 0, qsize_ = 0, win_min_ = UINT32_MAX, win_req_ = 0;
    uint64_t qabs_ = 0;
    std::vector<int16_t> pull_bed_, pull_lanes_;
    std::vector<ar_audio_tick_meta> pull_meta_, mq_;
    std::atomic<uint32_t> mw_{0}, mr_{0};
    double step_, pos_ = 0.0;
    std::vector<int16_t> rbuf_;
    uint32_t have_ = 1;
    uint64_t rabs_ = 0;
    bool rabs_known_ = false;
    std::vector<float> staging_[2];
    uint8_t silent_[2][kSceneChannels];
    // The current cycle: (frames << 1) | staging slot, one atomic so the frame check and the slot always match. Each staging
    // block has a seqlock count (odd while cycle() writes it): a copy that raced a rewrite (a voice stalled for two cycles)
    // is detected and becomes silence.
    std::atomic<uint64_t> pub_{0};
    std::atomic<uint32_t> seq_[2] = {{0}, {0}};
    std::atomic<float> lane_gain_[AR_AUDIO_MAX_SOURCES];
    float lane_gain_now_[AR_AUDIO_MAX_SOURCES];                 // the rendering thread's: each lane's gain at the end of the last cycle
    std::atomic<bool> lane_gain_snap_[AR_AUDIO_MAX_SOURCES];
    bool lane_gain_started_ = false;
    std::atomic<float> bed_gain_{1.f};
    float bed_gain_now_ = 1.f;
    std::atomic<uint64_t> key_{kNoCycle};
    std::atomic_flag rendering_ = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> heard_{0}, cycles_{0}, missing_{0}, trimmed_{0}, busy_{0}, mismatched_{0}, stale_{0}, invalid_{0},
        meta_lost_{0};
    std::vector<int16_t> tap_;
    std::atomic<size_t> tap_pos_{0};
    bool tap_written_ = false;
    Polyphase poly_;
    std::atomic<uint8_t> poly_mode_{0};
    uint64_t poly_pushed_ = 0;
    int64_t poly_abs0_ = 0;
    bool poly_abs_known_ = false;
};
struct FeedChoice { bool polyphase; PolyMode mode; };
inline FeedChoice feed_choice(int v) {
    return v == 0 ? FeedChoice{true, PolyMode::sinc} : v == 1 ? FeedChoice{true, PolyMode::linear} : FeedChoice{false, PolyMode::sinc};
}
}

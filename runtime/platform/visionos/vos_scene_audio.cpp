#include "vos_scene_audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "wav_io.h"
namespace ar {
void mix_lanes_into_bed(int16_t *bed, const int16_t *lanes, uint32_t frames, uint32_t lane_count) {
    for (uint32_t f = 0; f < frames; f++) {
        float sum = 0.f;
        for (uint32_t l = 0; l < lane_count; l++) sum += (float)lanes[(size_t)f * lane_count + l];
        const float add = sum * 0.70710678f;
        for (uint32_t c = 0; c < 2; c++) {
            const float v = std::nearbyint((float)bed[2 * (size_t)f + c] + add);
            bed[2 * (size_t)f + c] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
        }
    }
}
SceneFeed::SceneFeed(const Config &c, Source src, void *user)
    : c_(c), src_(src), user_(user), rates_ok_(c.in_hz > 0 && c.in_hz < c.out_hz),
      step_(rates_ok_ ? (double)c.in_hz / (double)c.out_hz : 1.0),   // the resampler's invariants need step < 1
      poly_(c.in_hz, c.out_hz, c.polyphase ? kSceneChannels : 1, c.polyphase ? c.max_block : 1) {
    c_.queue = std::max<uint32_t>(c_.queue, 1);
    c_.standing = std::min(c_.standing, c_.queue);
    q_.assign((size_t)c_.queue * kSceneChannels, 0);
    pull_bed_.assign(2 * 1024, 0);
    pull_lanes_.assign((size_t)AR_AUDIO_MAX_SOURCES * 1024, 0);
    pull_meta_.assign(8, ar_audio_tick_meta{});
    mq_.assign(kMetaQueue, ar_audio_tick_meta{});
    rbuf_.assign(((size_t)std::ceil(c_.max_block * step_) + 4) * kSceneChannels, 0);
    for (auto &s : staging_) s.assign((size_t)kSceneChannels * c_.max_block, 0.f);
    std::memset(silent_, 1, sizeof silent_);
    for (auto &g : lane_gain_) g.store(1.f);
    for (float &g : lane_gain_now_) g = 1.f;
    for (auto &b : lane_gain_snap_) b.store(false);
    tap_.assign((size_t)(c_.tap_seconds * (float)c_.out_hz) * kSceneChannels, 0);
}
void SceneFeed::set_bed_gain(float g) { bed_gain_.store(g, std::memory_order_relaxed); }
void SceneFeed::set_lane_gain(uint32_t lane, float g, bool snap) {
    if (lane >= AR_AUDIO_MAX_SOURCES) return;
    lane_gain_[lane].store(g, std::memory_order_relaxed);
    if (snap) lane_gain_snap_[lane].store(true, std::memory_order_relaxed);
}
void SceneFeed::drop(uint32_t n) {
    n = std::min(n, qsize_);
    qhead_ = (qhead_ + n) % c_.queue;
    qsize_ -= n;
    qabs_ += n;
    trimmed_.fetch_add(n);
}
void SceneFeed::fill() {
    for (int rounds = 0; rounds < 4; rounds++) {
        while (qsize_ < c_.queue) {
            ar_audio_pull p{};
            p.bed = pull_bed_.data();
            p.lanes = pull_lanes_.data();
            p.frames = std::min<uint32_t>(c_.queue - qsize_, 1024);
            p.meta = pull_meta_.data();
            p.meta_cap = (uint32_t)pull_meta_.size();
            const uint32_t got = src_ ? src_(user_, &p) : 0;
            for (uint32_t i = 0; i < p.meta_count; i++) {   // to the layer thread (single producer, single consumer)
                const uint32_t w = mw_.load(std::memory_order_relaxed), r = mr_.load(std::memory_order_acquire);
                if (w - r >= kMetaQueue) { meta_lost_.fetch_add(1); continue; }
                mq_[w % kMetaQueue] = pull_meta_[i];
                mw_.store(w + 1, std::memory_order_release);
            }
            if (got == 0) return;
            if (qsize_ && qabs_ + qsize_ != p.first_frame) drop(qsize_);
            if (qsize_ == 0) qabs_ = p.first_frame;
            for (uint32_t i = 0; i < got; i++) {
                int16_t *f = &q_[(size_t)((qhead_ + qsize_) % c_.queue) * kSceneChannels];
                f[0] = pull_bed_[2 * (size_t)i];
                f[1] = pull_bed_[2 * (size_t)i + 1];
                std::memcpy(f + 2, &pull_lanes_[(size_t)i * AR_AUDIO_MAX_SOURCES], sizeof(int16_t) * AR_AUDIO_MAX_SOURCES);
                qsize_++;
            }
            if (got < p.frames) return;
        }
        drop(qsize_ - c_.standing);
    }
}
uint32_t SceneFeed::read(int16_t *out, uint32_t n, uint64_t *first_abs) {
    fill();
    *first_abs = qabs_;
    const uint32_t k = std::min(n, qsize_);
    for (uint32_t i = 0; i < k; i++)
        std::memcpy(out + (size_t)i * kSceneChannels, &q_[(size_t)((qhead_ + i) % c_.queue) * kSceneChannels], sizeof(int16_t) * kSceneChannels);
    qhead_ = (qhead_ + k) % c_.queue;
    qsize_ -= k;
    qabs_ += k;
    if (c_.window) {
        win_min_ = std::min(win_min_, qsize_);
        win_req_ += n;
        if (win_req_ >= c_.window) {
            if (win_min_ > c_.standing) drop(win_min_ - c_.standing);
            win_min_ = UINT32_MAX;
            win_req_ = 0;
        }
    }
    return k;
}
void SceneFeed::begin_gains(uint32_t n, float *g0, float *dg) {
    const float bed1 = bed_gain_.load(std::memory_order_relaxed);
    g0[0] = g0[1] = lane_gain_started_ ? bed_gain_now_ : bed1;
    dg[0] = dg[1] = (bed1 - g0[0]) / (float)n;
    bed_gain_now_ = bed1;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        const float g1 = lane_gain_[l].load(std::memory_order_relaxed);
        const bool snap = lane_gain_snap_[l].exchange(false, std::memory_order_relaxed);
        const float from = lane_gain_started_ && !snap ? lane_gain_now_[l] : g1;
        g0[2 + l] = from;
        dg[2 + l] = (g1 - from) / (float)n;
        lane_gain_now_[l] = g1;
    }
    lane_gain_started_ = true;
}
void SceneFeed::finish_cycle(const float *st, uint32_t n, uint32_t s) {
    size_t tp = tap_pos_.load();
    for (uint32_t j = 0; j < n && tp < tap_.size(); j++)
        for (uint32_t c = 0; c < kSceneChannels && tp < tap_.size(); c++, tp++)
            tap_[tp] = (int16_t)std::lround(std::clamp(st[(size_t)c * c_.max_block + j], -1.f, 1.f) * 32767.f);
    tap_pos_.store(tp, std::memory_order_release);
    pub_.store(((uint64_t)n << 1) | s, std::memory_order_release);   // then render() publishes the key
    cycles_.fetch_add(1);
}
void SceneFeed::cycle(uint32_t n) {
    if (poly_active()) { cycle_poly(n); return; }
    const uint32_t s = (uint32_t)(pub_.load(std::memory_order_relaxed) & 1u) ^ 1u;                  // the other staging block: a late copy of the last cycle stays intact
    float *st = staging_[s].data();
    const double last = pos_ + (double)(n - 1) * step_;
    const uint32_t need = (uint32_t)std::floor(last) + 2;
    if (need > have_) {
        const uint32_t want = need - have_;
        uint64_t a = 0;
        const uint32_t got = read(&rbuf_[(size_t)have_ * kSceneChannels], want, &a);
        if (got) { rabs_ = a - have_; rabs_known_ = true; }
        if (got < want) {
            std::memset(&rbuf_[(size_t)(have_ + got) * kSceneChannels], 0, sizeof(int16_t) * kSceneChannels * (want - got));
            missing_.fetch_add(want - got);
        }
        have_ = need;
    }
    if (rabs_known_) heard_.store(rabs_ + (uint64_t)pos_, std::memory_order_release);
    const uint32_t v = seq_[s].load(std::memory_order_relaxed);   // seqlock: odd while the block is rewritten
    seq_[s].store(v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    uint8_t *sil = silent_[s];
    std::memset(sil, 1, kSceneChannels);
    float g0[kSceneChannels], dg[kSceneChannels];
    begin_gains(n, g0, dg);
    for (uint32_t j = 0; j < n; j++) {
        const double t = pos_ + (double)j * step_;
        const uint32_t k = (uint32_t)t;
        const float f = (float)(t - (double)k);
        const int16_t *a = &rbuf_[(size_t)k * kSceneChannels], *b = a + kSceneChannels;
        for (uint32_t c = 0; c < kSceneChannels; c++) {
            const float va = a[c] / 32768.f, vb = b[c] / 32768.f;
            const float g = dg[c] == 0.f ? g0[c] : g0[c] + dg[c] * (float)(j + 1);
            st[(size_t)c * c_.max_block + j] = (va + (vb - va) * f) * g;
            if (a[c] | b[c]) sil[c] = 0;
        }
    }
    seq_[s].store(v + 2, std::memory_order_release);
    const double end = pos_ + (double)n * step_;
    const uint32_t used = (uint32_t)end;
    std::memmove(rbuf_.data(), &rbuf_[(size_t)used * kSceneChannels], sizeof(int16_t) * kSceneChannels * (have_ - used));
    have_ -= used;
    pos_ = end - (double)used;
    if (rabs_known_) rabs_ += used;
    finish_cycle(st, n, s);
}
void SceneFeed::cycle_poly(uint32_t n) {
    const uint32_t s = (uint32_t)(pub_.load(std::memory_order_relaxed) & 1u) ^ 1u;
    float *st = staging_[s].data();
    const uint32_t staged = (uint32_t)(rbuf_.size() / kSceneChannels);
    const uint32_t chunk_max = c_.read_chunk && c_.read_chunk < staged ? c_.read_chunk : staged;
    for (uint32_t want = poly_.need(n); want > 0;) {
        const uint32_t chunk = std::min(want, chunk_max);
        uint64_t a = 0;
        const uint32_t got = read(rbuf_.data(), chunk, &a);
        if (got) { poly_abs0_ = (int64_t)a - (int64_t)poly_pushed_; poly_abs_known_ = true; }
        if (got < chunk) {
            std::memset(&rbuf_[(size_t)got * kSceneChannels], 0, sizeof(int16_t) * kSceneChannels * (chunk - got));
            missing_.fetch_add(chunk - got);
        }
        poly_.push(rbuf_.data(), chunk, kSceneChannels);
        poly_pushed_ += chunk;
        want -= chunk;
    }
    if (poly_abs_known_) {   // signed: the first output may still play the zero-fill before the producer's frame 0
        const int64_t heard = poly_abs0_ + (int64_t)std::floor(poly_.next_input_time());
        heard_.store(heard > 0 ? (uint64_t)heard : 0u, std::memory_order_release);
    }
    const uint32_t v = seq_[s].load(std::memory_order_relaxed);
    seq_[s].store(v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    uint8_t *sil = silent_[s];
    float g0[kSceneChannels], dg[kSceneChannels];
    begin_gains(n, g0, dg);
    poly_.render(n, poly_mode(), st, c_.max_block, sil);
    for (uint32_t c = 0; c < kSceneChannels; c++) {
        if (sil[c]) continue;
        float *o = st + (size_t)c * c_.max_block;
        for (uint32_t j = 0; j < n; j++) o[j] *= dg[c] == 0.f ? g0[c] : g0[c] + dg[c] * (float)(j + 1);
    }
    seq_[s].store(v + 2, std::memory_order_release);
    finish_cycle(st, n, s);
}
bool SceneFeed::render(uint64_t key, uint32_t n, uint32_t ch, float *out, bool *silent) {
    auto quiet = [&] { std::memset(out, 0, sizeof(float) * n); if (silent) *silent = true; return false; };
    if (n == 0) return true;
    if (!rates_ok_ || n > c_.max_block || ch >= kSceneChannels || key == kNoCycle) { invalid_.fetch_add(1); return quiet(); }
    auto newer = [key](uint64_t cur) { return cur == kNoCycle || key > cur; };
    const uint64_t cur = key_.load(std::memory_order_acquire);
    if (newer(cur)) {
        if (rendering_.test_and_set(std::memory_order_acquire)) { busy_.fetch_add(1); return quiet(); }
        if (newer(key_.load(std::memory_order_relaxed))) { cycle(n); key_.store(key, std::memory_order_release); }
        rendering_.clear(std::memory_order_release);
    } else if (key < cur) {
        stale_.fetch_add(1);   // a newer cycle is open: copy it (the bed's cycle), never render an old key again
    }
    // The current cycle (possibly newer than the key: then the newer one). pub_ is stored before key_, so it is set here.
    const uint64_t p = pub_.load(std::memory_order_acquire);
    const uint32_t slot = (uint32_t)(p & 1u), frames = (uint32_t)(p >> 1);
    if (n != frames) { mismatched_.fetch_add(1); return quiet(); }
    // Seqlock read: the block's writer only starts on it two cycles later, so a retry is never needed in practice; a copy
    // that overlapped a rewrite (a stall of two whole cycles) is discarded.
    const uint32_t v = seq_[slot].load(std::memory_order_acquire);
    std::memcpy(out, &staging_[slot][(size_t)ch * c_.max_block], sizeof(float) * n);
    const bool sil = silent_[slot][ch] != 0;
    std::atomic_thread_fence(std::memory_order_acquire);
    if ((v & 1u) || seq_[slot].load(std::memory_order_relaxed) != v) { busy_.fetch_add(1); return quiet(); }
    if (silent) *silent = sil;
    return true;
}
bool SceneFeed::pop_meta(ar_audio_tick_meta *out) {
    const uint32_t r = mr_.load(std::memory_order_relaxed), w = mw_.load(std::memory_order_acquire);
    if (r == w) return false;
    *out = mq_[r % kMetaQueue];
    mr_.store(r + 1, std::memory_order_release);
    return true;
}
bool SceneFeed::tap_ready() const { return !tap_written_ && !tap_.empty() && tap_pos_.load(std::memory_order_acquire) >= tap_.size(); }
bool SceneFeed::write_tap(const std::string &path) {
    if (!tap_ready()) return false;   // the render thread may still be writing the buffer
    tap_written_ = true;
    return wav_write_s16(path, tap_.data(), tap_.size() / kSceneChannels, kSceneChannels, c_.out_hz);
}
}

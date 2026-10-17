// Polyphase Kaiser-windowed sinc. A linear mode interpolates at the same delayed time, so a live sinc <-> linear switch
// never jumps.
#pragma once
#include <cstdint>
#include <vector>
namespace ar {
enum class PolyMode : uint8_t { sinc = 0, linear = 1 };
class Polyphase {
public:
    static constexpr uint32_t kTaps = 48;
    static constexpr double kCutoffHz = 15000.0, kBeta = 8.0;
    Polyphase(uint32_t in_hz, uint32_t out_hz, uint32_t channels, uint32_t max_out);
    bool ok() const { return ok_; }
    uint32_t phases() const { return L_; }
    uint32_t step() const { return M_; }
    double delay() const { return (double)(kTaps * L_ - 1) / (2.0 * L_); }
    uint32_t need(uint32_t n) const;
    void push(const int16_t *frames, uint32_t n, uint32_t stride);
    void render(uint32_t n, PolyMode mode, float *out, uint32_t out_stride, uint8_t *silent);
    double next_input_time() const { return (double)consumed_ + (double)t_ / L_ - delay(); }
    void reset();
    const float *coefs() const { return coef_.data(); }   // L x K, phase-major: coefs()[p * K + k] multiplies window frame k
private:
    bool ok_ = false;
    uint32_t L_ = 1, M_ = 1, ch_ = 0, cap_ = 0;
    std::vector<float> coef_;
    std::vector<float> buf_;       // channel-major: ch_ x cap_ frames, oldest first; frame i is input frame consumed_ - (K - 1) + i
    std::vector<uint32_t> zrun_;
    uint32_t have_ = 0;
    uint64_t t_ = 0;               // the next output's time in 1/L input frames, relative to buf_ frame 0
    uint64_t consumed_ = 0;
};
}

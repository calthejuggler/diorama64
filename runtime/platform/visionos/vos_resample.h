// Continues the same signal across calls whatever the chunk sizes (no clicks at audio-unit buffer boundaries).
#pragma once
#include <cstdint>
#include <vector>
namespace ar {
class StereoResampler {
public:
    using Source = uint32_t (*)(void *user, int16_t *stereo, uint32_t frames);
    StereoResampler(uint32_t in_hz, uint32_t out_hz, uint32_t max_out_frames);  // upsampling only (in_hz < out_hz); allocates once
    uint32_t render(float *out, uint32_t out_frames, Source src, void *user);
private:
    double step_;
    double pos_ = 0.0;
    std::vector<int16_t> buf_;
    uint32_t have_ = 1;
    uint32_t max_out_;
};
}

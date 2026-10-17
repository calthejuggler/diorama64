#include "depth_stats.h"
namespace ar {
DepthStats depth_holes(const uint8_t *rgba, const float *depth, size_t pixels, uint8_t alpha_min) {
    DepthStats s;
    for (size_t i = 0; i < pixels; i++) {
        if (rgba[4 * i + 3] < alpha_min) continue;
        s.covered++;
        s.holes += depth[i] == 0.f;
    }
    return s;
}
}

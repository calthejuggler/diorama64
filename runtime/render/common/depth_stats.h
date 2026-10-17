#pragma once
#include <cstddef>
#include <cstdint>
namespace ar {
struct DepthStats {
    uint64_t covered = 0, holes = 0;
    double fraction() const { return covered ? double(holes) / double(covered) : 0.0; }
};
// rgba: premultiplied target bytes; depth: reverse-Z (0 = cleared). Covered: alpha >= alpha_min (26 ~ 0.1).
DepthStats depth_holes(const uint8_t *rgba, const float *depth, size_t pixels, uint8_t alpha_min = 26);
}

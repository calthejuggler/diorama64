#pragma once
#include <cstdint>
#include <vector>
extern "C" {
#include "ar_audio_types.h"
}
namespace ar {
struct DebugPoint {
    float pos[3];
    float diameter_m;
    uint8_t rgba[4];
};
constexpr float kSoundDotDiameterM = 0.015f;
constexpr float kSoundDotMinDiameterM = 0.003f;
bool sound_source_point(uint32_t flags, const float pos[3], float level, DebugPoint *out, float size_factor = 1.f);
void debug_point_disc(const float centre[3], float radius, const float head[3], uint32_t segments, std::vector<float> *tri);
}

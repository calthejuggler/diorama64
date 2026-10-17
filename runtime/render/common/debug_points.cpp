#include "debug_points.h"
#include <cmath>
namespace ar {
bool sound_source_point(uint32_t flags, const float pos[3], float level, DebugPoint *out, float size_factor) {
    if (!(flags & AR_SRC_ACTIVE)) return false;
    static const uint8_t kMusic[3] = {0, 230, 255}, kFocus[3] = {255, 220, 0}, kSfx[3] = {255, 0, 220};
    const uint8_t *c = (flags & AR_SRC_MUSIC) ? kMusic : (flags & AR_SRC_FOCUS) ? kFocus : kSfx;
    const float l = !(level > 0.f) ? 0.f : level > 1.f ? 1.f : level;
    const float bright = 0.3f + 0.7f * l;
    DebugPoint p{};
    for (int k = 0; k < 3; k++) {
        p.pos[k] = pos[k];
        p.rgba[k] = (uint8_t)std::lround(c[k] * bright);
    }
    p.rgba[3] = 255;
    const float k = size_factor > 0.f && std::isfinite(size_factor) ? size_factor : 1.f;
    p.diameter_m = std::fmax(kSoundDotMinDiameterM, kSoundDotDiameterM * k * (0.5f + 0.5f * l));
    *out = p;
    return true;
}
void debug_point_disc(const float c[3], float radius, const float head[3], uint32_t segments, std::vector<float> *tri) {
    tri->clear();
    float n[3] = {head[0] - c[0], head[1] - c[1], head[2] - c[2]};
    const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (segments < 3 || !(nl > 1e-6f)) return;
    for (float &x : n) x /= nl;
    float a[3] = {0.f, 1.f, 0.f};
    if (std::fabs(n[1]) > 0.999f) { a[1] = 0.f; a[2] = 1.f; }
    float u[3] = {a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0]};
    const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (float &x : u) x /= ul;
    const float v[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
    tri->reserve((size_t)segments * 9);
    auto rim = [&](uint32_t i) {
        const double t = 2.0 * 3.14159265358979323846 * (i % segments) / segments;
        const float cs = radius * (float)std::cos(t), sn = radius * (float)std::sin(t);
        for (int k = 0; k < 3; k++) tri->push_back(c[k] + cs * u[k] + sn * v[k]);
    };
    for (uint32_t i = 0; i < segments; i++) {
        for (int k = 0; k < 3; k++) tri->push_back(c[k]);
        rim(i);
        rim(i + 1);
    }
}
}

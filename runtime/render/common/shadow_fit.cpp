#include "shadow_fit.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace ar {
TableShadowBox table_shadow_box(const ar_level_info &lv, const float half[2], float size_upm) {
    TableShadowBox b;
    const float upm = lv.units_per_meter;
    const float w = lv.bounds_max[0] - lv.bounds_min[0], d = lv.bounds_max[2] - lv.bounds_min[2];
    if (!(upm > 0.f) || !(w > 0.f) || !(d > 0.f)) return b;
    const float su = size_upm > 0.f && std::isfinite(size_upm) ? size_upm : upm;
    const float m = 0.02f * su;
    b.x0 = lv.bounds_min[0] - 0.1f * w - m; b.x1 = lv.bounds_max[0] + 0.1f * w + m;
    b.z0 = lv.bounds_min[2] - 0.1f * d - m; b.z1 = lv.bounds_max[2] + 0.1f * d + m;
    if (half[0] > 0.f && half[1] > 0.f) {
        b.x0 = std::max(b.x0, -half[0] * upm); b.x1 = std::min(b.x1, half[0] * upm);
        b.z0 = std::max(b.z0, -half[1] * upm); b.z1 = std::min(b.z1, half[1] * upm);
    }
    b.ground_y = lv.ground_y;
    b.upm = upm;
    b.size_upm = su;
    b.valid = b.x1 > b.x0 && b.z1 > b.z0;
    return b;
}
UndersideDesc underside_desc(const TableShadowBox &b) {
    UndersideDesc u{};
    u.y0 = b.ground_y - 0.02f * b.size_upm;
    u.span = 0.27f * b.size_upm;
    float *m = u.clip_from_level;
    std::memset(m, 0, sizeof u.clip_from_level);
    const float sx = 2.f / (b.x1 - b.x0), sz = 2.f / (b.z1 - b.z0);
    m[0] = sx;  m[12] = -1.f - b.x0 * sx;
    m[9] = sz;  m[13] = -1.f - b.z0 * sz;
    m[6] = 1.f / u.span; m[14] = -u.y0 / u.span;
    m[15] = 1.f;
    return u;
}
bool water_footprint_window(const float water[][5], uint32_t n, const float bmin[3], const float bmax[3], float *w) {
    if (!water || n == 0) return false;
    float x0 = water[0][0], z0 = water[0][1], x1 = water[0][2], z1 = water[0][3];
    for (uint32_t i = 1; i < n; i++) {
        x0 = std::min(x0, water[i][0]); z0 = std::min(z0, water[i][1]);
        x1 = std::max(x1, water[i][2]); z1 = std::max(z1, water[i][3]);
    }
    if (bmin && bmax && bmax[0] > bmin[0] && bmax[2] > bmin[2] && std::isfinite(bmax[0] - bmin[0]) && std::isfinite(bmax[2] - bmin[2])) {
        x0 = std::min(x0, bmin[0]); z0 = std::min(z0, bmin[2]); x1 = std::max(x1, bmax[0]); z1 = std::max(z1, bmax[2]);
    }
    if (!std::isfinite(x0) || !std::isfinite(z0) || !std::isfinite(x1) || !std::isfinite(z1)) return false;
    w[0] = x0; w[1] = z0; w[2] = x1; w[3] = z1;
    return true;
}
bool water_footprint_clip(const float *w, float top_y, float bottom_y, float *m) {
    if (!w || !(top_y > bottom_y) || !std::isfinite(top_y) || !std::isfinite(bottom_y)) return false;
    const float x0 = w[0], z0 = w[1], x1 = w[2], z1 = w[3];
    if (!(x1 > x0) || !(z1 > z0) || !std::isfinite(x1 - x0) || !std::isfinite(z1 - z0)) return false;
    std::memset(m, 0, 16 * sizeof(float));
    const float sx = 2.f / (x1 - x0), sz = 2.f / (z1 - z0), span = top_y - bottom_y;
    m[0] = sx;  m[12] = -1.f - x0 * sx;
    m[9] = -sz; m[13] = 1.f + z0 * sz;
    m[6] = -1.f / span; m[14] = top_y / span;
    m[15] = 1.f;
    return true;
}
void uv_from_clip(const float c[16], float out[16]) {
    // uv = B * clip with B: u = 0.5 x + 0.5 w, v = -0.5 y + 0.5 w, depth = z, w = w
    for (int col = 0; col < 4; col++) {
        const float x = c[col * 4 + 0], y = c[col * 4 + 1], z = c[col * 4 + 2], w = c[col * 4 + 3];
        out[col * 4 + 0] = 0.5f * x + 0.5f * w;
        out[col * 4 + 1] = -0.5f * y + 0.5f * w;
        out[col * 4 + 2] = z;
        out[col * 4 + 3] = w;
    }
}
const float *ShadowDirHold::update(const float d[3]) {
    const float dot = dir_[0] * d[0] + dir_[1] * d[1] + dir_[2] * d[2];
    const float n2 = (dir_[0] * dir_[0] + dir_[1] * dir_[1] + dir_[2] * dir_[2]) * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!set_ || !(dot > 0.f) || dot * dot <= 0.99992f * n2) {
        set_ = true;
        for (int k = 0; k < 3; k++) dir_[k] = d[k];
    }
    return dir_;
}
ShadowFit fit_shadow(const float dir_in[3], const float bmin[3], const float bmax[3], uint32_t size) {
    ShadowFit f{};
    double l[3] = {dir_in[0], dir_in[1], dir_in[2]};
    double ll = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    if (!(ll > 1e-9)) { l[0] = 0; l[1] = 1; l[2] = 0; ll = 1; }
    for (double &c : l) c /= ll;
    const bool steep = std::fabs(l[1]) >= 0.99;
    const double ref[3] = {0.0, steep ? 0.0 : 1.0, steep ? 1.0 : 0.0};
    double t[3] = {ref[1] * l[2] - ref[2] * l[1], ref[2] * l[0] - ref[0] * l[2], ref[0] * l[1] - ref[1] * l[0]};
    const double tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    for (double &c : t) c /= tl;
    const double b[3] = {l[1] * t[2] - l[2] * t[1], l[2] * t[0] - l[0] * t[2], l[0] * t[1] - l[1] * t[0]};
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (int c = 0; c < 8; c++) {
        const double p[3] = {(c & 1) ? bmax[0] : bmin[0], (c & 2) ? bmax[1] : bmin[1], (c & 4) ? bmax[2] : bmin[2]};
        const double q[3] = {t[0] * p[0] + t[1] * p[1] + t[2] * p[2], b[0] * p[0] + b[1] * p[1] + b[2] * p[2],
                             l[0] * p[0] + l[1] * p[1] + l[2] * p[2]};
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], q[k]); hi[k] = std::max(hi[k], q[k]); }
    }
    for (int k = 0; k < 3; k++) { const double pad = 0.01 * (hi[k] - lo[k]) + 1.0; lo[k] -= pad; hi[k] += pad; }
    const double sx = 2.0 / (hi[0] - lo[0]), sy = 2.0 / (hi[1] - lo[1]), sz = 1.0 / (hi[2] - lo[2]);
    float *m = f.clip_from_level;
    for (int k = 0; k < 3; k++) {
        m[k * 4 + 0] = (float)(sx * t[k]);
        m[k * 4 + 1] = (float)(sy * b[k]);
        m[k * 4 + 2] = (float)(-sz * l[k]);                            // nearer the light (larger l.p) = smaller depth
        m[k * 4 + 3] = 0.f;
    }
    m[12] = (float)(-1.0 - sx * lo[0]);
    m[13] = (float)(-1.0 - sy * lo[1]);
    m[14] = (float)(sz * hi[2]);
    m[15] = 1.f;
    f.texel_units = (float)(std::max(hi[0] - lo[0], hi[1] - lo[1]) / (double)size);
    return f;
}
std::vector<std::array<float, 3>> ao_directions(uint32_t n) {
    std::vector<std::array<float, 3>> d;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (uint32_t i = 0; i < n; i++) {
        const double y = 0.15 + 0.85 * (i + 0.5) / n;
        const double r = std::sqrt(1.0 - y * y), a = golden * i;
        d.push_back({(float)(r * std::cos(a)), (float)y, (float)(r * std::sin(a))});
    }
    return d;
}
AoVolume ao_volume(const float bmin[3], const float bmax[3], uint32_t max_dim) {
    AoVolume v{};
    float ext = 1.f;
    for (int k = 0; k < 3; k++) ext = std::max(ext, bmax[k] - bmin[k]);
    v.voxel = ext / (float)max_dim;
    for (int k = 0; k < 3; k++) {
        v.dims[k] = std::min<uint32_t>(max_dim + 1, std::max<uint32_t>(2, (uint32_t)std::ceil((bmax[k] - bmin[k]) / v.voxel) + 1));
        v.min[k] = bmin[k];
    }
    return v;
}
uint64_t ao_bounds_key(const float bmin[3], const float bmax[3]) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](int64_t x) { for (int i = 0; i < 8; i++) { h ^= (uint64_t)((x >> (8 * i)) & 0xFF); h *= 1099511628211ull; } };
    for (int k = 0; k < 3; k++) { mix(std::lround(bmin[k])); mix(std::lround(bmax[k])); }
    return h;
}
void AoSceneKey::mix(uint64_t x) { h_ ^= x; h_ *= 0x100000001B3ull; h_ ^= h_ >> 31; h_ *= 0x9E3779B97F4A7C15ull; }
void AoSceneKey::draw(uint32_t vertex_count, uint32_t tex0, uint64_t cc_slots, uint32_t cc_opts) {
    mix(vertex_count); mix(tex0); mix(cc_slots); mix(cc_opts);
    n_ += vertex_count;
}
void AoSceneKey::clip(const float lo[4], const float hi[4]) {
    for (int k = 0; k < 4; k++) { uint32_t u; std::memcpy(&u, &lo[k], 4); mix(u); std::memcpy(&u, &hi[k], 4); mix(u); }
}
uint64_t AoSceneKey::key() const {
    AoSceneKey k = *this;
    k.mix(n_);
    return k.h_;
}
bool AoSchedule::should_bake(uint64_t key) {
    if (!has_pending_ || key != pending_) { pending_ = key; has_pending_ = true; stable_ = 1; }
    else if (stable_ < 1000000) stable_++;
    return stable_ == 15 && !valid(key);
}
bool AoSchedule::valid(uint64_t key) const {
    for (const Slot &s : slots_) if (s.set && s.key == key) return true;
    return false;
}
int AoSchedule::use(uint64_t key) {
    for (uint32_t i = 0; i < kSlots; i++)
        if (slots_[i].set && slots_[i].key == key) { slots_[i].used = ++clock_; return (int)i; }
    return -1;
}
int AoSchedule::latest(uint64_t bounds_key) {
    int best = -1;
    for (uint32_t i = 0; i < kSlots; i++)
        if (slots_[i].set && slots_[i].bounds == bounds_key && (best < 0 || slots_[i].used > slots_[best].used)) best = (int)i;
    if (best >= 0) slots_[best].used = ++clock_;
    return best;
}
uint32_t AoSchedule::slot_for(uint64_t key) const {
    for (uint32_t i = 0; i < kSlots; i++) if (slots_[i].set && slots_[i].key == key) return i;
    uint32_t e = 0;
    for (uint32_t i = 0; i < kSlots; i++) {
        if (!slots_[i].set) return i;
        if (slots_[i].used < slots_[e].used) e = i;
    }
    return e;
}
uint32_t AoSchedule::done(uint64_t key, uint64_t bounds_key) {
    const uint32_t e = slot_for(key);
    slots_[e].key = key; slots_[e].bounds = bounds_key; slots_[e].set = true; slots_[e].used = ++clock_;
    return e;
}
}

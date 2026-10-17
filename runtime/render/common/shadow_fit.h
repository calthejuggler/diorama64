// Pure; Metal clip conventions (x, y in [-1, 1], z in [0, 1], column-major).
#pragma once
#include <array>
#include <cstdint>
#include <vector>
extern "C" {
#include "ar_adapter.h"
}
namespace ar {
struct TableShadowBox {
    bool valid = false;
    float x0 = 0.f, z0 = 0.f, x1 = 0.f, z1 = 0.f;
    float ground_y = 0.f, upm = 1.f;
    float size_upm = 1.f;
};
TableShadowBox table_shadow_box(const ar_level_info &lv, const float table_half_m[2], float size_upm = 0.f);
struct UndersideDesc { float clip_from_level[16]; float y0, span; };
UndersideDesc underside_desc(const TableShadowBox &b);
void uv_from_clip(const float clip[16], float out[16]);
// SM64's water quads are their own shapes at a box's height and reach past it (JRB): the window is the union of the
// boxes and the level's bounds.
bool water_footprint_window(const float water[][5], uint32_t n, const float bmin[3], const float bmax[3], float window[4]);
bool water_footprint_clip(const float window[4], float top_y, float bottom_y, float clip_from_level[16]);
struct ShadowFit { float clip_from_level[16]; float texel_units; };
// The direction the shadow map is fitted to: the light's, held until it turns by more than 0.5 degrees from the held one
// (as KeyLightSender), so a light converging after a probe update does not rotate the map every frame.
class ShadowDirHold {
public:
    const float *update(const float dir[3]);
private:
    bool set_ = false;
    float dir_[3] = {0.f, 1.f, 0.f};
};
ShadowFit fit_shadow(const float dir[3], const float bmin[3], const float bmax[3], uint32_t size);
std::vector<std::array<float, 3>> ao_directions(uint32_t n);
struct AoVolume { uint32_t dims[3]; float min[3]; float voxel; };
AoVolume ao_volume(const float bmin[3], const float bmax[3], uint32_t max_dim);
uint64_t ao_bounds_key(const float bmin[3], const float bmax[3]);
class AoSceneKey {
public:
    explicit AoSceneKey(uint64_t bounds_key) : h_(bounds_key ^ 0x9E3779B97F4A7C15ull) {}
    void draw(uint32_t vertex_count, uint32_t tex0, uint64_t cc_slots, uint32_t cc_opts);
    void clip(const float lo[4], const float hi[4]);
    uint32_t vertices() const { return n_; }
    uint64_t key() const;
private:
    void mix(uint64_t x);
    uint64_t h_;
    uint32_t n_ = 0;
};
// Never another area's volume: while a new key waits, latest(bounds) gives the newest bake of the same bounds.
class AoSchedule {
public:
    static constexpr uint32_t kSlots = 4;
    bool should_bake(uint64_t key);
    uint32_t slot_for(uint64_t key) const;
    uint32_t done(uint64_t key, uint64_t bounds_key);
    bool valid(uint64_t key) const;
    int use(uint64_t key);
    int latest(uint64_t bounds_key);
private:
    uint64_t pending_ = 0;
    bool has_pending_ = false;
    uint32_t stable_ = 0;
    struct Slot { uint64_t key = 0, bounds = 0, used = 0; bool set = false; };
    Slot slots_[kSlots];
    uint64_t clock_ = 0;
};
}

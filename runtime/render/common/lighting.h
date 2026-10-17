#pragma once
#include <cstdint>
namespace ar {
struct LightingFrame {
    bool on = false;
    float dir[3] = {0.f, 1.f, 0.f};    // towards the key light, level (= table) axes, unit
    float tint[3] = {1.f, 1.f, 1.f};   // tint x gain: multiplies the vertex colour of FC_LF_TINT draws (gamma space)
    float ambient = 0.5f;
    bool contact = false; float contact_strength = 0.f;
    float upm = 1.f, ground_y = 0.f;
    uint8_t shadows = 0;
    bool hide_fake_shadows = false;
    float softness = 0.5f;
    float table_cast = 0.f;
    bool relight = false;
    float relight_strength = 0.f;
    bool ao = false;
    float ao_strength = 0.f;
    bool ao_ready = false;
    uint8_t ao_slot = 0;
    float bounds_min[3] = {0.f, 0.f, 0.f}, bounds_max[3] = {0.f, 0.f, 0.f};
    bool toy = false;
    float gloss = 0.f, rim = 0.f;
    bool reflections = false;
    void *env_cube = nullptr;
    float env_from_level[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // rotation level -> probe axes, rows
    float env_exposure = 1.f;
    float env_mix = 1.f;
    void *env_prev_cube = nullptr;
    float env_prev_from_level[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float env_prev_exposure = 1.f;
    float env_fade = 1.f;
    // The level views' world clip (as FrameView::wclip_lo/hi: lo.w 1 = floor at lo.y, hi.w 1 = XZ box lo.xz..hi.xz).
    // The shadow map, the underside map and the AO bake cut the same geometry; the AO key includes it. All 0: no clip.
    float wclip_lo[4] = {0.f, 0.f, 0.f, 0.f}, wclip_hi[4] = {0.f, 0.f, 0.f, 0.f};
    float ao_radius = 0.f;
    bool liquid = false;
    float liquid_time_s = 0.f;
    float liquid_upm = 8000.f;
    uint32_t water_count = 0;
    float water[8][5] = {};
    bool water_trim = false;
    bool cut = false;
    float cut_target[3] = {0.f, 0.f, 0.f};
    float cut_radius = 0.f, cut_keep = 0.f, cut_cell = 1.f;
    bool cut_warm = false;
    uint8_t trans_kind = 0;
    float trans_cover = 0.f;
    float trans_rgb[3] = {0.f, 0.f, 0.f}, trans_centre[3] = {0.f, 0.f, 0.f};
    float trans_radius = 0.f, trans_soft = 0.f;
    float trans_clear = 0.f;
    bool trans_clear_warm = false;
    bool fine_textures = false;
};
// The GPU reduction of an environment cube: mean radiance (linear, Display P3) and the luminance-weighted
// mean direction, both in the probe's axes, solid-angle weighted.
struct EnvStats { float l0[3] = {0.f, 0.f, 0.f}; float l1[3] = {0.f, 0.f, 0.f}; };
}

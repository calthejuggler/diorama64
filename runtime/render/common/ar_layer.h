// Shared by the macOS harness and the visionOS session, so both draw exactly the same thing.
#pragma once
#include <vector>
extern "C" {
#include "ar_runtime.h"
}
#include "renderer.h"
namespace ar {
// Measured on BoB's pit-ball shadows: 0.1 flickered on slopes; 16 units (2 mm at 1:8000) with slope 0.5 shows them
// steadily and stays hidden under a real occluder 3 mm (24 units) above the ground.
constexpr float kDecalPullUnits = 16.f;
// min(upm, reference): the AR layer's metric sizes never grow past their size at the reference scale; a world drawn
// smaller shrinks them.
float layer_size_upm(float units_per_meter, float reference_units_per_meter);
float layer_size_factor(float units_per_meter, float reference_units_per_meter);
float layer_ao_radius(float reference_units_per_meter);
// Per pixel its world size is metric, so a world drawn smaller gets less (else it leaks through the 24-unit occluder).
float decal_slope_scale(float units_per_meter, float reference_units_per_meter);
constexpr float kVanillaDecalPullUnits = 0.1f;
struct DecalParams { float pull, slope; };
DecalParams decal_params(bool fix, float units_per_meter, float reference_units_per_meter);
struct ArLayerOptions {
    bool overlays = true;
    bool silhouette = true;
    bool flat_hud = false;
    const fc_frame *table = nullptr;
    const LightingFrame *lighting = nullptr;
    bool layered = false;
    // With a rasterization rate map, the map's screen size: the passes' viewports are in screen pixels, while the views
    // report the texture map's.
    uint32_t view_w = 0, view_h = 0;
    const std::vector<DebugPoint> *points = nullptr;
    bool decal_fix = true;
    bool world_clip_floor = false, world_clip_box = false;
};
void overlay_footprint(const ar_render_frame &rf, float *half_x, float *half_z);
void layer_world_clip(const ar_render_frame &rf, const ArLayerOptions &o, float lo[4], float hi[4]);
TableShadowBox layer_table_shadow_box(const ar_render_frame &rf);
void apply_frame_look(const fc_look *look, const ar_level_info &level, LightingFrame *lf, float interp_alpha = 1.f);
// the alpha a look's between-tick values use: the frame's interpolation alpha only when its
// vertices are drawn between ticks too (prev_pos set and alpha < 1, renderer.cpp), else 1 (the tick's value).
float look_interp_alpha(const fc_frame *snapshot, float interp_alpha);
std::vector<PreviewLine> preview_lines(const ar_preview &p, float half_x, float half_z);
void draw_ar_layer(Renderer &r, uint32_t target, const ar_render_frame &rf, const ArLayerOptions &o, float dt_s);
}

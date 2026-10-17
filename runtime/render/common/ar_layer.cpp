#include "ar_layer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace ar {
float layer_size_upm(float upm, float ref) { return ref > 0.f && std::isfinite(ref) && upm > ref && std::isfinite(upm) ? ref : upm; }
float layer_size_factor(float upm, float ref) {
    return ref > 0.f && std::isfinite(ref) && upm > ref && std::isfinite(upm) ? ref / upm : 1.f;
}
float layer_ao_radius(float ref) { return ref > 0.f && std::isfinite(ref) ? ref / 10.f : 800.f; }
float decal_slope_scale(float upm, float ref) { return kDecalSlopeScale * layer_size_factor(upm, ref); }
DecalParams decal_params(bool fix, float upm, float ref) {
    return fix ? DecalParams{kDecalPullUnits, decal_slope_scale(upm, ref)} : DecalParams{kVanillaDecalPullUnits, 0.f};
}
void overlay_footprint(const ar_render_frame &rf, float *hx, float *hz) {
    const float upm = rf.level.units_per_meter > 0.f ? rf.level.units_per_meter : 8000.f;
    float x = std::fmax(std::fabs(rf.level.bounds_min[0]), std::fabs(rf.level.bounds_max[0])) / upm;
    float z = std::fmax(std::fabs(rf.level.bounds_min[2]), std::fabs(rf.level.bounds_max[2])) / upm;
    if (!(x > 0.05f) || !(z > 0.05f)) { x = 0.f; z = 0.f; }
    x = std::fmax(x, rf.table_half_extent[0]);
    z = std::fmax(z, rf.table_half_extent[1]);
    if (!(x > 0.05f) || !(z > 0.05f)) { x = 0.85f; z = 0.85f; }
    *hx = x; *hz = z;
}
std::vector<PreviewLine> preview_lines(const ar_preview &p, float hx, float hz) {
    std::vector<PreviewLine> out;
    if (p.state != AR_PREVIEW_SEEKING && p.state != AR_PREVIEW_DRAGGING) return out;
    constexpr float kLift = 0.002f, kRing = 0.06f;
    constexpr int kRingSegments = 32;
    const ar_pose &pose = p.tracking_from_preview;
    auto put = [&](PreviewLine &l, float x, float z) {
        const float local[3] = {x, kLift, z};
        float w[3];
        ar_quat_rotate(pose.quat, local, w);
        for (int k = 0; k < 3; k++) l.pts.push_back(w[k] + pose.pos[k]);
    };
    auto footprint = [&](const uint8_t rgba[4]) {
        PreviewLine l{{}, {rgba[0], rgba[1], rgba[2], rgba[3]}, 0.003f, true};
        const float c[5][2] = {{-hx, -hz}, {hx, -hz}, {hx, hz}, {-hx, hz}, {-hx, -hz}};
        for (const auto &q : c) put(l, q[0], q[1]);
        out.push_back(std::move(l));
    };
    if (p.state == AR_PREVIEW_DRAGGING) {
        static const uint8_t kCyan[4] = {80, 200, 255, 255};
        footprint(kCyan);
        return out;
    }
    if (!p.outlines_only) {
        static const uint8_t kGreen[4] = {80, 220, 120, 255}, kAmber[4] = {255, 180, 40, 255};
        const uint8_t *col = p.on_plane ? kGreen : kAmber;
        PreviewLine ring{{}, {col[0], col[1], col[2], col[3]}, 0.004f, true};
        for (int i = 0; i <= kRingSegments; i++) {
            const double a = 2.0 * 3.14159265358979323846 * (i % kRingSegments) / kRingSegments;
            put(ring, kRing * (float)std::cos(a), kRing * (float)std::sin(a));
        }
        out.push_back(std::move(ring));
        footprint(col);
    }
    const uint32_t n = p.outline_count < AR_PREVIEW_MAX_OUTLINES ? p.outline_count : AR_PREVIEW_MAX_OUTLINES;
    for (uint32_t i = 0; i < n; i++) {
        PreviewLine l{{}, {255, 255, 255, 128}, 0.002f};
        for (int c = 0; c <= 4; c++) {
            const float *q = p.outlines[i][c % 4];
            l.pts.push_back(q[0]); l.pts.push_back(q[1] + kLift); l.pts.push_back(q[2]);
        }
        out.push_back(std::move(l));
    }
    return out;
}
// After the table shadow: its depth and stencil writes would otherwise cut the shadow under the lines.
static void draw_preview(Renderer &r, uint32_t target, const ar_render_frame &rf, const FrameView *tracking) {
    if (rf.preview.state == AR_PREVIEW_NONE || rf.view_count == 0) return;
    float hx = 0.f, hz = 0.f;
    overlay_footprint(rf, &hx, &hz);
    const std::vector<PreviewLine> lines = preview_lines(rf.preview, hx, hz);
    if (lines.empty()) return;
    const ar_pose head = ar_pose_mul(rf.tracking_from_table, rf.head_table);
    r.draw_lines(target, lines, tracking, rf.view_count, head.pos);
}
void layer_world_clip(const ar_render_frame &rf, const ArLayerOptions &o, float lo[4], float hi[4]) {
    for (int k = 0; k < 4; k++) { lo[k] = 0.f; hi[k] = 0.f; }
    const fc_look *L = rf.snapshot ? rf.snapshot->look : nullptr;
    if (L && o.lighting && o.lighting->on) {
        if (L->flags & FC_LOOK_CLIP_FLOOR) { lo[1] = L->clip_floor_y; lo[3] = 1.f; }
        if (L->flags & FC_LOOK_CLIP_BOX) {
            lo[0] = L->clip_lo[0]; lo[2] = L->clip_lo[1]; hi[0] = L->clip_hi[0]; hi[2] = L->clip_hi[1]; hi[3] = 1.f;
        }
        return;
    }
    if (o.world_clip_floor) { lo[1] = rf.level.ground_y; lo[3] = 1.f; }
    if (o.world_clip_box) {
        lo[0] = rf.level.bounds_min[0]; lo[2] = rf.level.bounds_min[2];
        hi[0] = rf.level.bounds_max[0]; hi[2] = rf.level.bounds_max[2]; hi[3] = 1.f;
    }
}
TableShadowBox layer_table_shadow_box(const ar_render_frame &rf) {
    const fc_look *L = rf.snapshot ? rf.snapshot->look : nullptr;
    if (L && (L->flags & FC_LOOK_FLOATING)) return TableShadowBox{};
    return table_shadow_box(rf.level, rf.table_half_extent, layer_size_upm(rf.level.units_per_meter, rf.level.reference_units_per_meter));
}
float look_interp_alpha(const fc_frame *snapshot, float interp_alpha) {
    return snapshot && snapshot->prev_pos && std::isfinite(interp_alpha) && interp_alpha < 1.f ? interp_alpha : 1.f;
}
void apply_frame_look(const fc_look *look, const ar_level_info &level, LightingFrame *lf, float interp_alpha) {
    if (!look || !lf) return;
    const float upm = layer_size_upm(level.units_per_meter > 0.f ? level.units_per_meter : 8000.f, level.reference_units_per_meter);
    if (look->flags & FC_LOOK_LIQUID) {
        lf->liquid = true;
        lf->liquid_time_s = std::isfinite(look->time_s) ? look->time_s : 0.f;
        lf->liquid_upm = upm;
        lf->water_count = std::min<uint32_t>(look->water_count, FC_LOOK_MAX_WATER);
        for (uint32_t i = 0; i < lf->water_count; i++) {
            const fc_water_box &w = look->water[i];
            lf->water[i][0] = w.lo[0]; lf->water[i][1] = w.lo[1]; lf->water[i][2] = w.hi[0]; lf->water[i][3] = w.hi[1];
            lf->water[i][4] = w.surface_y;
        }
        lf->water_trim = (look->flags & FC_LOOK_WATER_TRIM) != 0 && lf->water_count > 0;
    }
    if (look->flags & FC_LOOK_CUT_ARMED) lf->cut_warm = true;
    if ((look->flags & FC_LOOK_CUTAWAY) && std::isfinite(look->cut_target[0]) && std::isfinite(look->cut_target[1]) &&
        std::isfinite(look->cut_target[2]) && look->cut_radius > 0.f && std::isfinite(look->cut_radius)) {
        lf->cut = true;
        // between ticks, from the previous tick's target, as Mario's own vertices (else the hole steps at 30 Hz)
        const float a = std::isfinite(interp_alpha) ? std::clamp(interp_alpha, 0.f, 1.f) : 1.f;
        const bool prev = (look->flags & FC_LOOK_CUT_PREV) && std::isfinite(look->cut_target_prev[0]) &&
                          std::isfinite(look->cut_target_prev[1]) && std::isfinite(look->cut_target_prev[2]);
        for (int k = 0; k < 3; k++)
            lf->cut_target[k] = prev ? look->cut_target_prev[k] + (look->cut_target[k] - look->cut_target_prev[k]) * a : look->cut_target[k];
        lf->cut_radius = look->cut_radius;
        lf->cut_keep = 0.15f * upm;
        lf->cut_cell = 0.0025f * upm;   // 2.5 mm: 0.8 mm was about a headset pixel, so the dither shimmered with the head
    }
    if (look->flags & FC_LOOK_TRANS_CLEAR_ARMED) lf->trans_clear_warm = true;
    if ((look->flags & FC_LOOK_TRANSITION) && look->trans_kind == FC_TRANS_CLEAR && std::isfinite(look->trans_cover)) {
        const float a = std::isfinite(interp_alpha) ? std::clamp(interp_alpha, 0.f, 1.f) : 1.f;
        const float cur = std::clamp(look->trans_cover, 0.f, 1.f);
        const float prev = std::isfinite(look->trans_cover_prev) ? std::clamp(look->trans_cover_prev, 0.f, 1.f) : cur;
        lf->trans_clear = prev + (cur - prev) * a;
    }
    if ((look->flags & FC_LOOK_TRANSITION) && (look->trans_kind == FC_TRANS_FADE || look->trans_kind == FC_TRANS_IRIS) &&
        std::isfinite(look->trans_cover)) {
        lf->trans_kind = look->trans_kind;
        const float a = std::isfinite(interp_alpha) ? std::clamp(interp_alpha, 0.f, 1.f) : 1.f;
        const float cur = std::clamp(look->trans_cover, 0.f, 1.f);
        const float prev = std::isfinite(look->trans_cover_prev) ? std::clamp(look->trans_cover_prev, 0.f, 1.f) : cur;
        lf->trans_cover = prev + (cur - prev) * a;
        for (int k = 0; k < 3; k++) { lf->trans_rgb[k] = look->trans_rgb[k] / 255.f; lf->trans_centre[k] = look->trans_centre[k]; }
        lf->trans_radius = look->trans_radius;
        lf->trans_soft = 0.03f * upm;
        const bool centred = std::isfinite(look->trans_centre[0]) && std::isfinite(look->trans_centre[2]) &&
                             look->trans_radius > 0.f && std::isfinite(look->trans_radius);
        if (lf->trans_kind == FC_TRANS_IRIS && !centred) lf->trans_kind = FC_TRANS_FADE;
    }
    if (look->flags & FC_LOOK_FINE_TEXTURES) lf->fine_textures = true;
}
void draw_ar_layer(Renderer &r, uint32_t target, const ar_render_frame &rf, const ArLayerOptions &o, float dt_s) {
    static const float kTransparent[4] = {0.f, 0.f, 0.f, 0.f};
    FrameView table[AR_MAX_VIEWS], level[AR_MAX_VIEWS], tracking[AR_MAX_VIEWS];
    const DecalParams dp = decal_params(o.decal_fix, rf.level.units_per_meter, rf.level.reference_units_per_meter);
    uint32_t h = 0;
    float wlo[4], whi[4];
    layer_world_clip(rf, o, wlo, whi);
    for (uint32_t i = 0; i < rf.view_count; i++) {
        const ar_view_out &v = rf.views[i];
        const Viewport vp{o.layered ? 0.f : float(v.x), 0.f, float(o.view_w ? o.view_w : v.width), float(o.view_h ? o.view_h : v.height)};
        const uint32_t layer = o.layered ? i : 0u;
        table[i] = FrameView{v.clip_from_table, {0.f, 0.f, 0.f}, vp, {0.f, 0.f, 0.f}, layer};
        tracking[i] = FrameView{v.clip_from_tracking, {0.f, 0.f, 0.f}, vp, {0.f, 0.f, 0.f}, layer};
        level[i] = FrameView{v.clip_from_level, {rf.head_level[0], rf.head_level[1], rf.head_level[2]}, vp,
                             {v.eye_level[0], v.eye_level[1], v.eye_level[2]}, layer, dp.slope};
        std::memcpy(level[i].wclip_lo, wlo, sizeof wlo); std::memcpy(level[i].wclip_hi, whi, sizeof whi);
        h = std::max(h, (uint32_t)vp.h);
    }
    const bool ar_overlays = o.overlays && !o.flat_hud && rf.table_valid && rf.snapshot && rf.view_count > 0;
    if (!ar_overlays) r.hide_panel();
    if (rf.view_count == 0) return;
    const fc_frame empty{};
    if (!rf.table_valid) { r.draw_world(target, empty, level, 1, 0.f, kTransparent); draw_preview(r, target, rf, tracking); return; }
    const float *clear = kTransparent;
    if (o.table) { r.draw_world(target, *o.table, table, rf.view_count, 0.f, kTransparent); clear = nullptr; }
    if (!rf.snapshot) { if (clear) r.draw_world(target, empty, level, 1, 0.f, clear); draw_preview(r, target, rf, tracking); return; }
    const float upm = rf.level.units_per_meter > 0.f ? rf.level.units_per_meter : 8000.f;
    LightingFrame lf;
    if (o.lighting) {
        lf = *o.lighting;
        for (int k = 0; k < 3; k++) { lf.bounds_min[k] = rf.level.bounds_min[k]; lf.bounds_max[k] = rf.level.bounds_max[k]; }
        // The depth passes and the AO bake cut the level views' clip; the AO radius is 0.1 m at the game's reference
        // scale (SM64: 8000 / 10 = 800).
        if (rf.view_count > 0) {
            std::memcpy(lf.wclip_lo, level[0].wclip_lo, sizeof lf.wclip_lo);
            std::memcpy(lf.wclip_hi, level[0].wclip_hi, sizeof lf.wclip_hi);
        }
        const float ref = rf.level.reference_units_per_meter;
        lf.ao_radius = layer_ao_radius(ref);
        if (lf.on) apply_frame_look(rf.snapshot->look, rf.level, &lf, look_interp_alpha(rf.snapshot, rf.interp_alpha));
    }
    const LightingFrame *light = o.lighting ? &lf : nullptr;
    r.set_fine_textures(light && light->on && light->fine_textures);
    r.set_interp(rf.interp_alpha);
    r.draw_world(target, *rf.snapshot, level, rf.view_count, dp.pull, clear, o.silhouette ? &kSilhouetteGray : nullptr, light);
    // After the world and its depth fill: the shadow goes only where nothing was drawn, at the table plane's depth.
    if (light) r.draw_table_shadow(target, *rf.snapshot, level, rf.view_count, lf, layer_table_shadow_box(rf));
    r.draw_polylines(target, *rf.snapshot, level, rf.view_count, rf.head_level, upm);
    r.set_interp(1.f);               // overlays and later draws never interpolate
    draw_preview(r, target, rf, tracking);
    if (o.points && !o.points->empty()) r.draw_points(target, *o.points, table, rf.view_count, rf.head_table.pos);
    if (o.overlays && o.flat_hud) {
        const float ph = float(h) * 0.4f;
        r.draw_overlay_panel(target, *rf.snapshot, Viewport{12.f, 12.f, ph / screen_aspect(*rf.snapshot), ph});
    } else if (o.overlays) {
        OverlayContext oc{};
        oc.head_table = rf.head_table;
        oc.units_per_meter = upm;
        oc.ground_y = rf.level.ground_y;
        overlay_footprint(rf, &oc.half_x, &oc.half_z);
        oc.dt_s = dt_s;
        oc.decal_pull = dp.pull;
        r.draw_overlays(target, *rf.snapshot, table, rf.view_count, oc);
    }
}
}

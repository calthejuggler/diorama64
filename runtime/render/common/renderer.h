#pragma once
#include <unordered_set>
#include <vector>
#include "debug_points.h"
#include "lighting.h"
#include "render_device.h"
#include "shadow_fit.h"
extern "C" {
#include "ar_float_panel.h"
#include "ar_math.h"
#include "ar_view.h"
}
namespace ar {
// Name layer explicitly: in {clip, eye, vp, 1} brace elision binds the 1 to eye_view[0].
struct FrameView { ar_mat4 clip_from_model; float eye_model[3]; Viewport viewport; float eye_view[3] = {0.f, 0.f, 0.f}; uint32_t layer = 0;
                   float decal_slope = kDecalSlopeScale;
                   float wclip_lo[4] = {0.f, 0.f, 0.f, 0.f}, wclip_hi[4] = {0.f, 0.f, 0.f, 0.f};
};
struct PreviewLine { std::vector<float> pts; uint8_t rgba[4]; float width_m; bool ghost = false; };
struct SilhouetteStyle { float rgba[4]; };
extern const SilhouetteStyle kSilhouetteGray;
extern const float kPanelBacking[4];
extern const float kPanelBackingWhite[4];
float screen_aspect(const fc_frame &f);
struct OverlayContext {
    ar_pose head_table;
    float units_per_meter, ground_y;
    float half_x, half_z;
    float dt_s;
    float decal_pull = 0.f;
};
void overlay_axes(uint8_t flags, const float a[3], const ar_pose &head, float r[3], float u[3], float b[3]);
// Only the draws the cut can reach take the cutaway (discard) pipelines.
void mark_cutaway(const fc_frame &f, const LightingFrame *light, const float eye[3], std::vector<DrawPacket> &packets);
bool cutaway_reaches(const float eye[3], const float target[3], float cut_radius, const float c[3], float rad);
// With async pipelines draw_world queues them in the background, so a wall the cone reaches later is cut at once.
std::vector<DrawPacket> cutaway_warm_draws(const fc_frame &f, const LightingFrame *light, const std::vector<DrawPacket> &packets);
// Each draw that writes depth gets a depth-only copy first (kDepthOnlyPrepass), then every colour draw is drawn at 1 -
// clear of its share, so the room behind shows through but none of the level's own farther surfaces do.
void apply_clear_fade(std::vector<DrawPacket> &packets, float clear);
struct ClearWarmKey {
    PipeIdentity id; uint8_t writes = 0; uint32_t samples = 1;
    bool operator==(const ClearWarmKey &o) const { return id == o.id && writes == o.writes && samples == o.samples; }
};
struct ClearWarmKeyHash { size_t operator()(const ClearWarmKey &k) const; };
using ClearWarmSet = std::unordered_set<ClearWarmKey, ClearWarmKeyHash>;
std::vector<DrawPacket> clear_fade_warm_draws(const LightingFrame *light, const std::vector<DrawPacket> &packets,
                                              ClearWarmSet *seen = nullptr, uint32_t samples = 1);
class Renderer {
public:
    explicit Renderer(IRenderDevice &dev) : dev_(dev) {}
    // Last, every translucent draw without depth write is drawn again depth-only, so lava and water over nothing still
    // give the compositor a depth.
    void draw_world(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, float decal_pull,
                    const float *clear_color, const SilhouetteStyle *silhouette = nullptr,
                    const LightingFrame *light = nullptr);
    void draw_table_shadow(uint32_t target, const fc_frame &f, const FrameView *level_views, uint32_t n, const LightingFrame &lf,
                           const TableShadowBox &box);
    void draw_overlay_panel(uint32_t target, const fc_frame &f, Viewport viewport);
    // WORLD_POINT groups draw in fc_frame group-array order (the order the adapter first set each id that frame); each
    // group's depth rectangle is written after all colour draws.
    void draw_overlays(uint32_t target, const fc_frame &f, const FrameView *table_views, uint32_t n, const OverlayContext &ctx);
    void set_interp(float alpha) { interp_alpha_ = alpha; }
    void set_fine_textures(bool on) { dev_.set_fine_textures(on); }
    void hide_panel() { panel_shown_ = false; }
    void set_panel_backing(const float *rgba);
    void draw_polylines(uint32_t target, const fc_frame &f, const FrameView *level_views, uint32_t n,
                        const float head_level[3], float units_per_meter);
    void draw_lines(uint32_t target, const std::vector<PreviewLine> &lines, const FrameView *views, uint32_t n, const float head[3]);
    static constexpr float kGhostAlpha = 0.35f, kGhostWidth = 0.8f;
    void draw_points(uint32_t target, const std::vector<DebugPoint> &points, const FrameView *views, uint32_t n, const float head[3]);
    static fc_combiner_key ribbon_combiner(bool blend);
    void warm_ribbon_pipelines(uint32_t samples = 1);
    static std::vector<DrawPacket> fixed_pipeline_draws();
    // ids from 0xFFFFFF00 up are the renderer's, never an adapter's
    static constexpr uint32_t kGameViewTexture = 0xFFFFFF01u;
    static constexpr uint32_t kGameViewHeight = 480;
    static constexpr uint32_t kGameViewReleaseFrames = 30;
    uint32_t game_view_target() const { return gv_target_; }
    uint64_t game_view_renders() const { return gv_renders_; }
    bool game_view_resident() const { return gv_resident_; }
    uint64_t ao_bakes() const { return ao_bakes_; }
    AoVolume last_ao() const { return ao_last_; }
    double last_ao_cpu_ms() const { return ao_cpu_ms_; }
    uint64_t ao_failures() const { return ao_failures_; }
    uint64_t water_footprint_renders() const { return foot_renders_; }
private:
    void encode_overlay(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const ar_mat4 &model, const float box[4],
                        const float *backing, bool on_top, const float *depth_box = nullptr, bool ink = false);
    void encode_overlay_depth(uint32_t target, const FrameView *views, uint32_t n);
    void draw_game_view(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const OverlayContext &ctx,
                        const fc_overlay_group &g);
    void append_ribbon(const float *pts, uint32_t count, const uint8_t rgba[4], float half_width, const float head[3], bool dashed);
    void encode_ribbons(uint32_t target, const DrawPacket *packets, uint32_t np, const FrameView *views, uint32_t n);
    void append_strip(const float *pts, uint32_t count, const uint8_t rgba[4], float half_width, const float head[3]);
    struct DepthQuad { ar_mat4 model; float box[4]; };
    std::vector<DepthQuad> depth_quads_;
    IRenderDevice &dev_;
    std::vector<DrawPacket> packets_;
    ClearWarmSet clear_warmed_;
    ShadowDirHold shadow_dir_;
    AoSchedule ao_sched_;
    uint64_t ao_bakes_ = 0;
    AoVolume ao_last_{};
    double ao_cpu_ms_ = 0.0;
    uint64_t ao_failures_ = 0;
    uint64_t foot_key_ = 0, foot_renders_ = 0;
    bool foot_have_ = false;
    std::vector<DrawPacket> sel_packets_;
    std::vector<fc_draw> sel_;
    std::vector<float> fades_;
    std::vector<fc_vertex> ribbon_;
    ar_panel_state panel_{};
    bool panel_shown_ = false;
    float backing_[4] = {kPanelBacking[0], kPanelBacking[1], kPanelBacking[2], kPanelBacking[3]};
    bool backing_on_ = true;
    float interp_alpha_ = 1.f;
    uint32_t gv_target_ = 0;
    uint64_t gv_renders_ = 0;
    bool gv_resident_ = false, gv_have_ = false, gv_drawn_ = false;
    uint32_t gv_idle_ = 0, gv_w_ = 0, gv_h_ = 0, gv_samples_ = 0;
    uint64_t gv_seq_ = 0;
    float gv_clip_[16] = {};
    uint8_t gv_clear_[4] = {};
};
}

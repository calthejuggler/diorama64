#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "combiner.h"
#include "fc_frame.h"
#include "lighting.h"
#include "shadow_fit.h"
namespace ar {
// Decal draws (FC_RS_DECAL) get this slope-scaled depth bias toward the viewer by default (metal_device.mm).
// Slope-scaled bias is per pixel, so its world size is metric: the AR layer lowers it for a level drawn smaller than its
// reference scale (ar_layer.h decal_slope_scale).
constexpr float kDecalSlopeScale = 0.5f;
enum : uint8_t { kSilhouetteNone = 0, kSilhouetteFlat = 1, kSilhouetteCutout = 2 };
struct ViewConstants {
    float clip_from_model[16];
    float eye_model[3];
    float decal_pull;
    float noise_seed;
    float eye_view[3];
    uint32_t layer = 0;
    float decal_slope = kDecalSlopeScale;
    // world clip (level units): wclip_lo.w = 1 clips below y = wclip_lo.y; wclip_hi.w = 1 clips outside the XZ box
// wclip_lo.xz..wclip_hi.xz. Zero (off) for every pass but the main level views.
    float wclip_lo[4] = {0.f, 0.f, 0.f, 0.f}, wclip_hi[4] = {0.f, 0.f, 0.f, 0.f};
    float interp_alpha = 1.f;
    uint32_t interp_on = 0;
};
struct DepthExtras {
    const float *prev_pos = nullptr; float interp_alpha = 1.f;
    float wclip_lo[4] = {0.f, 0.f, 0.f, 0.f}, wclip_hi[4] = {0.f, 0.f, 0.f, 0.f};
};
struct DrawPacket {
    uint32_t first_vertex, vertex_count;
    uint32_t tex[2];
    fc_sampler samp[2];
    fc_combiner_key cc;          // normalized
    float prim[4], env[4];
    float lod_frac;
    uint8_t depth_test, depth_write, decal, blend, cull;
    uint8_t focus;
    uint8_t silhouette;
    float fade = 1.f;
    uint8_t clip_on = 0;
    float clip[4] = {0.f, 0.f, 0.f, 0.f};
    uint8_t depth_only = 0;
    uint8_t light_flags = 0;
    uint8_t light_role = 0;
    uint8_t table_shadow = 0;
    float pull = 0.f;            // C: a silhouette's clearance: its depth is taken this many level units nearer the
                                 // centre eye (its x, y stay); 0 = none. Never on a decal draw.
    uint8_t cutaway = 0;         // the cutaway pipeline variant (static opaque terrain of a frame with a look); the
                                 // effect itself waits for LightingFrame::cut
    uint8_t ink = 0;
    uint8_t faded = 0;           // blend is the fading WORLD draw's alone (opaque as captured)
    float trans_alpha = 1.f;     // a clear transition's share of this WORLD draw (multiplies the fragment's alpha
                                 // after the cut: the cutaway grows with fade alone, never with the transition)
};
// the DrawPacket fields a pipeline takes, in one place. metal_device.mm's PipeKey is this plus the
// target (format, depth, samples) and the derived alpha-to-coverage. The renderer's warm lists dedup on it, so a warm list
// is never narrower than the key (a missing field would leave a variant cold).
struct PipeIdentity {
    uint64_t slots = 0;
    uint32_t opts = 0;
    uint8_t blend = 0, silhouette = 0, depth_only = 0, table_shadow = 0, clip = 0, cut = 0;
    bool operator==(const PipeIdentity &o) const {
        return slots == o.slots && opts == o.opts && blend == o.blend && silhouette == o.silhouette &&
               depth_only == o.depth_only && table_shadow == o.table_shadow && clip == o.clip && cut == o.cut;
    }
};
inline PipeIdentity pipe_identity(const DrawPacket &d) {
    PipeIdentity k;
    k.slots = d.cc.slots; k.opts = d.cc.opts; k.blend = d.blend; k.silhouette = d.silhouette;
    k.depth_only = d.depth_only; k.table_shadow = d.table_shadow; k.clip = d.clip_on ? 1 : 0; k.cut = d.cutaway ? 1 : 0;
    return k;
}
struct Viewport { float x, y, w, h; };   // pixels, origin top-left
struct PassDesc { bool clear; float clear_color[4]; bool depth; Viewport viewport; };
// linearize: the destination is an sRGB format, so write linear premultiplied values and let the hardware re-encode.
struct ExternalView {
    void *color = nullptr;      // id<MTLTexture>, unretained (2D or 2D array)
    void *depth = nullptr;
    uint32_t slice = 0;
    Viewport viewport{0.f, 0.f, 0.f, 0.f};
    uint32_t src_x = 0, src_y = 0;
    uint32_t src_layer = 0;
    bool linearize = false;
};
enum class TextureFilter { vanilla, smooth };
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual void upload_texture(uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba) = 0;
    virtual void set_edge_coverage(bool on) = 0;
    virtual void set_texture_filter(TextureFilter f) = 0;
    // Fine textures (any time between draws): anisotropy 16 instead of 4 from the next draw, and mip chains averaged in
    // linear light from the next begin_frame; only with the smooth filter.
    static constexpr uint32_t kFineMaxTexels = 1u << 20;
    virtual void set_fine_textures(bool on) { (void)on; }
    virtual uint32_t anisotropy() const { return 0; }
    virtual bool wait_mips_ready(double timeout_s) { (void)timeout_s; return true; }
    struct MipCounts { uint64_t kernel = 0, blit = 0; size_t waiting = 0; };
    virtual MipCounts mip_counts() const { return {}; }
    virtual bool read_texture_level(uint32_t tex_id, uint32_t level, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) {
        (void)tex_id; (void)level; (void)rgba; (void)w; (void)h; return false;
    }
    // With samples > 1 the passes draw into multisample attachments resolved at the end of each encoder: colour
    // averaged, depth the nearest sample (reverse-Z: the largest), stencil discarded.
    virtual uint32_t create_target(uint32_t w, uint32_t h, uint32_t layers = 1, uint32_t samples = 1) = 0;
    virtual void resize_target(uint32_t target, uint32_t w, uint32_t h, uint32_t layers = 1, uint32_t samples = 1) = 0;
    // 1 for a layered target where the GPU cannot rasterize multisample layers (the visionOS simulator's validation
    // rejects it).
    virtual uint32_t supported_samples(uint32_t wanted, uint32_t layers = 1) const = 0;
    virtual uint32_t target_samples(uint32_t target) const = 0;
    // picture-in-picture: from now on a draw whose texture id is tex_id samples target's resolved colour (a 2D, one-slice
// target) instead of an uploaded texture; target 0 removes the binding. tex_id must be an id no adapter uploads. A pass
// recorded with such a draw first encodes the target's recorded passes, so the draw samples what they drew this frame.
    virtual void bind_target_texture(uint32_t tex_id, uint32_t target) = 0;
    // Call it only between frames, or before any pass of this frame sampled the target.
    virtual void release_target(uint32_t target) = 0;
    // Multisample attachments are memoryless (tile memory only, Apple GPUs): they cannot be loaded by a second encoder.
    virtual bool memoryless_msaa() const = 0;
    // Only the target's own passes are rate-mapped; their viewports are in the map's screen pixels and encode_resolve
    // copies physical texels 1:1.
    virtual void set_rate_map(uint32_t target, void *map) = 0;
    virtual void begin_frame() = 0;
    // A target's passes are recorded in order and encoded together in one render encoder at the first point that needs
    // the target (encode_resolve, encode_composite or end_frame); each pass keeps the state it was recorded with.
    virtual void encode_pass(uint32_t target, const PassDesc &pass, const ViewConstants &view,
                             const fc_vertex *verts, uint32_t vert_count, const DrawPacket *draws, uint32_t draw_count,
                             const int8_t *normals = nullptr, const float *prev_pos = nullptr) = 0;
    virtual void set_lighting(const LightingFrame *lf) = 0;
    virtual void encode_underside_map(const UndersideDesc &u, const fc_vertex *verts, uint32_t nv, const DrawPacket *draws,
                                      uint32_t nd, const DepthExtras &x = DepthExtras{}) = 0;
    virtual void encode_water_footprint(const float window[4], const float clip_from_level[16], const fc_vertex *verts,
                                        uint32_t nv, const DrawPacket *draws, uint32_t nd) {
        (void)window; (void)clip_from_level; (void)verts; (void)nv; (void)draws; (void)nd;
    }
    virtual bool reuse_water_footprint() { return false; }
    virtual void encode_shadow_map(const ShadowFit &fit, const fc_vertex *verts, uint32_t nv, const DrawPacket *draws,
                                   uint32_t nd, const DepthExtras &x = DepthExtras{}) = 0;
    virtual bool bake_ao(uint32_t slot, const AoVolume &vol, const std::vector<ShadowFit> &views, float radius,
                         const fc_vertex *verts, uint32_t nv, const DrawPacket *draws, uint32_t nd,
                         const DepthExtras &x = DepthExtras{}) = 0;
    virtual void prepare_ao() {}
    virtual bool ao_bake_time(uint64_t *bake, double *ms) const { (void)bake; (void)ms; return false; }
    virtual void encode_environment_reduce(void *cube_texture, uint32_t generation) = 0;
    virtual bool environment_stats(EnvStats *out, uint32_t *generation) const = 0;
    virtual const char *environment_error() const { return nullptr; }
    // The caller never copies into a slot that a frame still being built shows.
    static constexpr uint32_t kEnvCopySlots = 3;
    virtual void *copy_environment(void *cube_texture, uint32_t slot) { (void)cube_texture; (void)slot; return nullptr; }
    virtual void encode_composite(uint32_t dst, void *external_texture, uint32_t under, uint32_t over) = 0;
    virtual void end_frame(void (*on_gpu_done)(void *), void *ctx, void *present_drawable) = 0;
    virtual void wait_idle() = 0;
    virtual double last_gpu_ms() const = 0;
    virtual uint32_t render_encoders_last_frame() const = 0;
    virtual uint64_t late_passes() const = 0;
    virtual bool read_target(uint32_t target, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) = 0;
    virtual bool compile_combiner(const fc_combiner_key &key, std::string *error, uint32_t samples = 1) = 0;
    virtual void encode_resolve(uint32_t src_target, const ExternalView &dst) = 0;
    virtual void *command_buffer() = 0;
    virtual bool read_target_depth(uint32_t target, std::vector<float> &depth, uint32_t &w, uint32_t &h) = 0;
    virtual std::string pipeline_list() const = 0;
    virtual size_t warm_pipelines(const std::string &list, uint32_t at_samples = 0) = 0;
    virtual size_t warm_pipelines_background(const std::string &list, const std::vector<DrawPacket> &draws, uint32_t samples) = 0;
    virtual size_t pipelines_warming() = 0;
    // For tests and tools: the render thread never waits.
    virtual bool wait_pipelines_warmed(double timeout_s) { (void)timeout_s; return pipelines_warming() == 0; }
    virtual void hold_background_warm(bool hold) { (void)hold; }
    virtual uint32_t background_warm_peak() const { return 0; }
    virtual uint64_t warm_compiles() const { return 0; }
    virtual uint64_t warm_promotions() const { return 0; }
    virtual void hold_promoted_warm(bool hold) { (void)hold; }
    virtual void hold_warm_compiles(bool hold) { (void)hold; }
    virtual size_t draw_time_builds() const = 0;
    // With async on, a draw (or resolve) whose pipeline is not built never compiles it on the calling thread: it is
    // queued, the draw is skipped and counted.
    virtual void set_async_pipelines(bool on) { (void)on; }
    virtual bool async_pipelines() const { return false; }
    virtual uint64_t deferred_draws() const { return 0; }
    virtual bool take_deferred_miss(std::string *key) { (void)key; return false; }
    // The fixed passes' compute and depth pipelines (shadow, underside and AO depth maps, the AO fill, the reflections'
    // reduction) compile off the render thread from here on; their first use waits for them. Without it they compile at
    // their first use (a look change that turns them on mid-play).
    virtual void prepare_passes() {}
    // A refused multisample pipeline also turns multisampling off: supported_samples returns 1 from then on.
    virtual bool take_pipeline_error(std::string *why) = 0;
    virtual size_t pipeline_count() const = 0;
};
}

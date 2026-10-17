#import <Metal/Metal.h>
#include "metal_device.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "combiner.h"
#include "msl_emitter.h"
#include "pipeline_warm.h"

namespace ar {
namespace {
// reverse-Z compares GreaterEqual, so toward the viewer is positive. Slope-scaled bias is per-pixel depth slope: 1.0
// leaked through a 3 mm (24-unit) occluder, 0.5 (kDecalSlopeScale) does not.
struct DrawU { float prim[4]; float env[4]; float lod; float decal; float noise_seed; float fade; float clip[4]; float clip_on; uint32_t lflags;
               float pull; float talpha; };
static_assert(sizeof(DrawU) == 80, "DrawU matches the MSL struct (float4 clip at 48)");
struct LightU {
    float dir_on[4]; float tint_amb[4]; float contact[4]; float under_uv[16]; float under_range[4];
    float shadow[4]; float shadow_uv[16]; float cast[4];
    float relight[4];
    float ao[4];
    float ao_min[4];
    float ao_inv[4];
    float toy[4];
    float env0[4], env1[4], env2[4];
    float env3[4], env4[4], env5[4];
    float liquid[4];
    float liquid2[4];
    float water_box[8][4];
    float water_y[2][4];
    float water_foot[4];
    float cut[4]; float cut2[4];
    float trans[4]; float trans_rgb[4]; float trans_c[4];
    float tex[4];
};
static_assert(sizeof(LightU) == 704, "LightU matches the MSL struct");   // matches the MSL struct
struct ViewU { float clip_from_model[16]; float eye_pull[4]; float eye_view[4]; uint32_t layer, pad[3]; float wclip_lo[4]; float wclip_hi[4];
               float interp[4]; };
static_assert(sizeof(ViewU) == 160, "ViewU matches the MSL struct (interp at 144)");
const MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float_Stencil8;
const size_t kMaxWarmLines = 4096;   // warm_pipelines reads at most this many lines of a pipeline list
const size_t kMaxWarmLineBytes = 96;  // and skips longer lines unparsed (pipeline_list's lines fit its 96-byte buffer)
// Stencil low bits mark where a focus owner is the front-most opaque surface; the silhouette pass draws the owner where
// it is hidden and the bits are still 0, then bumps them, so each hidden pixel blends once. Bit 0x80 marks where a draw
// wrote depth.
enum StencilMode : uint8_t { kStencilKeep = 0, kStencilMarkFocus = 1, kStencilClearFocus = 2, kStencilSilhouette = 3, kStencilFaint = 4 };
const uint32_t kStencilDepthWritten = 0x80, kStencilFocusBits = 0x7F;
uint8_t stencil_mode(const DrawPacket &d) {
    if (d.silhouette) return kStencilSilhouette;
    if (d.depth_only == kDepthOnlyFaint || d.table_shadow) return kStencilFaint;
    if (!d.depth_write) return kStencilKeep;
    return d.focus ? kStencilMarkFocus : kStencilClearFocus;
}
uint32_t stencil_ref(uint8_t mode) {
    switch (mode) {
    case kStencilMarkFocus: return kStencilDepthWritten | 1u;
    case kStencilClearFocus: return kStencilDepthWritten;
    default: return 0u;
    }
}

const char *kCompositeMsl = R"(#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; };
vertex VOut cvert(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    VOut o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); return o;
}
fragment float4 cfrag(VOut in [[stage_in]], texture2d<float> under [[texture(0)]], texture2d<float> over [[texture(1)]]) {
    uint2 p = uint2(in.pos.xy);
    float4 o = over.read(p);
    float4 u = under.read(p);
    return o + u * (1.0 - o.a);
}
)";
// Alpha-weighted mean of the 2 x 2 parents in linear light, so mips neither darken nor take a dark fringe from clear
// texels.
const char *kMipMsl = R"(#include <metal_stdlib>
using namespace metal;
static float3 to_linear(float3 c) { return select(pow((c + 0.055) / 1.055, float3(2.4)), c / 12.92, c <= 0.04045); }
static float3 to_srgb(float3 c) { return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c <= 0.0031308); }
kernel void mip_gamma(texture2d<float, access::read> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]],
                      uint2 g [[thread_position_in_grid]]) {
    if (g.x >= dst.get_width() || g.y >= dst.get_height()) return;
    const uint2 sz = uint2(src.get_width(), src.get_height()), last = sz - 1;
    const uint nx = (sz.x > 1 && (sz.x & 1) != 0 && g.x == dst.get_width() - 1) ? 3 : 2;
    const uint ny = (sz.y > 1 && (sz.y & 1) != 0 && g.y == dst.get_height() - 1) ? 3 : 2;
    float3 sum = float3(0.0), plain = float3(0.0);
    float a = 0.0;
    for (uint j = 0; j < ny; j++) for (uint i = 0; i < nx; i++) {
        const float4 c = src.read(min(g * 2 + uint2(i, j), last));
        const float3 l = to_linear(c.rgb);
        sum += l * c.a; plain += l; a += c.a;
    }
    const float n = float(nx * ny);
    const float3 l = a > 0.0 ? sum / a : plain / n;
    dst.write(float4(to_srgb(saturate(l)), a / n), g);
}
)";
const char *kEnvMsl = R"(#include <metal_stdlib>
using namespace metal;
constant int kN = 24;
static float3 face_dir(int f, float u, float v) {   // runtime/render/common/room_light.cpp cube_face_dir
    switch (f) {
        case 0: return float3(1.0, -v, -u);
        case 1: return float3(-1.0, -v, u);
        case 2: return float3(u, 1.0, v);
        case 3: return float3(u, -1.0, -v);
        case 4: return float3(u, -v, 1.0);
        default: return float3(-u, -v, -1.0);
    }
}
kernel void env_reduce(texturecube<float> env [[texture(0)]], device float *out [[buffer(0)]],
                       uint tid [[thread_index_in_threadgroup]], uint nt [[threads_per_threadgroup]]) {   // nt <= 256
    constexpr sampler s(filter::linear, mip_filter::linear, address::clamp_to_edge);
    threadgroup float acc[256][7];
    float a[7] = {0, 0, 0, 0, 0, 0, 0};
    const float lod = max(0.0, log2(float(env.get_width()) / float(kN)));
    for (uint i = tid; i < uint(6 * kN * kN); i += nt) {
        const int f = int(i) / (kN * kN), r = int(i) % (kN * kN);
        const float u = (float(r % kN) + 0.5) / float(kN) * 2.0 - 1.0, v = (float(r / kN) + 0.5) / float(kN) * 2.0 - 1.0;
        const float3 d = face_dir(f, u, v);
        const float w = 1.0 / pow(dot(d, d), 1.5);                 // the texel's solid angle
        const float3 n = normalize(d);
        const float3 c = max(env.sample(s, n, level(lod)).rgb, float3(0.0));
        const float lum = dot(c, float3(0.2290, 0.6917, 0.0793));  // Display P3 luminance
        a[0] += w * c.r; a[1] += w * c.g; a[2] += w * c.b;
        a[3] += w * lum * n.x; a[4] += w * lum * n.y; a[5] += w * lum * n.z; a[6] += w;
    }
    for (int k = 0; k < 7; k++) acc[tid][k] = a[k];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0) {
        float t[7] = {0, 0, 0, 0, 0, 0, 0};
        for (uint j = 0; j < nt; j++) for (int k = 0; k < 7; k++) t[k] += acc[j][k];
        for (int k = 0; k < 6; k++) out[k] = t[k] / t[6];
    }
}
)";
const char *kResolveMsl = R"(#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; };
// src_last: the view's last source pixel; src_layer: the source target's slice (the _a variants read 2D-array targets).
struct RU { float2 dst_origin; uint2 src_origin; uint2 src_last; uint linearize; uint src_layer; };
vertex VOut rvert(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    VOut o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); return o;
}
static uint2 source_pixel(VOut in, constant RU &u, uint w, uint h) {
    return min(uint2(in.pos.xy - u.dst_origin) + u.src_origin, min(u.src_last, uint2(w - 1, h - 1)));
}
static float4 output(float4 c, constant RU &u) {
    if (u.linearize != 0 && c.a > 0.0) {   // un-premultiply, sRGB -> linear, premultiply again
        float3 g = saturate(c.rgb / c.a);
        c.rgb = select(pow((g + 0.055) / 1.055, float3(2.4)), g / 12.92, g <= 0.04045) * c.a;
    }
    return c;
}
struct ROut { float4 color [[color(0)]]; float depth [[depth(any)]]; };
fragment ROut rfrag(VOut in [[stage_in]], texture2d<float> src [[texture(0)]], depth2d<float> srcd [[texture(1)]], constant RU &u [[buffer(0)]]) {
    const uint2 p = source_pixel(in, u, src.get_width(), src.get_height());
    ROut o; o.color = output(src.read(p), u); o.depth = srcd.read(p); return o;
}
fragment float4 rfrag_color(VOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant RU &u [[buffer(0)]]) {
    return output(src.read(source_pixel(in, u, src.get_width(), src.get_height())), u);
}
fragment ROut rfrag_a(VOut in [[stage_in]], texture2d_array<float> src [[texture(0)]], depth2d_array<float> srcd [[texture(1)]],
                      constant RU &u [[buffer(0)]]) {
    const uint2 p = source_pixel(in, u, src.get_width(), src.get_height());
    ROut o; o.color = output(src.read(p, u.src_layer), u); o.depth = srcd.read(p, u.src_layer); return o;
}
fragment float4 rfrag_color_a(VOut in [[stage_in]], texture2d_array<float> src [[texture(0)]], constant RU &u [[buffer(0)]]) {
    return output(src.read(source_pixel(in, u, src.get_width(), src.get_height()), u.src_layer), u);
}
)";
const char *kDepthMsl = R"(#include <metal_stdlib>
using namespace metal;
struct DIn { float3 pos [[attribute(0)]]; float2 uv0 [[attribute(1)]]; };
struct DOut { float4 pos [[position]]; float2 uv0; };                                  // dcut's [[stage_in]]
// dmain's return: DOut plus the world clip distances (a [[stage_in]] struct cannot hold them; dcut does not consume them)
struct DOutV { float4 pos [[position]]; float2 uv0; float clip_distance [[clip_distance]] [5]; };
// interp x alpha, y 1 = interpolate (prevp at buffer 3); world clip as ViewU (lo.w floor on, hi.w XZ box on)
struct DepthU { float4 interp; float4 wclip_lo; float4 wclip_hi; };
vertex DOutV dmain(DIn in [[stage_in]], constant float4x4 &m [[buffer(1)]], constant DepthU &u [[buffer(2)]],
                   device const packed_float3 *prevp [[buffer(3)]], uint vid [[vertex_id]]) {
    const float3 pos = u.interp.y > 0.5 ? mix(float3(prevp[vid]), in.pos, u.interp.x) : in.pos;
    DOutV o; o.pos = m * float4(pos, 1.0); o.uv0 = in.uv0;
    const float off = 1.0;
    o.clip_distance[0] = u.wclip_lo.w > 0.5 ? pos.y - u.wclip_lo.y : off;
    const bool box = u.wclip_hi.w > 0.5;
    o.clip_distance[1] = box ? pos.x - u.wclip_lo.x : off;
    o.clip_distance[2] = box ? u.wclip_hi.x - pos.x : off;
    o.clip_distance[3] = box ? pos.z - u.wclip_lo.z : off;
    o.clip_distance[4] = box ? u.wclip_hi.z - pos.z : off;
    return o;
}
fragment void dcut(DOut in [[stage_in]], texture2d<float> t0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    if (t0.sample(s0, in.uv0).a <= 0.3) { discard_fragment(); }
}
)";
const char *kAoMsl = R"(#include <metal_stdlib>
using namespace metal;
struct AoU { float4 min_voxel; uint4 dims_n; float4 range; };   // xyz min, w voxel; xyz dims, w view count; x radius
// A view occludes a voxel when the nearest surface towards the sky lies in front of it by more than a small bias (0.002 of
// the map's depth) and less than the radius (level units): AO, not the whole level's sky.
kernel void ao_fill(depth2d_array<float> maps [[texture(0)]], texture3d<float, access::write> out [[texture(1)]],
                    constant AoU &u [[buffer(0)]], constant float4x4 *uv [[buffer(1)]], uint3 id [[thread_position_in_grid]]) {
    if (any(id >= u.dims_n.xyz)) { return; }
    const float3 p = u.min_voxel.xyz + float3(id) * u.min_voxel.w;
    const uint w = maps.get_width(), h = maps.get_height();
    float seen = 0.0;
    for (uint j = 0; j < u.dims_n.w; j++) {
        const float4 q = uv[j] * float4(p, 1.0);
        const uint2 px = uint2(clamp(q.xy / q.w, 0.0, 0.9999) * float2(w, h));
        const float per_unit = length(float3(uv[j][0].z, uv[j][1].z, uv[j][2].z));   // depth per level unit (orthographic)
        const float ahead = q.z / q.w - maps.read(px, j);                            // depth from the occluder to the voxel
        seen += (ahead <= 0.002 || ahead >= u.range.x * per_unit) ? 1.0 : 0.0;
    }
    out.write(float4(seen / float(u.dims_n.w)), id);
}
)";
const uint32_t kAoMapSize = 512;
const uint32_t kAoMaxViews = 32;
const uint32_t kUndersideSize = 256;
const uint32_t kFootprintSize = 512;
const uint32_t kShadowSize = 2048;

struct Rec {
    PassDesc pass{};
    ViewConstants view{};
    LightU light{};
    id<MTLBuffer> vb, nb, pb;
    NSUInteger voff = 0, noff = 0, poff = 0;
    uint32_t nv = 0;
    id<MTLTexture> shadow, under, ao, env, env_prev, foot;   // fragment textures 2, 5, 3, 4, 6, 7
    std::vector<DrawPacket> draws;
    // The game textures of each draw (2 per draw) as they were at record time. Unretained: textures_ holds them, and one an
    // upload replaces while the frame is open is kept in MetalDevice::retired_ until the frame is encoded.
    std::vector<__unsafe_unretained id<MTLTexture>> tex;
};
struct Target {
    id<MTLTexture> color; id<MTLTexture> depth;
    id<MTLTexture> ms_color, ms_depth;
    bool memoryless = false;
    uint32_t w = 0, h = 0, layers = 1, samples = 1;
    std::vector<Rec> recs;
    size_t nrec = 0;
    bool encoded = false;
    id<MTLRasterizationRateMap> rate_map;
    uint32_t screen_w = 0, screen_h = 0;
};
struct PipeKey {
    uint64_t slots; uint32_t opts; uint32_t fmt; uint8_t blend; uint8_t depth; uint8_t flat; uint8_t depth_only; uint8_t table_shadow;
    uint8_t clip;
    uint8_t samples;
    uint8_t a2c;
    uint8_t cut;
    bool operator<(const PipeKey &o) const {
        return std::tie(slots, opts, fmt, blend, depth, flat, depth_only, table_shadow, clip, samples, a2c, cut) <
               std::tie(o.slots, o.opts, o.fmt, o.blend, o.depth, o.flat, o.depth_only, o.table_shadow, o.clip, o.samples, o.a2c, o.cut);
    }
};
MTLSamplerAddressMode address(uint8_t w) {
    return w == FC_WRAP_CLAMP ? MTLSamplerAddressModeClampToEdge : w == FC_WRAP_MIRROR ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat;
}

class MetalDevice final : public IRenderDevice {
public:
    MetalDevice(id<MTLDevice> d, bool shared) : dev_(d ? d : MTLCreateSystemDefaultDevice()), shared_(shared) {
        queue_ = [dev_ newCommandQueue];
        inflight_ = dispatch_semaphore_create(3);
        vdesc_ = [MTLVertexDescriptor vertexDescriptor];
        const MTLVertexFormat fmts[4] = {MTLVertexFormatFloat3, MTLVertexFormatFloat2, MTLVertexFormatFloat2, MTLVertexFormatUChar4Normalized};
        const NSUInteger offs[4] = {0, 12, 20, 28};
        for (int i = 0; i < 4; i++) { vdesc_.attributes[i].format = fmts[i]; vdesc_.attributes[i].offset = offs[i]; vdesc_.attributes[i].bufferIndex = 0; }
        vdesc_.layouts[0].stride = 32;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        white_ = [dev_ newTextureWithDescriptor:td];
        const uint8_t w[4] = {255, 255, 255, 255};
        [white_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:w bytesPerRow:4];
        NSError *e = nil;
        composite_lib_ = [dev_ newLibraryWithSource:@(kCompositeMsl) options:nil error:&e];
        if (!composite_lib_) std::fprintf(stderr, "composite shader: %s\n", e.localizedDescription.UTF8String);
        e = nil;
        resolve_lib_ = [dev_ newLibraryWithSource:@(kResolveMsl) options:nil error:&e];
        if (!resolve_lib_) std::fprintf(stderr, "resolve shader: %s\n", e.localizedDescription.UTF8String);
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear; sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge; sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        linear_clamp_ = [dev_ newSamplerStateWithDescriptor:sd];
        // The underside map's stand-in until a table shadow renders one: 1x1, depth 1 ("nothing above the table"). Private
        // depth storage cannot be written from the CPU: cleared by a depth pass in the first frame (ensure_placeholders).
        MTLTextureDescriptor *pd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:1 height:1 mipmapped:NO];
        pd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        pd.storageMode = MTLStorageModePrivate;
        under_placeholder_ = [dev_ newTextureWithDescriptor:pd];
        shadow_placeholder_ = [dev_ newTextureWithDescriptor:pd];
        MTLSamplerDescriptor *cd = [MTLSamplerDescriptor new];
        cd.minFilter = MTLSamplerMinMagFilterLinear; cd.magFilter = MTLSamplerMinMagFilterLinear;
        cd.sAddressMode = MTLSamplerAddressModeClampToEdge; cd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        cd.compareFunction = MTLCompareFunctionLessEqual;
        compare_sampler_ = [dev_ newSamplerStateWithDescriptor:cd];
        MTLTextureDescriptor *ad = [MTLTextureDescriptor new];
        ad.textureType = MTLTextureType3D; ad.pixelFormat = MTLPixelFormatR8Unorm;
        ad.width = ad.height = ad.depth = 1;
        ad.usage = MTLTextureUsageShaderRead;
        ao_placeholder_ = [dev_ newTextureWithDescriptor:ad];
        const uint8_t one = 255;
        [ao_placeholder_ replaceRegion:MTLRegionMake3D(0, 0, 0, 1, 1, 1) mipmapLevel:0 slice:0 withBytes:&one bytesPerRow:1 bytesPerImage:1];
        MTLTextureDescriptor *ed = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float size:1 mipmapped:NO];
        ed.usage = MTLTextureUsageShaderRead;
        env_placeholder_ = [dev_ newTextureWithDescriptor:ed];
        const uint16_t black[4] = {0, 0, 0, 0x3c00};
        for (NSUInteger f = 0; f < 6; f++)
            [env_placeholder_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:f withBytes:black bytesPerRow:8 bytesPerImage:8];
    }
    ~MetalDevice() override {   // held warm-up jobs run out instead of waiting forever
        hold_background_warm(false); hold_promoted_warm(false); hold_warm_compiles(false);
    }
    id<MTLDevice> device() const { return dev_; }

    void set_edge_coverage(bool on) override { edge_ = on; }
    void set_texture_filter(TextureFilter f) override {
        if (f != filter_) { filter_ = f; samplers_.clear(); apply_fine(); }
    }
    void upload_texture(uint32_t tex_id, uint32_t w, uint32_t h, const uint8_t *rgba) override {
        if (!tex_id || !w || !h) return;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:(w > 1 || h > 1)];
        td.usage = MTLTextureUsageShaderRead;   // not ShaderWrite: the blit's chains differ (rounding) for writable textures
        td.storageMode = readable_storage();
        id<MTLTexture> t = [dev_ newTextureWithDescriptor:td];
        [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba bytesPerRow:(NSUInteger)w * 4];
        auto old = textures_.find(tex_id);
        if (old != textures_.end()) {
            pending_mips_.erase(std::remove(pending_mips_.begin(), pending_mips_.end(), old->second), pending_mips_.end());
            late_gamma_.erase(std::remove(late_gamma_.begin(), late_gamma_.end(), old->second), late_gamma_.end());
            late_blit_.erase(std::remove(late_blit_.begin(), late_blit_.end(), old->second), late_blit_.end());
        }
        if (t.mipmapLevelCount > 1) pending_mips_.push_back(t);
        if (frame_open_ && old != textures_.end()) retired_.push_back(old->second);   // recorded passes may still draw it
        textures_[tex_id] = t;   // a new object per upload: frames still in flight keep the old one alive
    }
    uint32_t create_target(uint32_t w, uint32_t h, uint32_t layers, uint32_t samples) override {
        targets_.emplace_back();
        make_target(targets_.back(), w, h, layers, samples);
        return (uint32_t)targets_.size();
    }
    void resize_target(uint32_t t, uint32_t w, uint32_t h, uint32_t layers, uint32_t samples) override {
        if (t == 0 || t > targets_.size()) return;
        Target &x = targets_[t - 1];
        if (x.w != std::max(w, 1u) || x.h != std::max(h, 1u) || x.layers != std::max(layers, 1u) ||
            x.samples != supported_samples(samples, layers))
            make_target(x, w, h, layers, samples);
    }
    uint32_t supported_samples(uint32_t wanted, uint32_t layers) const override {
        if (msaa_refused_ || (layers > 1 && !layered_msaa_)) return 1;
        for (uint32_t s : {4u, 2u}) if (s <= wanted && [dev_ supportsTextureSampleCount:s]) return s;
        return 1;
    }
    bool memoryless_msaa() const override { return [dev_ supportsFamily:MTLGPUFamilyApple4]; }
    uint32_t target_samples(uint32_t t) const override { return t == 0 || t > targets_.size() ? 1u : targets_[t - 1].samples; }
    void release_target(uint32_t t) override {
        if (t == 0 || t > targets_.size()) return;
        Target &x = targets_[t - 1];
        flush(x);
        x.color = nil; x.depth = nil; x.ms_color = nil; x.ms_depth = nil;
        x.w = 0; x.h = 0; x.samples = 1; x.memoryless = false;
    }
    void bind_target_texture(uint32_t tex_id, uint32_t target) override {
        if (!tex_id) return;
        if (target == 0 || target > targets_.size()) target_tex_.erase(tex_id);
        else target_tex_[tex_id] = target;
    }
    void set_rate_map(uint32_t target, void *map) override {
        if (target == 0 || target > targets_.size()) return;
        Target &t = targets_[target - 1];
        id<MTLRasterizationRateMap> m = (__bridge id<MTLRasterizationRateMap>)map;
        if (m == t.rate_map) return;
        flush(t);
        t.rate_map = m;
        t.screen_w = m ? (uint32_t)m.screenSize.width : 0;
        t.screen_h = m ? (uint32_t)m.screenSize.height : 0;
    }
    void begin_frame() override {
        collect_warmed();
        dispatch_semaphore_wait(inflight_, DISPATCH_TIME_FOREVER);
        frame_open_ = true;
        encoders_ = 0;
        for (Target &t : targets_) t.encoded = false;
        under_valid_ = false;
        shadow_valid_ = false;
        foot_valid_ = false;
        ring_index_ = (ring_index_ + 1) % 3;
        {
            Ring &rs = ring_[ring_index_];
            if (rs.buf && rs.buf.length > 4 * std::max<NSUInteger>(rs.total, 1u << 20)) rs.buf = nil;
            rs.used = 0;
            rs.total = 0;
        }
        for (StageMemo &m : memo_) m = StageMemo{};
        memo_next_ = 0;
        cmd_ = [queue_ commandBuffer];
        size_t gamma_n = 0, gamma_texels = 0;
        if (cmd_ && !pending_mips_.empty()) {
            id<MTLComputePipelineState> mp = fine_ ? mip_pipeline_if_ready() : nil;
            std::vector<id<MTLTexture>> gamma, blit;
            for (id<MTLTexture> t : pending_mips_) {
                if (mp && gamma_eligible(t) && within_gamma_budget(gamma_n, gamma_texels)) {
                    gamma.push_back(t); gamma_n++; gamma_texels += t.width * t.height;
                } else {
                    blit.push_back(t);
                }
            }
            if (!gamma.empty()) encode_gamma_mips(mp, gamma);
            if (!blit.empty()) {
                encode_blit_mips(blit);
                if (fine_ && !mip_failed_)
                    for (id<MTLTexture> t : blit) if (gamma_eligible(t)) late_gamma_.push_back(t);
            }
            pending_mips_.clear();
        }
        if (cmd_ && !late_blit_.empty()) {
            size_t n = 0;
            while (n < late_blit_.size() && within_gamma_budget(gamma_n, gamma_texels)) {
                gamma_texels += late_blit_[n].width * late_blit_[n].height; gamma_n++; n++;
            }
            encode_blit_mips(std::vector<id<MTLTexture>>(late_blit_.begin(), late_blit_.begin() + n));
            late_blit_.erase(late_blit_.begin(), late_blit_.begin() + n);
        }
        if (cmd_ && fine_ && !late_gamma_.empty() && within_gamma_budget(gamma_n, gamma_texels)) {
            if (id<MTLComputePipelineState> mp = mip_pipeline_if_ready()) {
                size_t n = 0;
                while (n < late_gamma_.size() && within_gamma_budget(gamma_n, gamma_texels)) {
                    gamma_texels += late_gamma_[n].width * late_gamma_[n].height; gamma_n++; n++;
                }
                encode_gamma_mips(mp, std::vector<id<MTLTexture>>(late_gamma_.begin(), late_gamma_.begin() + n));
                late_gamma_.erase(late_gamma_.begin(), late_gamma_.begin() + n);
            }
        }
    }
    // With async pipelines the render thread never waits for the gamma kernel and re-mips a bounded number of textures
    // per frame; without (the harness, tests) it waits and re-mips at once.
    static constexpr size_t kGammaTextures = 64, kGammaTexels = 4u << 20;
    bool within_gamma_budget(size_t count, size_t texels) const {
        return !async_ || count == 0 || (count < kGammaTextures && texels < kGammaTexels);
    }
    // the hard per-texture cap (a 4096^2 pack texture would need ~90 MB of scratch): larger textures keep their
// blit chains, so one frame's kernel work stays under kGammaTexels + kFineMaxTexels.
    static bool gamma_eligible(id<MTLTexture> t) { return t.mipmapLevelCount > 1 && t.width * t.height <= kFineMaxTexels; }
    void encode_blit_mips(const std::vector<id<MTLTexture>> &list) {
        if (list.empty()) return;
        id<MTLBlitCommandEncoder> b = [cmd_ blitCommandEncoder];
        for (id<MTLTexture> t : list) [b generateMipmapsForTexture:t];
        [b endEncoding];
        mips_blit_ += list.size();
    }
    void prepare_mips() {
        if (mip_group_) return;
        mip_group_ = dispatch_group_create();
        auto slot = mip_compiled_;
        id<MTLDevice> dev = dev_;
        dispatch_group_async(mip_group_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSError *e = nil;
            id<MTLLibrary> lib = [dev newLibraryWithSource:@(kMipMsl) options:nil error:&e];
            id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"mip_gamma"] : nil;
            id<MTLComputePipelineState> ps = fn ? [dev newComputePipelineStateWithFunction:fn error:&e] : nil;
            if (!ps) std::fprintf(stderr, "AR_ERROR mip_gamma shader: %s\n", e ? e.localizedDescription.UTF8String : "no function");
            slot->ps = ps;   // read only after dispatch_group_wait, which orders it
        });
    }
    id<MTLComputePipelineState> mip_pipeline_if_ready() {
        prepare_mips();
        if (dispatch_group_wait(mip_group_, async_ ? DISPATCH_TIME_NOW : DISPATCH_TIME_FOREVER) != 0) return nil;
        if (!mip_compiled_->ps && !mip_failed_) { mip_failed_ = true; late_gamma_.clear(); }
        return mip_compiled_->ps;
    }
    bool wait_mips_ready(double timeout_s) override {
        prepare_mips();
        return dispatch_group_wait(mip_group_, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_s * 1e9))) == 0 && mip_compiled_->ps;
    }
    void set_fine_textures(bool on) override { fine_want_ = on; apply_fine(); }
    void apply_fine() {
        const bool on = fine_want_ && filter_ == TextureFilter::smooth;
        if (on) prepare_mips();
        if (on == fine_) return;
        fine_ = on;
        samplers_.clear();
        std::unordered_set<void *> skip;
        for (id<MTLTexture> t : pending_mips_) skip.insert((__bridge void *)t);
        if (on) {
            for (id<MTLTexture> t : late_blit_) skip.insert((__bridge void *)t);
            late_blit_.clear();
            if (!mip_failed_)
                for (auto &kv : textures_)
                    if (gamma_eligible(kv.second) && !skip.count((__bridge void *)kv.second)) late_gamma_.push_back(kv.second);
        } else {
            for (id<MTLTexture> t : late_gamma_) skip.insert((__bridge void *)t);
            late_gamma_.clear();
            for (auto &kv : textures_)
                if (gamma_eligible(kv.second) && !skip.count((__bridge void *)kv.second)) late_blit_.push_back(kv.second);
        }
    }
    MipCounts mip_counts() const override { return MipCounts{mips_kernel_, mips_blit_, late_gamma_.size() + late_blit_.size()}; }
    bool read_texture_level(uint32_t tex_id, uint32_t level, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) override {
        auto it = textures_.find(tex_id);
        if (it == textures_.end() || level >= it->second.mipmapLevelCount) return false;
        id<MTLTexture> t = it->second;
#if TARGET_OS_OSX
        if (t.storageMode == MTLStorageModeManaged) {
            id<MTLCommandBuffer> c = [queue_ commandBuffer];
            id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
            [b synchronizeResource:t];
            [b endEncoding];
            [c commit];
            [c waitUntilCompleted];
        }
#endif
        w = (uint32_t)std::max<NSUInteger>(t.width >> level, 1); h = (uint32_t)std::max<NSUInteger>(t.height >> level, 1);
        rgba.resize((size_t)w * h * 4);
        [t getBytes:rgba.data() bytesPerRow:(NSUInteger)w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:level];
        return true;
    }
    uint32_t anisotropy() const override { return filter_ == TextureFilter::smooth ? (fine_ ? 16u : 4u) : 0u; }
    // The kernel writes into a scratch chain (private, writable; its level k is the texture's level k + 1) and a blit
    // copies the levels into the texture: the textures stay ShaderRead only (a ShaderWrite texture gets a blit chain
    // that differs by a rounding step).
    void encode_gamma_mips(id<MTLComputePipelineState> ps, const std::vector<id<MTLTexture>> &list) {
        if (list.empty()) return;
        @autoreleasepool {
            std::vector<id<MTLTexture>> scratch(list.size());
            id<MTLComputeCommandEncoder> ce = [cmd_ computeCommandEncoder];
            [ce setComputePipelineState:ps];
            for (size_t i = 0; i < list.size(); i++) {
                id<MTLTexture> t = list[i];
                const NSUInteger sw = std::max<NSUInteger>(t.width >> 1, 1), sh = std::max<NSUInteger>(t.height >> 1, 1);
                MTLTextureDescriptor *sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.pixelFormat width:sw height:sh
                                                                                         mipmapped:(sw > 1 || sh > 1)];
                sd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                sd.storageMode = MTLStorageModePrivate;
                id<MTLTexture> sc = [dev_ newTextureWithDescriptor:sd];
                if (!sc || sc.mipmapLevelCount + 1 != t.mipmapLevelCount) continue;
                scratch[i] = sc;
                for (NSUInteger l = 1; l < t.mipmapLevelCount; l++) {
                    id<MTLTexture> src = l == 1 ? t : [sc newTextureViewWithPixelFormat:sc.pixelFormat textureType:MTLTextureType2D
                                                                                 levels:NSMakeRange(l - 2, 1) slices:NSMakeRange(0, 1)];
                    id<MTLTexture> dst = [sc newTextureViewWithPixelFormat:sc.pixelFormat textureType:MTLTextureType2D
                                                                    levels:NSMakeRange(l - 1, 1) slices:NSMakeRange(0, 1)];
                    [ce setTexture:src atIndex:0]; [ce setTexture:dst atIndex:1];
                    const NSUInteger w = std::max<NSUInteger>(t.width >> l, 1), h = std::max<NSUInteger>(t.height >> l, 1);
                    [ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                }
            }
            [ce endEncoding];
            id<MTLBlitCommandEncoder> b = [cmd_ blitCommandEncoder];
            for (size_t i = 0; i < list.size(); i++) {
                id<MTLTexture> t = list[i], sc = scratch[i];
                if (!sc) { [b generateMipmapsForTexture:t]; mips_blit_++; continue; }
                mips_kernel_++;
                for (NSUInteger l = 1; l < t.mipmapLevelCount; l++) {
                    const NSUInteger w = std::max<NSUInteger>(t.width >> l, 1), h = std::max<NSUInteger>(t.height >> l, 1);
                    [b copyFromTexture:sc sourceSlice:0 sourceLevel:l - 1 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                             toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)];
                }
            }
            [b endEncoding];
        }
    }
    void set_lighting(const LightingFrame *lf) override {
        light_ = LightU{};
        ao_bound_ = nil;
        env_bound_ = nil;
        env_prev_bound_ = nil;
        if (!lf || !lf->on) return;
        for (int k = 0; k < 3; k++) { light_.dir_on[k] = lf->dir[k]; light_.tint_amb[k] = lf->tint[k]; }
        light_.dir_on[3] = 1.f;
        light_.tint_amb[3] = lf->ambient;
        light_.contact[0] = lf->contact ? lf->contact_strength : 0.f;
        light_.contact[1] = 0.03f * lf->upm;
        light_.contact[2] = 0.015f * lf->upm;
        light_.contact[3] = lf->ground_y;
        uv_from_clip(under_desc_.clip_from_level, light_.under_uv);
        light_.under_range[0] = under_desc_.y0;
        light_.under_range[1] = under_desc_.span;
        // PCF radius 1..3 texels; the receiver is pushed 3 texels along its face normal (1.5 texels left acne on steep
        // walls and grazing slopes).
        light_.shadow[0] = shadow_valid_ && lf->shadows ? 1.f : 0.f;
        light_.shadow[1] = 1.f + 2.f * lf->softness;
        light_.shadow[2] = 1.f / (float)kShadowSize;
        light_.shadow[3] = 3.f * shadow_fit_.texel_units;
        uv_from_clip(shadow_fit_.clip_from_level, light_.shadow_uv);
        light_.cast[0] = lf->table_cast;
        light_.relight[0] = lf->relight ? 1.f : 0.f;
        light_.relight[1] = lf->relight_strength;
        // AO: only the bake of this frame's scene (the renderer's ao_ready / ao_slot), never another slot's volume.
        if (lf->ao && lf->ao_ready && lf->ao_slot < kAoSlots && ao_[lf->ao_slot]) {
            const AoVolume &v = ao_vol_[lf->ao_slot];
            ao_bound_ = ao_[lf->ao_slot];
            light_.ao[0] = 1.f;
            light_.ao[1] = lf->ao_strength;
            for (int k = 0; k < 3; k++) { light_.ao_min[k] = v.min[k]; light_.ao_inv[k] = 1.f / (float)v.dims[k]; }
            light_.ao_min[3] = v.voxel;
        }
        if (lf->toy && lf->reflections && lf->env_cube) {
            id<MTLTexture> c = (__bridge id<MTLTexture>)lf->env_cube;
            if (c.textureType == MTLTextureTypeCube) env_bound_ = c;
            if (env_bound_ && lf->env_prev_cube && lf->env_fade < 1.f) {
                id<MTLTexture> p = (__bridge id<MTLTexture>)lf->env_prev_cube;
                if (p.textureType == MTLTextureTypeCube) env_prev_bound_ = p;
            }
        }
        light_.toy[0] = lf->toy ? 1.f : 0.f;
        light_.toy[1] = lf->gloss;
        light_.toy[2] = lf->rim;
        light_.toy[3] = env_bound_ ? std::clamp(lf->env_mix, 0.f, 1.f) : 0.f;
        for (int k = 0; k < 3; k++) {
            light_.env0[k] = lf->env_from_level[k]; light_.env1[k] = lf->env_from_level[3 + k]; light_.env2[k] = lf->env_from_level[6 + k];
        }
        light_.env1[3] = lf->env_exposure;
        for (int k = 0; k < 3; k++) {
            light_.env3[k] = lf->env_prev_from_level[k]; light_.env4[k] = lf->env_prev_from_level[3 + k];
            light_.env5[k] = lf->env_prev_from_level[6 + k];
        }
        light_.env4[3] = lf->env_prev_exposure;
        light_.env5[3] = env_prev_bound_ ? std::clamp(lf->env_fade, 0.f, 1.f) : 1.f;
        if (lf->liquid) {
            const uint32_t n = std::min<uint32_t>(lf->water_count, 8u);
            light_.liquid[0] = 1.f; light_.liquid[1] = lf->liquid_time_s; light_.liquid[2] = (float)n; light_.liquid[3] = 0.25f * lf->liquid_upm;
            light_.liquid2[0] = 6.2831853f / (0.04f * lf->liquid_upm); light_.liquid2[1] = 0.08f;
            light_.liquid2[2] = lf->water_trim && foot_valid_ && n > 0 ? 1.f : 0.f;
            light_.liquid2[3] = 0.01f * lf->liquid_upm;
            for (uint32_t i = 0; i < n; i++) {
                for (int k = 0; k < 4; k++) light_.water_box[i][k] = lf->water[i][k];
                light_.water_y[i / 4][i % 4] = lf->water[i][4];
            }
            if (light_.liquid2[2] > 0.5f) {
                light_.water_foot[0] = foot_window_[0]; light_.water_foot[1] = foot_window_[1];
                light_.water_foot[2] = 1.f / (foot_window_[2] - foot_window_[0]); light_.water_foot[3] = 1.f / (foot_window_[3] - foot_window_[1]);
            }
        }
        if (lf->cut && lf->cut_radius > 0.f) {
            for (int k = 0; k < 3; k++) light_.cut[k] = lf->cut_target[k];
            light_.cut[3] = lf->cut_radius;
            // the core keeps none of its cells: an open hole (20% left pixel-sized specks that shimmered)
            light_.cut2[0] = 0.f; light_.cut2[1] = lf->cut_keep; light_.cut2[2] = std::max(lf->cut_cell, 1e-4f);
        }
        if (lf->trans_kind) {
            light_.trans[0] = (float)lf->trans_kind; light_.trans[1] = lf->trans_cover; light_.trans[2] = lf->trans_radius; light_.trans[3] = lf->trans_soft;
            for (int k = 0; k < 3; k++) { light_.trans_rgb[k] = lf->trans_rgb[k]; light_.trans_c[k] = lf->trans_centre[k]; }
        }
        if (lf->fine_textures && filter_ == TextureFilter::smooth) { light_.tex[0] = 1.f; light_.tex[1] = 0.25f; }
    }
    bool bake_ao(uint32_t slot, const AoVolume &vol, const std::vector<ShadowFit> &views, float radius, const fc_vertex *verts,
                 uint32_t nv, const DrawPacket *draws, uint32_t nd, const DepthExtras &x) override {
        const uint32_t n = (uint32_t)std::min<size_t>(views.size(), kAoMaxViews);
        auto fail = [](const char *why) { std::fprintf(stderr, "AR_AO bake failed: %s\n", why); return false; };
        if (slot >= kAoSlots) return fail("slot");
        if (!cmd_ || n == 0 || !nv) return fail("no frame or nothing to bake");
        if (!depth_pipelines() || !ao_pipeline()) return fail("no pipeline");
        // Its own command buffer, committed now (before this frame's): its GPU time is the bake's alone, and the frame's
        // passes, queued after it, sample the finished volume.
        id<MTLCommandBuffer> bc = [queue_ commandBuffer];
        if (!bc) return fail("no command buffer");
        MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                     width:kAoMapSize height:kAoMapSize mipmapped:NO];
        md.textureType = MTLTextureType2DArray; md.arrayLength = n;
        md.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        md.storageMode = MTLStorageModePrivate;
        id<MTLTexture> maps = [dev_ newTextureWithDescriptor:md];
        MTLTextureDescriptor *vd = [MTLTextureDescriptor new];
        vd.textureType = MTLTextureType3D; vd.pixelFormat = MTLPixelFormatR8Unorm;
        vd.width = vol.dims[0]; vd.height = vol.dims[1]; vd.depth = vol.dims[2];
        vd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        vd.storageMode = MTLStorageModePrivate;
        id<MTLTexture> out = [dev_ newTextureWithDescriptor:vd];
        if (!maps || !out) return fail("no memory");
        NSUInteger off = 0;
        id<MTLBuffer> vb = stage(verts, nv, &off);
        std::vector<float> uv((size_t)n * 16);
        for (uint32_t j = 0; j < n; j++) {
            encode_depth_map(bc, maps, views[j].clip_from_level, vb, off, draws, nd, nv, 0.f, 0.f, /*clamp*/ false, j, x);
            uv_from_clip(views[j].clip_from_level, &uv[(size_t)j * 16]);
        }
        struct AoU { float min_voxel[4]; uint32_t dims_n[4]; float range[4]; } u{{vol.min[0], vol.min[1], vol.min[2], vol.voxel},
                                                                                {vol.dims[0], vol.dims[1], vol.dims[2], n},
                                                                                {radius, 0.f, 0.f, 0.f}};
        id<MTLComputeCommandEncoder> enc = [bc computeCommandEncoder];
        [enc setComputePipelineState:ao_pipe_];
        [enc setTexture:maps atIndex:0];
        [enc setTexture:out atIndex:1];
        [enc setBytes:&u length:sizeof u atIndex:0];
        [enc setBytes:uv.data() length:uv.size() * sizeof(float) atIndex:1];
        [enc dispatchThreadgroups:MTLSizeMake((vol.dims[0] + 3) / 4, (vol.dims[1] + 3) / 4, (vol.dims[2] + 3) / 4)
            threadsPerThreadgroup:MTLSizeMake(4, 4, 4)];
        [enc endEncoding];
        const uint64_t gen = ++ao_gen_;
        auto t = ao_time_;
        [bc addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            std::lock_guard<std::mutex> g(t->m);
            if (gen > t->gen) { t->gen = gen; t->ms = std::max(0.0, (cb.GPUEndTime - cb.GPUStartTime) * 1000.0); }
        }];
        [bc commit];
        ao_[slot] = out;
        ao_vol_[slot] = vol;
        return true;
    }
    bool ao_bake_time(uint64_t *bake, double *ms) const override {
        std::lock_guard<std::mutex> g(ao_time_->m);
        if (ao_time_->gen == 0) return false;
        *bake = ao_time_->gen; *ms = ao_time_->ms;
        return true;
    }
    // The AO kernel compiles off the render thread (~0.1 s, a hitch on the render thread), started as soon as AO is
    // wanted.
    void prepare_ao() override {
        if (ao_group_) return;
        ao_group_ = dispatch_group_create();
        auto slot = ao_compiled_;
        id<MTLDevice> dev = dev_;
        dispatch_group_async(ao_group_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSError *e = nil;
            id<MTLLibrary> lib = [dev newLibraryWithSource:@(kAoMsl) options:nil error:&e];
            id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"ao_fill"] : nil;
            id<MTLComputePipelineState> ps = fn ? [dev newComputePipelineStateWithFunction:fn error:&e] : nil;
            if (!ps) std::fprintf(stderr, "ao_fill shader: %s\n", e ? e.localizedDescription.UTF8String : "no function");
            if (ps && ps.maxTotalThreadsPerThreadgroup < 64) { std::fprintf(stderr, "ao_fill: threadgroup too small\n"); ps = nil; }
            slot->ps = ps;   // read only after dispatch_group_wait, which orders it
        });
    }
    id<MTLComputePipelineState> ao_pipeline() {
        prepare_ao();
        if (!ao_pipe_tried_) {
            dispatch_group_wait(ao_group_, DISPATCH_TIME_FOREVER);
            ao_pipe_ = ao_compiled_->ps;
            ao_pipe_tried_ = true;
        }
        return ao_pipe_;
    }
    void encode_shadow_map(const ShadowFit &fit, const fc_vertex *verts, uint32_t nv, const DrawPacket *draws,
                           uint32_t nd, const DepthExtras &x) override {
        if (!cmd_ || !depth_pipelines()) return;
        if (!shadow_) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                          width:kShadowSize height:kShadowSize mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            shadow_ = [dev_ newTextureWithDescriptor:td];
        }
        flush_sampling(shadow_);   // recorded passes that sample this frame's earlier map get it, not the new one
        encode_depth_map(shadow_, fit.clip_from_level, verts, nv, draws, nd, /*slope*/ 2.f, /*const*/ 0.f, /*clamp*/ true, x);
        shadow_fit_ = fit;
        shadow_valid_ = true;
    }
    void encode_underside_map(const UndersideDesc &u, const fc_vertex *verts, uint32_t nv, const DrawPacket *draws,
                              uint32_t nd, const DepthExtras &x) override {
        if (!cmd_ || !depth_pipelines()) return;
        if (!under_) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                          width:kUndersideSize height:kUndersideSize mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            under_ = [dev_ newTextureWithDescriptor:td];
        }
        flush_sampling(under_);
        encode_depth_map(under_, u.clip_from_level, verts, nv, draws, nd, 0.f, 0.f, /*clamp*/ false, x);
        under_desc_ = u;
        under_valid_ = true;
    }
    void encode_water_footprint(const float window[4], const float clip_from_level[16], const fc_vertex *verts, uint32_t nv,
                                const DrawPacket *draws, uint32_t nd) override {
        if (!cmd_ || !depth_pipelines() || !(window[2] > window[0]) || !(window[3] > window[1])) return;
        if (!foot_) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                          width:kFootprintSize height:kFootprintSize mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            foot_ = [dev_ newTextureWithDescriptor:td];
        }
        flush_sampling(foot_);
        encode_depth_map(foot_, clip_from_level, verts, nv, draws, nd, 0.f, 0.f, /*clamp*/ false, DepthExtras{});
        std::memcpy(foot_window_, window, sizeof foot_window_);
        foot_valid_ = true;
        foot_kept_ = true;
    }
    bool reuse_water_footprint() override {
        if (!cmd_ || !foot_ || !foot_kept_) return false;
        foot_valid_ = true;
        return true;
    }
    struct EnvResult { std::mutex m; EnvStats s; uint32_t generation = 0; bool valid = false; };
    std::shared_ptr<EnvResult> env_result_ = std::make_shared<EnvResult>();
    const char *environment_error() const override { return env_error_.empty() ? nullptr : env_error_.c_str(); }
    std::string env_error_;
    id<MTLComputePipelineState> env_pipe_ = nil;
    bool env_pipe_tried_ = false;
    id<MTLBuffer> env_buf_[3] = {nil, nil, nil};
    int env_pending_ = -1;
    uint32_t env_pending_gen_ = 0;
    id<MTLComputePipelineState> env_pipeline() {
        if (env_pipe_tried_) return env_pipe_;
        env_pipe_tried_ = true;
        std::string err;
        if (passes_group_) {
            dispatch_group_wait(passes_group_, DISPATCH_TIME_FOREVER);
            env_pipe_ = passes_compiled_->env; err = passes_compiled_->env_err;
        } else {
            env_pipe_ = compile_env_pipeline(dev_, &err);
        }
        if (!env_pipe_) env_error_ = err;
        return env_pipe_;
    }
    void encode_environment_reduce(void *cube, uint32_t generation) override {
        if (!cmd_ || !cube) return;
        id<MTLTexture> t = (__bridge id<MTLTexture>)cube;
        if (t.textureType != MTLTextureTypeCube) { env_error_ = "not_cube"; return; }
        id<MTLComputePipelineState> ps = env_pipeline();
        if (!ps) return;
        const NSUInteger threads = std::min<NSUInteger>(256, ps.maxTotalThreadsPerThreadgroup);
        if (threads == 0) { env_error_ = "no_threads"; return; }
        __strong id<MTLBuffer> &b = env_buf_[ring_index_];
        if (!b) b = [dev_ newBufferWithLength:6 * sizeof(float) options:MTLResourceStorageModeShared];
        id<MTLComputeCommandEncoder> enc = [cmd_ computeCommandEncoder];
        [enc setComputePipelineState:ps];
        [enc setTexture:t atIndex:0];
        [enc setBuffer:b offset:0 atIndex:0];
        [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
        [enc endEncoding];
        env_pending_ = ring_index_;
        env_pending_gen_ = generation;
    }
    void *copy_environment(void *cube, uint32_t slot) override {
        if (!cmd_ || !cube || slot >= kEnvCopySlots) return nullptr;
        id<MTLTexture> s = (__bridge id<MTLTexture>)cube;
        if (s.textureType != MTLTextureTypeCube) return nullptr;
        __strong id<MTLTexture> &d = env_copy_[slot];
        if (!d || d.width != s.width || d.pixelFormat != s.pixelFormat || d.mipmapLevelCount != s.mipmapLevelCount) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:s.pixelFormat size:s.width
                                                                                       mipmapped:s.mipmapLevelCount > 1];
            td.mipmapLevelCount = s.mipmapLevelCount;
            td.usage = MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            d = [dev_ newTextureWithDescriptor:td];   // frames in flight keep the one it replaces (command buffers retain)
            if (!d) return nullptr;
        }
        id<MTLBlitCommandEncoder> b = [cmd_ blitCommandEncoder];
        [b copyFromTexture:s toTexture:d];            // all faces and mips: a snapshot, whatever ARKit does to its texture
        [b endEncoding];
        return (__bridge void *)d;
    }
    bool environment_stats(EnvStats *out, uint32_t *generation) const override {
        std::lock_guard<std::mutex> g(env_result_->m);
        if (!env_result_->valid) return false;
        *out = env_result_->s;
        *generation = env_result_->generation;
        return true;
    }
    void encode_pass(uint32_t target, const PassDesc &pass, const ViewConstants &view, const fc_vertex *verts, uint32_t nv,
                     const DrawPacket *draws, uint32_t nd, const int8_t *normals, const float *prev_pos) override {
        if (!cmd_ || target == 0 || target > targets_.size()) return;
        ensure_placeholders();
        Target &t = targets_[target - 1];
        if (!t.color) return;
        if (pass.clear && t.nrec) flush(t);
        if (!target_tex_.empty())                               // a draw sampling a target sees what it drew this frame
            for (uint32_t i = 0; i < nd; i++)
                for (uint32_t k : {draws[i].tex[0], draws[i].tex[1]}) {
                    auto bt = k ? target_tex_.find(k) : target_tex_.end();
                    if (bt != target_tex_.end() && bt->second != target && bt->second <= targets_.size()) flush(targets_[bt->second - 1]);
                }
        if (t.nrec == t.recs.size()) t.recs.emplace_back();
        Rec &r = t.recs[t.nrec++];
        r.pass = pass; r.view = view; r.light = light_; r.nv = nv;
        r.shadow = shadow_valid_ ? shadow_ : shadow_placeholder_;
        r.under = under_valid_ ? under_ : under_placeholder_;
        r.ao = ao_bound_ ? ao_bound_ : ao_placeholder_;
        r.env = env_bound_ ? env_bound_ : env_placeholder_;
        r.env_prev = env_prev_bound_ ? env_prev_bound_ : env_placeholder_;
        r.foot = foot_valid_ ? foot_ : under_placeholder_;
        r.draws.clear();
        r.vb = nil; r.nb = nil; r.pb = nil; r.voff = r.noff = r.poff = 0;
        if (nv && nd) {
            r.vb = stage(verts, nv, &r.voff);
            if (normals) r.nb = stage_bytes(normals, (size_t)nv * 4, &r.noff);
            if (prev_pos && view.interp_on) r.pb = stage_bytes(prev_pos, (size_t)nv * 3 * sizeof(float), &r.poff);
            r.draws.assign(draws, draws + nd);
            r.tex.resize((size_t)nd * 2);
            for (uint32_t i = 0; i < nd; i++) { r.tex[2 * i] = texture(draws[i].tex[0]); r.tex[2 * i + 1] = texture(draws[i].tex[1]); }
        }
    }
    void encode_composite(uint32_t dst, void *external, uint32_t under, uint32_t over) override {
        if (cmd_ && !composite_lib_) std::fprintf(stderr, "error: composite shader library unavailable\n");
        if (!cmd_ || !composite_lib_ || under == 0 || over == 0 || under > targets_.size() || over > targets_.size()) return;
        id<MTLTexture> out = dst ? (dst <= targets_.size() ? targets_[dst - 1].color : nil) : (__bridge id<MTLTexture>)external;
        if (!out) return;
        if (targets_[under - 1].layers > 1 || targets_[over - 1].layers > 1 || (dst && targets_[dst - 1].layers > 1)) {
            std::fprintf(stderr, "error: composite of layered targets is not supported\n");
            return;
        }
        flush(targets_[under - 1]); flush(targets_[over - 1]);
        if (dst) flush(targets_[dst - 1]);
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = out;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderPipelineState> ps = composite_pipeline(out.pixelFormat);
        if (!ps) {   // never bind a nil pipeline (Metal asserts): report and leave dst untouched
            std::fprintf(stderr, "error: no composite pipeline for pixel format %lu\n", (unsigned long)out.pixelFormat);
            return;
        }
        id<MTLRenderCommandEncoder> enc = [cmd_ renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:ps];
        [enc setFragmentTexture:targets_[under - 1].color atIndex:0];
        [enc setFragmentTexture:targets_[over - 1].color atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
    }
    void end_frame(void (*done)(void *), void *ctx, void *drawable) override {
        dispatch_semaphore_t sem = inflight_;
        if (!frame_open_) { if (done) done(ctx); return; }
        frame_open_ = false;
        for (Target &t : targets_) { flush(t); t.encoded = false; }
        retired_.clear();                                              // the command buffer now holds what it draws
        encoders_last_ = encoders_;
        encoders_ = 0;
        if (!cmd_) {   // begin_frame got no command buffer: still release its in-flight slot and the caller's ctx
            if (done) done(ctx);
            dispatch_semaphore_signal(sem);
            return;
        }
        auto gpu = gpu_ms_;
        id<MTLBuffer> env_buf = env_pending_ >= 0 ? env_buf_[env_pending_] : nil;
        const uint32_t env_gen = env_pending_gen_;
        auto env = env_result_;
        env_pending_ = -1;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            if (cb.GPUEndTime > cb.GPUStartTime) gpu->store((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
            if (env_buf) {
                const float *v = (const float *)env_buf.contents;
                std::lock_guard<std::mutex> g(env->m);
                for (int k = 0; k < 3; k++) { env->s.l0[k] = v[k]; env->s.l1[k] = v[3 + k]; }
                env->generation = env_gen;
                env->valid = true;
            }
            if (done) done(ctx);
            dispatch_semaphore_signal(sem);
        }];
        if (drawable) [cmd_ presentDrawable:(__bridge id<MTLDrawable>)drawable];
        [cmd_ commit];
        last_ = cmd_;
        cmd_ = nil;
    }
    double last_gpu_ms() const override { return gpu_ms_->load(); }
    uint32_t render_encoders_last_frame() const override { return encoders_last_; }
    uint64_t late_passes() const override { return late_; }
    void wait_idle() override { if (last_) [last_ waitUntilCompleted]; }
    bool read_target(uint32_t t, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) override {
        if (t == 0 || t > targets_.size()) return false;
        Target &x = targets_[t - 1];
        if (!x.color) return false;
#if TARGET_OS_OSX
        if (!shared_) {
            id<MTLCommandBuffer> c = [queue_ commandBuffer];
            id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
            [b synchronizeResource:x.color];
            [b endEncoding];
            [c commit];
            [c waitUntilCompleted];
        }
#endif
        w = x.w; h = x.h;
        rgba.resize((size_t)w * h * 4);
        [x.color getBytes:rgba.data() bytesPerRow:(NSUInteger)w * 4 bytesPerImage:(NSUInteger)w * h * 4
               fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 slice:0];
        return true;
    }
    bool compile_combiner(const fc_combiner_key &key, std::string *error, uint32_t samples) override {
        DrawPacket d{};
        d.cc = normalize_combiner(key);
        d.blend = (d.cc.opts & FC_CC_ALPHA) != 0;
        return pipeline(d, MTLPixelFormatRGBA8Unorm, true, supported_samples(samples, 1), error) != nil;
    }
    std::string pipeline_list() const override {
        std::string out;
        char b[96];
        for (const auto &[k, ps] : pipes_) {
            if (!ps) continue;
            // the 11th field (cut) is written only when it is 1, so every other line keeps the older format
            const int len = std::snprintf(b, sizeof b, "%016llx %x %u %u %u %u %u %u %u %u", (unsigned long long)k.slots, k.opts, k.fmt,
                                          (unsigned)k.blend, (unsigned)k.depth, (unsigned)k.flat, (unsigned)k.depth_only,
                                          (unsigned)k.table_shadow, (unsigned)k.clip, (unsigned)k.samples);
            if (len > 0 && (size_t)len < sizeof b)
                std::snprintf(b + len, sizeof b - (size_t)len, k.cut ? " 1\n" : "\n");
            out += b;
        }
        return out;
    }
    // The pipelines a list names (each line's draw and sample count). Only the renderer's own target format is accepted
    // (a stray format would compile a pipeline no draw uses), and only the first kMaxWarmLines lines, each at most
    // kMaxWarmLineBytes long, are read: a damaged or foreign file cannot stall the layer thread for long.
    struct ListLine { DrawPacket d; bool depth; uint32_t samples; };
    static std::vector<ListLine> parse_pipeline_lines(const std::string &list) {
        std::vector<ListLine> out;
        size_t pos = 0, lines = 0;
        while (pos < list.size() && lines++ < kMaxWarmLines) {
            size_t e = list.find('\n', pos);
            if (e == std::string::npos) e = list.size();
            const size_t len = e - pos, at = pos;
            pos = e + 1;
            if (len > kMaxWarmLineBytes) continue;
            const std::string line = list.substr(at, len);
            unsigned long long slots = 0;
            unsigned opts = 0, fmt = 0, blend = 0, depth = 0, flat = 0, depth_only = 0, table_shadow = 0, clip = 0, samples = 1, cut = 0;
            char tail = 0;
            // Lines with 7 fields (no table_shadow) are skipped like any bad line; 8 fields (no clip) name pipelines
            // without the scissor discard; 9 fields (no samples) single-sample ones; 10 fields pipelines without the
            // cutaway.
            const int got = std::sscanf(line.c_str(), "%llx %x %u %u %u %u %u %u %u %u %u %c", &slots, &opts, &fmt, &blend, &depth, &flat,
                                        &depth_only, &table_shadow, &clip, &samples, &cut, &tail);
            if ((got != 8 && got != 9 && got != 10 && got != 11) || blend > 1 || depth > 1 || flat > kSilhouetteCutout ||
                depth_only > kDepthOnlyPrepass || table_shadow > 1 || clip > 1 || (samples != 1 && samples != 2 && samples != 4) ||
                cut > 1 || fmt != MTLPixelFormatRGBA8Unorm)
                continue;
            DrawPacket d{};
            d.cc = fc_combiner_key{slots, opts, 0};
            d.blend = (uint8_t)blend; d.silhouette = (uint8_t)flat; d.depth_only = (uint8_t)depth_only; d.table_shadow = (uint8_t)table_shadow;
            d.clip_on = (uint8_t)clip;
            d.cutaway = (uint8_t)cut;
            out.push_back(ListLine{d, depth != 0, samples});
        }
        return out;
    }
    static std::vector<DrawPacket> parse_pipeline_list(const std::string &list) {
        std::vector<DrawPacket> out;
        for (const ListLine &l : parse_pipeline_lines(list)) if (l.depth) out.push_back(l.d);
        return out;
    }
    size_t warm_pipelines(const std::string &list, uint32_t at_samples) override {
        // The pipelines not yet built are compiled in parallel on the global queue (the calling thread waits), then cached.
// a background job not started yet is claimed here and joins the parallel set (its slot is filled
// by this thread after the apply: the claim makes it the only writer); one another block owns is waited for.
        struct Job { PipeKey pk; DrawPacket d; MTLPixelFormat fmt; bool depth; uint32_t samples; id<MTLRenderPipelineState> ps; std::string why; bool refused;
                     std::shared_ptr<WarmSlot> slot; };
        std::vector<Job> jobs;
        std::vector<PipeKey> owned_elsewhere;
        std::set<PipeKey> queued;
        const MTLPixelFormat fmt = MTLPixelFormatRGBA8Unorm;
        for (const ListLine &l : parse_pipeline_lines(list)) {
            const uint32_t s = supported_samples(at_samples ? at_samples : l.samples, 1);
            const PipeKey pk = pipe_key(l.d, fmt, l.depth, s);
            if (pipes_.count(pk) || !queued.insert(pk).second) continue;
            auto w = warming_.find(pk);
            if (w == warming_.end()) { jobs.push_back(Job{pk, l.d, fmt, l.depth, s, nil, std::string(), false, nullptr}); continue; }
            const std::shared_ptr<WarmJob> wj = w->second;
            if (claim(*wj->slot, kOwnerInline)) jobs.push_back(Job{pk, wj->d, wj->fmt, wj->depth, wj->samples, nil, std::string(), false, wj->slot});
            else owned_elsewhere.push_back(pk);
        }
        Job *jp = jobs.data();
        const MetalDevice *self = this;
        dispatch_apply(jobs.size(), dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t i) {
            Job &j = jp[i];
            j.ps = self->build_pipeline(j.d, j.fmt, j.depth, j.samples, j.pk.a2c != 0, &j.why, &j.refused);
        });
        size_t built = 0;
        for (Job &j : jobs) {
            if (j.slot) {
                ++warm_stats_->compiles;
                j.slot->ps = j.ps; j.slot->why = j.why; j.slot->refused = j.refused;
                dispatch_group_leave(j.slot->done);
                take_warmed(j.pk, false);
            } else {
                store_pipeline(j.pk, j.ps, j.why, j.refused, j.samples);
            }
            if (j.ps) built++;
        }
        for (const PipeKey &pk : owned_elsewhere) take_warmed(pk, true);
        return built;
    }
    size_t warm_pipelines_background(const std::string &list, const std::vector<DrawPacket> &draws, uint32_t samples) override {
        std::vector<DrawPacket> want = draws;
        for (const DrawPacket &d : parse_pipeline_list(list)) {
            want.push_back(d);
            for (const DrawPacket &z : pipeline_siblings(d)) want.push_back(z);
        }
        const uint32_t s = supported_samples(samples, 1);
        const MTLPixelFormat fmt = MTLPixelFormatRGBA8Unorm;
        size_t queued = 0;
        for (const DrawPacket &d : want) {
            if (d.silhouette && decode_combiner(d.cc).unsupported) continue;
            const PipeKey pk = pipe_key(d, fmt, true, s);
            if (pipes_.count(pk) || warming_.count(pk)) continue;
            enqueue_warm(pk, d, fmt, true, s, QOS_CLASS_UTILITY);
            queued++;
        }
        return queued;
    }
private:
    // one result slot per pipeline. Every block that could compile it (its queue's block, a
// promotion, a waiting lookup: take_warmed / warm_pipelines) first claims the slot; only the owner writes ps, why and
// refused, then leaves `done`. Everyone else only reads after waiting on `done`: never two writers.
    enum : int { kOwnerNone = 0, kOwnerQueued = 1, kOwnerPromoted = 2, kOwnerInline = 3 };
    struct WarmSlot {
        std::atomic<int> owner{kOwnerNone};
        dispatch_group_t done = dispatch_group_create();
        id<MTLRenderPipelineState> ps = nil; std::string why; bool refused = false;
        WarmSlot() { dispatch_group_enter(done); }
    };
    static bool claim(WarmSlot &s, int who) { int none = kOwnerNone; return s.owner.compare_exchange_strong(none, who); }
    struct WarmJob;
    struct WarmStats;
    static void warm_compile(id<MTLDevice> dev, MTLVertexDescriptor *vd, bool lm, const WarmJob &j, WarmStats &stats,
                             dispatch_group_t compile_hold);
public:
    void enqueue_warm(const PipeKey &pk, const DrawPacket &d, MTLPixelFormat fmt, bool depth, uint32_t samples, qos_class_t qos) {
        id<MTLDevice> dev = dev_;
        MTLVertexDescriptor *vd = vdesc_;
        const bool lm = layered_msaa_;
        auto j = std::make_shared<WarmJob>();
        j->pk = pk; j->d = d; j->fmt = fmt; j->depth = depth; j->samples = samples; j->qos = qos;
        j->slot = std::make_shared<WarmSlot>();
        warming_[pk] = j;
        dispatch_group_t hold = warm_hold_, compile_hold = warm_compile_hold_;
        const bool background = qos != QOS_CLASS_USER_INITIATED;
        std::shared_ptr<WarmStats> stats = warm_stats_;
        // the background in kWarmLanes serial lanes, so a draw's own compile (user-initiated, the global queue)
// reaches the Metal compiler behind at most that many, never behind the whole list (the compiler serves in order).
        dispatch_queue_t q = background ? warm_lanes_[warm_lane_next_++ % kWarmLanes] : dispatch_get_global_queue(qos, 0);
        dispatch_async(q, ^{
            dispatch_group_wait(hold, DISPATCH_TIME_FOREVER);
            if (!claim(*j->slot, kOwnerQueued)) return;
            if (background) {
                const uint32_t now = ++stats->running;
                for (uint32_t p = stats->peak.load(); now > p && !stats->peak.compare_exchange_weak(p, now);) {}
            }
            warm_compile(dev, vd, lm, *j, *stats, compile_hold);
            if (background) --stats->running;
        });
    }
    // a draw skipped for a pipeline still waiting in a background lane promotes it: one more block, user-initiated
// on the global queue, claims the same slot; whichever block claims first compiles it (once). At most
// kMaxPromotionsInFlight run at once; a draw skipped while that many are in flight tries again next frame.
    void promote_warm(WarmJob &w) {
        if (w.promoted || w.qos == QOS_CLASS_USER_INITIATED) return;
        if (w.slot && w.slot->owner.load() != kOwnerNone) return;
        if (warm_stats_->promoting.load() >= kMaxPromotionsInFlight) return;
        w.promoted = true;
        promotions_++;
        ++warm_stats_->promoting;
        id<MTLDevice> dev = dev_;
        MTLVertexDescriptor *vd = vdesc_;
        const bool lm = layered_msaa_;
        std::shared_ptr<WarmJob> j = warming_[w.pk];
        std::shared_ptr<WarmStats> stats = warm_stats_;
        dispatch_group_t hold = warm_promote_hold_, compile_hold = warm_compile_hold_;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            dispatch_group_wait(hold, DISPATCH_TIME_FOREVER);
            if (claim(*j->slot, kOwnerPromoted)) warm_compile(dev, vd, lm, *j, *stats, compile_hold);
            --stats->promoting;
        });
    }
    uint32_t background_warm_peak() const override { return warm_stats_->peak.load(); }
    uint64_t warm_compiles() const override { return warm_stats_->compiles.load(); }
    uint64_t warm_promotions() const override { return promotions_; }
    static void set_gate(dispatch_group_t g, bool &held, bool hold) {
        if (hold == held) return;
        held = hold;
        if (hold) dispatch_group_enter(g);
        else dispatch_group_leave(g);
    }
    void hold_background_warm(bool hold) override { set_gate(warm_hold_, warm_held_, hold); }
    void hold_promoted_warm(bool hold) override { set_gate(warm_promote_hold_, warm_promote_held_, hold); }
    void hold_warm_compiles(bool hold) override { set_gate(warm_compile_hold_, warm_compile_held_, hold); }
    size_t pipelines_warming() override { collect_warmed(); return warming_.size(); }
    bool wait_pipelines_warmed(double timeout_s) override {
        const dispatch_time_t end = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_s * 1e9));
        while (!warming_.empty()) {
            auto it = warming_.begin();
            if (dispatch_group_wait(it->second->slot->done, end) != 0) return false;
            take_warmed(it->first, false);
        }
        return true;
    }
    size_t draw_time_builds() const override { return draw_builds_; }
    void set_async_pipelines(bool on) override { async_ = on; }
    bool async_pipelines() const override { return async_; }
    uint64_t deferred_draws() const override { return deferred_; }
    bool take_deferred_miss(std::string *key) override {
        if (miss_log_.empty()) return false;
        if (key) *key = miss_log_.front();
        miss_log_.erase(miss_log_.begin());
        return true;
    }
    size_t pipeline_count() const override { return pipes_.size(); }
    bool take_pipeline_error(std::string *why) override {
        if (refused_.empty()) return false;
        if (why) *why = refused_.front();
        refused_.erase(refused_.begin());
        return true;
    }
    void *command_buffer() override { return (__bridge void *)cmd_; }
    bool read_target_depth(uint32_t t, std::vector<float> &out, uint32_t &w, uint32_t &h) override {
        if (t == 0 || t > targets_.size()) return false;
        Target &x = targets_[t - 1];
        if (!x.depth) return false;
        const NSUInteger bpr = (NSUInteger)x.w * 4, n = bpr * x.h;
        id<MTLBuffer> buf = [dev_ newBufferWithLength:n options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> c = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
        [b copyFromTexture:x.depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(x.w, x.h, 1)
                  toBuffer:buf destinationOffset:0 destinationBytesPerRow:bpr destinationBytesPerImage:n
                   options:MTLBlitOptionDepthFromDepthStencil];
        [b endEncoding];
        [c commit];
        [c waitUntilCompleted];
        w = x.w; h = x.h;
        out.resize((size_t)w * h);
        std::memcpy(out.data(), buf.contents, out.size() * sizeof(float));
        return true;
    }
    void encode_resolve(uint32_t src, const ExternalView &dst) override {
        if (!cmd_ || !resolve_lib_ || src == 0 || src > targets_.size() || !dst.color) return;
        Target &s = targets_[src - 1];
        flush(s);
        if (dst.src_layer >= s.layers) {
            std::fprintf(stderr, "error: resolve of slice %u from a target with %u\n", dst.src_layer, s.layers);
            return;
        }
        id<MTLTexture> color = (__bridge id<MTLTexture>)dst.color;
        id<MTLTexture> depth = (__bridge id<MTLTexture>)dst.depth;
        const MTLPixelFormat df = depth ? depth.pixelFormat : MTLPixelFormatInvalid;
        bool pending = false;
        id<MTLRenderPipelineState> ps = resolve_pipeline(color.pixelFormat, df, s.layers > 1, &pending);
        if (!ps && pending) {   // async: still compiling; the drawable's slice is cleared (transparent, far) this frame
            MTLRenderPassDescriptor *cp = [MTLRenderPassDescriptor renderPassDescriptor];
            cp.colorAttachments[0].texture = color;
            cp.colorAttachments[0].slice = dst.slice;
            cp.colorAttachments[0].loadAction = MTLLoadActionClear;
            cp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            cp.colorAttachments[0].storeAction = MTLStoreActionStore;
            if (depth) {
                cp.depthAttachment.texture = depth;
                cp.depthAttachment.slice = dst.slice;
                cp.depthAttachment.loadAction = MTLLoadActionClear;
                cp.depthAttachment.clearDepth = 0.0;
                cp.depthAttachment.storeAction = MTLStoreActionStore;
                if (has_stencil(df)) {
                    cp.stencilAttachment.texture = depth;
                    cp.stencilAttachment.slice = dst.slice;
                    cp.stencilAttachment.loadAction = MTLLoadActionLoad;
                    cp.stencilAttachment.storeAction = MTLStoreActionStore;
                }
            }
            [[cmd_ renderCommandEncoderWithDescriptor:cp] endEncoding];
            deferred_++;
            return;
        }
        if (!ps) {
            std::fprintf(stderr, "error: no resolve pipeline for colour %lu / depth %lu\n", (unsigned long)color.pixelFormat,
                         (unsigned long)(depth ? depth.pixelFormat : 0));
            return;
        }
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = color;
        rp.colorAttachments[0].slice = dst.slice;
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;   // views may share a texture: never discard the others
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (depth) {
            rp.depthAttachment.texture = depth;
            rp.depthAttachment.slice = dst.slice;
            rp.depthAttachment.loadAction = MTLLoadActionLoad;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            if (has_stencil(df)) {   // keep the device's stencil as it is; the pipeline must name the same format
                rp.stencilAttachment.texture = depth;
                rp.stencilAttachment.slice = dst.slice;
                rp.stencilAttachment.loadAction = MTLLoadActionLoad;
                rp.stencilAttachment.storeAction = MTLStoreActionStore;
            }
        }
        id<MTLRenderCommandEncoder> enc = [cmd_ renderCommandEncoderWithDescriptor:rp];
        const MTLViewport vp = {dst.viewport.x, dst.viewport.y, dst.viewport.w, dst.viewport.h, 0.0, 1.0};
        [enc setViewport:vp];
        [enc setRenderPipelineState:ps];
        if (depth) [enc setDepthStencilState:resolve_depth_state()];
        // Reads stay inside this view's own source region, so a neighbouring view in the same target never bleeds in.
        const uint32_t vw = std::max(1u, (uint32_t)std::ceil(dst.viewport.w)), vh = std::max(1u, (uint32_t)std::ceil(dst.viewport.h));
        struct { float dst_origin[2]; uint32_t src_origin[2]; uint32_t src_last[2]; uint32_t linearize; uint32_t src_layer; } u = {
            {dst.viewport.x, dst.viewport.y}, {dst.src_x, dst.src_y}, {dst.src_x + vw - 1, dst.src_y + vh - 1},
            dst.linearize ? 1u : 0u, dst.src_layer};
        [enc setFragmentBytes:&u length:sizeof u atIndex:0];
        [enc setFragmentTexture:s.color atIndex:0];
        if (depth) [enc setFragmentTexture:s.depth atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
    }

private:
    MTLStorageMode readable_storage() const {
#if TARGET_OS_OSX
        return shared_ ? MTLStorageModeShared : MTLStorageModeManaged;
#else
        return MTLStorageModeShared;
#endif
    }
    void make_target(Target &t, uint32_t w, uint32_t h, uint32_t layers, uint32_t samples) {
        flush(t);
        w = w ? w : 1; h = h ? h : 1; layers = std::max(layers, 1u);
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = readable_storage();
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kDepthFormat width:w height:h mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        dd.storageMode = MTLStorageModePrivate;
        if (layers > 1) {
            cd.textureType = dd.textureType = MTLTextureType2DArray;
            cd.arrayLength = dd.arrayLength = layers;
        }
        t.color = [dev_ newTextureWithDescriptor:cd];
        t.depth = [dev_ newTextureWithDescriptor:dd];
        t.w = w; t.h = h; t.layers = layers;
        t.samples = supported_samples(samples, layers);
        t.ms_color = nil; t.ms_depth = nil; t.memoryless = false;
        if (t.samples > 1) {
            t.memoryless = memoryless_msaa();
            MTLTextureDescriptor *md = [MTLTextureDescriptor new];
            md.textureType = layers > 1 ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample;
            md.width = w; md.height = h; md.sampleCount = t.samples;
            if (layers > 1) md.arrayLength = layers;
            md.usage = MTLTextureUsageRenderTarget;
            md.storageMode = t.memoryless ? MTLStorageModeMemoryless : MTLStorageModePrivate;
            md.pixelFormat = MTLPixelFormatRGBA8Unorm;
            t.ms_color = [dev_ newTextureWithDescriptor:md];
            md.pixelFormat = kDepthFormat;
            t.ms_depth = [dev_ newTextureWithDescriptor:md];
        }
    }
    // Multisample depth resolves with MTLMultisampleDepthResolveFilterMax (reverse-Z's nearest sample), so the
    // compositor reprojects an edge pixel with the foreground surface it partly shows.
    void flush(Target &t) {
        if (!t.nrec) return;
        const size_t n = t.nrec;
        t.nrec = 0;
        const bool ms = t.samples > 1;
        if (cmd_ && ms && t.memoryless && !t.recs[0].pass.clear) {
            late_ += n;
        } else if (cmd_) {
            const PassDesc &p0 = t.recs[0].pass;
            const MTLLoadAction load = p0.clear ? MTLLoadActionClear : MTLLoadActionLoad;
            const MTLStoreAction store = !ms ? MTLStoreActionStore
                                       : t.memoryless ? MTLStoreActionMultisampleResolve : MTLStoreActionStoreAndMultisampleResolve;
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = ms ? t.ms_color : t.color;
            rp.colorAttachments[0].loadAction = load;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(p0.clear_color[0], p0.clear_color[1], p0.clear_color[2], p0.clear_color[3]);
            rp.colorAttachments[0].storeAction = store;
            rp.depthAttachment.texture = ms ? t.ms_depth : t.depth;
            rp.depthAttachment.loadAction = load;
            rp.depthAttachment.clearDepth = 0.0;
            rp.depthAttachment.storeAction = store;
            rp.stencilAttachment.texture = ms ? t.ms_depth : t.depth;
            rp.stencilAttachment.loadAction = load;
            rp.stencilAttachment.clearStencil = 0;
            rp.stencilAttachment.storeAction = ms && t.memoryless ? MTLStoreActionDontCare : MTLStoreActionStore;
            if (ms) {
                rp.colorAttachments[0].resolveTexture = t.color;
                rp.depthAttachment.resolveTexture = t.depth;
                rp.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterMax;
            }
            rp.renderTargetArrayLength = t.layers;   // also 1 for a 2D target: vmain writes render_target_array_index (validation)
            rp.rasterizationRateMap = t.rate_map;    // only here, on the target's own encoder (nil: none)
            id<MTLRenderCommandEncoder> enc = [cmd_ renderCommandEncoderWithDescriptor:rp];
            [enc setFrontFacingWinding:MTLWindingCounterClockwise];
            for (size_t i = 0; i < n; i++) encode_rec(enc, t, t.recs[i]);
            [enc endEncoding];
            t.encoded = true;
            encoders_++;
        }
        for (size_t i = 0; i < n; i++) {                        // the command buffer holds what the encoder used
            Rec &r = t.recs[i];
            r.vb = nil; r.nb = nil; r.pb = nil; r.shadow = nil; r.under = nil; r.ao = nil; r.env = nil; r.env_prev = nil; r.foot = nil;
        }
    }
    // Viewport and scissor are in the target's pixels, or with a rate map in its screen pixels (the map's screenSize).
    void encode_rec(id<MTLRenderCommandEncoder> enc, const Target &t, const Rec &r) {
        const uint32_t bw = t.rate_map ? t.screen_w : t.w, bh = t.rate_map ? t.screen_h : t.h;
        Viewport v = r.pass.viewport;
        if (v.w <= 0 || v.h <= 0) v = Viewport{0, 0, float(bw), float(bh)};
        const NSUInteger sx = (NSUInteger)std::max(0.f, v.x), sy = (NSUInteger)std::max(0.f, v.y);
        const NSUInteger sw = std::min<NSUInteger>((NSUInteger)v.w, bw > sx ? bw - sx : 0), sh = std::min<NSUInteger>((NSUInteger)v.h, bh > sy ? bh - sy : 0);
        if (!r.vb || r.draws.empty() || !sw || !sh) return;
        const MTLViewport mvp = {v.x, v.y, v.w, v.h, 0.0, 1.0};
        [enc setViewport:mvp];
        const MTLScissorRect sc = {sx, sy, sw, sh};
        [enc setScissorRect:sc];
        [enc setVertexBuffer:r.vb offset:r.voff atIndex:0];
        // vmain reads the normals only with DrawU.lflags bit 256; otherwise any buffer will do
        [enc setVertexBuffer:(r.nb ? r.nb : r.vb) offset:(r.nb ? r.noff : r.voff) atIndex:3];
        [enc setVertexBytes:&r.light length:sizeof r.light atIndex:4];
        // vmain reads the previous positions only with ViewU.interp.y (r.pb staged); otherwise any buffer will do
        [enc setVertexBuffer:(r.pb ? r.pb : r.vb) offset:(r.pb ? r.poff : r.voff) atIndex:5];
        const ViewConstants &view = r.view;
        ViewU vu{};
        std::memcpy(vu.clip_from_model, view.clip_from_model, sizeof vu.clip_from_model);
        vu.eye_pull[0] = view.eye_model[0]; vu.eye_pull[1] = view.eye_model[1]; vu.eye_pull[2] = view.eye_model[2]; vu.eye_pull[3] = view.decal_pull;
        for (int k = 0; k < 3; k++) vu.eye_view[k] = view.eye_view[k];
        vu.layer = view.layer;
        for (int k = 0; k < 4; k++) { vu.wclip_lo[k] = view.wclip_lo[k]; vu.wclip_hi[k] = view.wclip_hi[k]; }
        vu.interp[0] = view.interp_alpha; vu.interp[1] = (view.interp_on && r.pb) ? 1.f : 0.f;
        [enc setVertexBytes:&vu length:sizeof vu atIndex:1];
        [enc setFragmentBytes:&vu length:sizeof vu atIndex:2];
        [enc setFragmentTexture:r.env atIndex:4];
        [enc setFragmentTexture:r.env_prev atIndex:6];
        [enc setFragmentBytes:&r.light length:sizeof r.light atIndex:1];
        [enc setFragmentTexture:r.under atIndex:5];             // every fmain declares it
        [enc setFragmentTexture:r.foot atIndex:7];
        [enc setFragmentSamplerState:linear_clamp_ atIndex:3];
        [enc setFragmentTexture:r.shadow atIndex:2];
        [enc setFragmentSamplerState:compare_sampler_ atIndex:2];
        [enc setFragmentTexture:r.ao atIndex:3];
        const uint32_t nd = (uint32_t)r.draws.size();
        for (uint32_t i = 0; i < nd; i++) {
            const DrawPacket &d = r.draws[i];
            if (d.vertex_count == 0 || d.first_vertex + d.vertex_count > r.nv) continue;
            id<MTLRenderPipelineState> ps = pipeline(d, t.color.pixelFormat, true, t.samples, nullptr, true);
            if (!ps) continue;
            [enc setRenderPipelineState:ps];
            [enc setDepthStencilState:r.pass.depth ? depth_state(d) : no_depth_state()];
            [enc setStencilReferenceValue:r.pass.depth ? stencil_ref(stencil_mode(d)) : 0u];
            [enc setCullMode:d.cull == FC_CULL_BACK ? MTLCullModeBack : d.cull == FC_CULL_FRONT ? MTLCullModeFront : MTLCullModeNone];
            DrawU du{};
            std::memcpy(du.prim, d.prim, sizeof du.prim); std::memcpy(du.env, d.env, sizeof du.env);
            du.lod = d.lod_frac; du.decal = d.decal ? 1.f : 0.f; du.noise_seed = view.noise_seed; du.pull = d.pull;
            du.fade = d.fade; du.talpha = d.trans_alpha; std::memcpy(du.clip, d.clip, sizeof du.clip); du.clip_on = d.clip_on ? 1.f : 0.f;
            du.lflags = d.light_flags | (r.nb ? 256u : 0u) | ((d.light_role & FC_LR_SELF_LIT) ? 512u : 0u) |
                        ((d.light_role & FC_LR_MIRROR) ? 1024u : 0u) |
                        ((d.light_role & FC_LR_WATER) ? 2048u : 0u) | ((d.light_role & FC_LR_LAVA) ? 8192u : 0u) |
                        (d.ink ? 4096u : 0u);
            [enc setVertexBytes:&du length:sizeof du atIndex:2];
            [enc setFragmentBytes:&du length:sizeof du atIndex:0];
            for (int k = 0; k < 2; k++) {
                [enc setFragmentTexture:r.tex[2 * i + k] atIndex:k];
                [enc setFragmentSamplerState:sampler(d.samp[k]) atIndex:k];
            }
            [enc setDepthBias:0.f slopeScale:(r.pass.depth && d.decal ? view.decal_slope : 0.f) clamp:0.f];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:d.first_vertex vertexCount:d.vertex_count];
        }
    }
    // Before an off-target pass overwrites tex, the targets whose recorded passes sample it are encoded.
    void flush_sampling(id<MTLTexture> tex) {
        if (!tex) return;
        for (Target &t : targets_)
            for (size_t i = 0; i < t.nrec; i++)
                if (t.recs[i].shadow == tex || t.recs[i].under == tex || t.recs[i].foot == tex) { flush(t); break; }
    }
    id<MTLDepthStencilState> no_depth_state() {
        if (!no_depth_) {
            MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = NO;
            no_depth_ = [dev_ newDepthStencilStateWithDescriptor:dd];
        }
        return no_depth_;
    }
    id<MTLBuffer> stage(const fc_vertex *v, uint32_t n, NSUInteger *off) { return stage_bytes(v, (size_t)n * sizeof(fc_vertex), off); }
    id<MTLBuffer> stage_bytes(const void *v, size_t n, NSUInteger *off) {
        // A large array (the frame's vertices/normals) is staged by every pass and shadow map; reuse the first copy when a later
        // call passes the same pointer and size and the bytes are still equal (the renderer's ribbon_ vector is rebuilt in
        // place between calls, so the pointer alone does not prove the content; the frame snapshot itself is immutable while held).
        const bool big = n >= kMemoMin;
        if (big)
            for (const StageMemo &m : memo_)
                if (m.src == v && m.n == n && m.buf && std::memcmp((const uint8_t *)m.buf.contents + m.off, v, n) == 0) {
                    *off = m.off;
                    return m.buf;
                }
        Ring &r = ring_[ring_index_];
        NSUInteger bytes = (NSUInteger)n;
        if (!r.buf || r.used + bytes > r.buf.length) {   // grow; the command buffer retains the old buffer
            NSUInteger len = std::max<NSUInteger>(std::max<NSUInteger>(r.buf ? r.buf.length * 2 : 0, r.used + bytes), 1u << 20);
            r.buf = [dev_ newBufferWithLength:len options:MTLResourceStorageModeShared];
            r.used = 0;
        }
        std::memcpy((uint8_t *)r.buf.contents + r.used, v, bytes);
        *off = r.used;
        r.used += (bytes + 255) & ~(NSUInteger)255;
        r.total += (bytes + 255) & ~(NSUInteger)255;
        if (big) {
            memo_[memo_next_] = StageMemo{v, n, r.buf, *off};
            memo_next_ = (memo_next_ + 1) % kMemoSlots;
        }
        return r.buf;
    }
    // samples: the target's (1, 2 or 4). No alpha-to-coverage: texture-edge texels are already a = 1 or discarded at
    // 0.3 (like the depth maps' dcut), so coverage would see alpha 1 everywhere. A silhouette copy's fragment is its
    // PRIM colour whatever its combiner (emit_msl: flat_color), so all of them share one pipeline per clip state and
    // sample count, keyed and built with the shade combiner.
    static DrawPacket pipeline_form(const DrawPacket &d) {
        if (!d.silhouette) return d;
        static const fc_combiner_key flat = normalize_combiner(fc_combiner_key{((uint64_t)FC_SRC_SHADE << 12) | ((uint64_t)FC_SRC_SHADE << 28), 0, 0});
        DrawPacket c = d;
        c.cc = flat;
        return c;
    }
    PipeKey pipe_key(const DrawPacket &draw, MTLPixelFormat fmt, bool depth, uint32_t samples) {
        const PipeIdentity k = pipe_identity(pipeline_form(draw));
        return PipeKey{k.slots, k.opts, (uint32_t)fmt, k.blend, (uint8_t)depth, k.silhouette, k.depth_only, k.table_shadow,
                       k.clip, (uint8_t)samples, (uint8_t)(edge_ && samples > 1 && (k.opts & FC_CC_TEXTURE_EDGE) ? 1 : 0), k.cut};
    }
    // at_draw: a draw needs it now (a compile here stalls the frame: counted in draw_time_builds).
    id<MTLRenderPipelineState> pipeline(const DrawPacket &d, MTLPixelFormat fmt, bool depth, uint32_t samples, std::string *err,
                                        bool at_draw = false) {
        if (d.silhouette && decode_combiner(d.cc).unsupported) return nil;
        const PipeKey pk = pipe_key(d, fmt, depth, samples);
        const bool defer = at_draw && async_;                   // never compile (nor wait for a compile) on this thread
        auto it = pipes_.find(pk);
        if (it == pipes_.end() && take_warmed(pk, !defer)) it = pipes_.find(pk);
        if (it != pipes_.end()) { if (!it->second && err) *err = pipe_errors_[pk]; return it->second; }
        if (defer) {
            if (!warming_.count(pk)) {
                enqueue_warm(pk, d, fmt, depth, samples, QOS_CLASS_USER_INITIATED);
                draw_builds_++;
            }
            if (missed_.insert(pk).second) {
                char b[96];
                std::snprintf(b, sizeof b, "%016llx/%x/b%u/f%u/d%u/t%u/c%u/s%u/k%u", (unsigned long long)pk.slots, pk.opts, pk.blend,
                              pk.flat, pk.depth_only, pk.table_shadow, pk.clip, pk.samples, (unsigned)pk.cut);
                miss_log_.push_back(b);
            }
            if (d.cutaway) {   // drawn uncut (its normal pipeline) until the variant is built: never missing
                DrawPacket plain = d;
                plain.cutaway = 0;
                return pipeline(plain, fmt, depth, samples, err, at_draw);
            }
            auto w = warming_.find(pk);
            if (w != warming_.end()) promote_warm(*w->second);
            deferred_++;
            return nil;
        }
        std::string why;
        bool refused = false;
        id<MTLRenderPipelineState> ps = build_pipeline(d, fmt, depth, samples, pk.a2c != 0, &why, &refused);
        store_pipeline(pk, ps, why, refused, samples);
        if (at_draw) draw_builds_++;
        if (!ps && err) *err = why;
        return ps;
    }
    bool take_warmed(const PipeKey &pk, bool wait) {
        auto it = warming_.find(pk);
        if (it == warming_.end()) return false;
        const std::shared_ptr<WarmJob> jp = it->second;
        const WarmJob &j = *jp;
        WarmSlot &sl = *j.slot;
        if (wait && claim(sl, kOwnerInline)) {
            sl.ps = build_pipeline(j.d, j.fmt, j.depth, j.samples, j.pk.a2c != 0, &sl.why, &sl.refused);
            ++warm_stats_->compiles;
            dispatch_group_leave(sl.done);
        } else if (dispatch_group_wait(sl.done, wait ? DISPATCH_TIME_FOREVER : DISPATCH_TIME_NOW) != 0) return false;
        store_pipeline(j.pk, sl.ps, sl.why, sl.refused, j.samples);
        warming_.erase(it);
        return true;
    }
    void collect_warmed() {
        for (auto it = warming_.begin(); it != warming_.end();) {
            const PipeKey pk = (it++)->first;
            take_warmed(pk, false);
        }
    }
    // Compiles a draw pipeline (no cache; any thread: it reads only immutable device state). refused: Metal returned an
    // error (not a combiner the renderer cannot draw).
    id<MTLRenderPipelineState> build_pipeline(const DrawPacket &d, MTLPixelFormat fmt, bool depth, uint32_t samples, bool a2c, std::string *why,
                                              bool *refused) const {
        return build_pipeline_on(dev_, vdesc_, layered_msaa_, d, fmt, depth, samples, a2c, why, refused);
    }
    static id<MTLRenderPipelineState> build_pipeline_on(id<MTLDevice> dev_, MTLVertexDescriptor *vdesc_, bool layered_msaa_,
                                                        const DrawPacket &draw, MTLPixelFormat fmt, bool depth, uint32_t samples, bool a2c,
                                                        std::string *why, bool *refused) {
        const DrawPacket d = pipeline_form(draw);
        CombinerIR ir = decode_combiner(d.cc);
        ir.flat_color = d.silhouette != kSilhouetteNone;
        ir.flat_cutout = d.silhouette == kSilhouetteCutout;
        ir.depth_fill = d.depth_only;
        ir.table_shadow = d.table_shadow != 0;
        ir.clip = d.clip_on != 0;
        ir.alpha_coverage = a2c;
        ir.layer_out = samples == 1 || layered_msaa_;
        ir.cutaway = d.cutaway != 0;
        *refused = false;
        if (ir.unsupported) { *why = "unsupported combiner feature (light map)"; return nil; }
        id<MTLRenderPipelineState> ps = nil;
        NSError *e = nil;
        std::string src = emit_msl(ir);
        id<MTLLibrary> lib = shared_library(dev_, src, &e);
        if (lib) {
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"vmain"];
            pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
            pd.vertexDescriptor = vdesc_;
            pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
            pd.rasterSampleCount = samples;
            if (a2c) { pd.alphaToCoverageEnabled = YES; pd.alphaToOneEnabled = YES; }
            MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
            ca.pixelFormat = fmt;
            if (d.depth_only) ca.writeMask = MTLColorWriteMaskNone;
            if (d.blend) {   // RGB SrcAlpha/1-SrcAlpha, alpha One/1-SrcAlpha -> premultiplied coverage
                ca.blendingEnabled = YES;
                ca.rgbBlendOperation = MTLBlendOperationAdd; ca.alphaBlendOperation = MTLBlendOperationAdd;
                ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha; ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                ca.sourceAlphaBlendFactor = MTLBlendFactorOne; ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            }
            pd.depthAttachmentPixelFormat = depth ? kDepthFormat : MTLPixelFormatInvalid;
            pd.stencilAttachmentPixelFormat = depth ? kDepthFormat : MTLPixelFormatInvalid;
            ps = [dev_ newRenderPipelineStateWithDescriptor:pd error:&e];
        }
        if (!ps) {
            *why = e ? std::string(e.localizedDescription.UTF8String) : std::string("unknown error");
            *refused = true;
        }
        return ps;
    }
    static id<MTLLibrary> shared_library(id<MTLDevice> dev, const std::string &src, NSError **e) {
        static std::mutex m;
        static std::map<std::pair<void *, std::string>, id<MTLLibrary>> libs;
        const auto key = std::make_pair((__bridge void *)dev, src);
        {
            std::lock_guard<std::mutex> g(m);
            auto it = libs.find(key);
            if (it != libs.end()) return it->second;
        }
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src.c_str()) options:nil error:e];
        if (lib) {
            std::lock_guard<std::mutex> g(m);
            libs.emplace(key, lib);
        }
        return lib;
    }
    void store_pipeline(const PipeKey &pk, id<MTLRenderPipelineState> ps, const std::string &why, bool refused, uint32_t samples) {
        if (refused) {
            std::fprintf(stderr, "combiner %016llx/%x: %s\n", (unsigned long long)pk.slots, pk.opts, why.c_str());
            char head[80];
            std::snprintf(head, sizeof head, "combiner %016llx/%x samples=%u: ", (unsigned long long)pk.slots, pk.opts, samples);
            refused_.push_back(head + why);
            if (samples > 1) msaa_refused_ = true;
        }
        if (!ps) pipe_errors_[pk] = why;
        pipes_[pk] = ps;
    }
    void ensure_placeholders() {
        if (placeholders_ready_ || !cmd_) return;
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        for (id<MTLTexture> t : {under_placeholder_, shadow_placeholder_}) {
            rp.depthAttachment.texture = t;
            [[cmd_ renderCommandEncoderWithDescriptor:rp] endEncoding];
        }
        placeholders_ready_ = true;
    }
    static void compile_depth_pipelines(id<MTLDevice> dev, MTLVertexDescriptor *vd, __strong id<MTLRenderPipelineState> ps[2]) {
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(kDepthMsl) options:nil error:&e];
        if (!lib) { std::fprintf(stderr, "depth shader: %s\n", e ? e.localizedDescription.UTF8String : "unknown error"); return; }
        for (int cut = 0; cut < 2; cut++) {
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"dmain"];
            if (cut) pd.fragmentFunction = [lib newFunctionWithName:@"dcut"];
            pd.vertexDescriptor = vd;
            pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            e = nil;
            ps[cut] = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
            if (!ps[cut]) std::fprintf(stderr, "depth pipeline: %s\n", e ? e.localizedDescription.UTF8String : "unknown error");
        }
    }
    static id<MTLComputePipelineState> compile_env_pipeline(id<MTLDevice> dev, std::string *err) {
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(kEnvMsl) options:nil error:&e];
        id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"env_reduce"] : nil;
        id<MTLComputePipelineState> ps = fn ? [dev newComputePipelineStateWithFunction:fn error:&e] : nil;
        if (!ps) {
            std::fprintf(stderr, "env_reduce shader: %s\n", e ? e.localizedDescription.UTF8String : "no function");
            *err = "shader_failed";
        }
        return ps;
    }
    // The depth, AO and reflection pipelines compile on another thread from here on; depth_pipelines(), ao_pipeline()
    // and env_pipeline() wait for them.
    void prepare_passes() override {
        prepare_ao();
        if (passes_group_) return;
        passes_group_ = dispatch_group_create();
        auto slot = passes_compiled_;
        id<MTLDevice> dev = dev_;
        MTLVertexDescriptor *vd = vdesc_;
        dispatch_group_async(passes_group_, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            compile_depth_pipelines(dev, vd, slot->depth);
            slot->env = compile_env_pipeline(dev, &slot->env_err);   // read only after dispatch_group_wait, which orders it
        });
    }
    bool depth_pipelines() {
        if (depth_tried_) return depth_opaque_ && depth_cut_ && depth_less_write_;
        depth_tried_ = true;
        if (passes_group_) {
            dispatch_group_wait(passes_group_, DISPATCH_TIME_FOREVER);
            depth_opaque_ = passes_compiled_->depth[0]; depth_cut_ = passes_compiled_->depth[1];
        } else {
            id<MTLRenderPipelineState> ps[2] = {nil, nil};
            compile_depth_pipelines(dev_, vdesc_, ps);
            depth_opaque_ = ps[0]; depth_cut_ = ps[1];
        }
        MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLess;
        dd.depthWriteEnabled = YES;
        depth_less_write_ = [dev_ newDepthStencilStateWithDescriptor:dd];
        return depth_opaque_ && depth_cut_ && depth_less_write_;
    }
    void encode_depth_map(id<MTLTexture> map, const float m[16], const fc_vertex *verts, uint32_t nv, const DrawPacket *draws,
                          uint32_t nd, float slope_bias, float const_bias, bool clamp_depth, const DepthExtras &x) {
        NSUInteger off = 0, poff = 0;
        id<MTLBuffer> vb = nv ? stage(verts, nv, &off) : nil;
        const bool interp = vb && x.prev_pos && x.interp_alpha < 1.f;
        id<MTLBuffer> pb = interp ? stage_bytes(x.prev_pos, (size_t)nv * 3 * sizeof(float), &poff) : nil;
        encode_depth_map(cmd_, map, m, vb, off, draws, nd, nv, slope_bias, const_bias, clamp_depth, 0, x, pb, poff);
    }
    void encode_depth_map(id<MTLCommandBuffer> cb, id<MTLTexture> map, const float m[16], id<MTLBuffer> vb, NSUInteger off,
                          const DrawPacket *draws, uint32_t nd, uint32_t nv, float slope_bias, float const_bias, bool clamp_depth,
                          uint32_t slice, const DepthExtras &x, id<MTLBuffer> pb = nil, NSUInteger poff = 0) {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.depthAttachment.texture = map;
        rp.depthAttachment.slice = slice;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        [enc setDepthStencilState:depth_less_write_];
        [enc setCullMode:MTLCullModeNone];
        [enc setDepthBias:const_bias slopeScale:slope_bias clamp:0.f];
        // The key light's map clamps instead of clipping: a caster in front of the near plane (above the level's static
        // bounds: a jump at the summit, flight) lands at depth 0 and still shadows every receiver.
        if (clamp_depth) [enc setDepthClipMode:MTLDepthClipModeClamp];
        if (vb) {
            [enc setVertexBuffer:vb offset:off atIndex:0];
            [enc setVertexBytes:m length:16 * sizeof(float) atIndex:1];
            struct { float interp[4]; float wclip_lo[4]; float wclip_hi[4]; } du{{x.interp_alpha, pb ? 1.f : 0.f, 0.f, 0.f}, {}, {}};
            std::memcpy(du.wclip_lo, x.wclip_lo, sizeof du.wclip_lo);
            std::memcpy(du.wclip_hi, x.wclip_hi, sizeof du.wclip_hi);
            [enc setVertexBytes:&du length:sizeof du atIndex:2];
            [enc setVertexBuffer:(pb ? pb : vb) offset:(pb ? poff : off) atIndex:3];
            for (uint32_t i = 0; i < nd; i++) {
                const DrawPacket &d = draws[i];
                if (d.vertex_count == 0 || d.first_vertex + d.vertex_count > nv) continue;
                const bool cut = (d.cc.opts & FC_CC_TEXTURE_EDGE) && d.tex[0];
                [enc setRenderPipelineState:cut ? depth_cut_ : depth_opaque_];
                if (cut) {
                    [enc setFragmentTexture:texture(d.tex[0]) atIndex:0];
                    [enc setFragmentSamplerState:sampler(d.samp[0], true) atIndex:0];
                }
                [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:d.first_vertex vertexCount:d.vertex_count];
            }
        }
        [enc endEncoding];
    }
    id<MTLRenderPipelineState> composite_pipeline(MTLPixelFormat fmt) {
        __strong id<MTLRenderPipelineState> &ps = composite_[(uint32_t)fmt];
        if (!ps) {
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [composite_lib_ newFunctionWithName:@"cvert"];
            pd.fragmentFunction = [composite_lib_ newFunctionWithName:@"cfrag"];
            pd.colorAttachments[0].pixelFormat = fmt;
            NSError *e = nil;
            ps = [dev_ newRenderPipelineStateWithDescriptor:pd error:&e];
            if (!ps) std::fprintf(stderr, "composite pipeline: %s\n", e ? e.localizedDescription.UTF8String : "unknown error");
        }
        return ps;
    }
    id<MTLRenderPipelineState> resolve_pipeline(MTLPixelFormat cf, MTLPixelFormat df, bool layered, bool *pending = nullptr) {
        const uint64_t key = ((uint64_t)layered << 63) | ((uint64_t)cf << 32) | (uint64_t)df;
        auto it = resolve_.find(key);
        if (it != resolve_.end()) return it->second;
        if (!async_) return resolve_[key] = build_resolve_pipeline(dev_, resolve_lib_, cf, df, layered);
        auto jt = resolve_jobs_.find(key);
        if (jt == resolve_jobs_.end()) {
            auto j = std::make_shared<ResolveJob>();
            j->g = dispatch_group_create();
            id<MTLDevice> dev = dev_;
            id<MTLLibrary> lib = resolve_lib_;
            dispatch_group_async(j->g, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
                j->ps = build_resolve_pipeline(dev, lib, cf, df, layered);
            });
            jt = resolve_jobs_.emplace(key, j).first;
        }
        if (dispatch_group_wait(jt->second->g, DISPATCH_TIME_NOW) != 0) { if (pending) *pending = true; return nil; }
        id<MTLRenderPipelineState> ps = jt->second->ps;
        resolve_[key] = ps;
        resolve_jobs_.erase(jt);
        return ps;
    }
    static id<MTLRenderPipelineState> build_resolve_pipeline(id<MTLDevice> dev, id<MTLLibrary> lib, MTLPixelFormat cf,
                                                             MTLPixelFormat df, bool layered) {
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"rvert"];
        NSString *fn = df == MTLPixelFormatInvalid ? (layered ? @"rfrag_color_a" : @"rfrag_color") : (layered ? @"rfrag_a" : @"rfrag");
        pd.fragmentFunction = [lib newFunctionWithName:fn];
        pd.colorAttachments[0].pixelFormat = cf;
        pd.depthAttachmentPixelFormat = df;
        if (has_stencil(df)) pd.stencilAttachmentPixelFormat = df;
        NSError *e = nil;
        id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!ps) std::fprintf(stderr, "resolve pipeline: %s\n", e ? e.localizedDescription.UTF8String : "unknown error");
        return ps;
    }
    static bool has_stencil(MTLPixelFormat f) {
        switch (f) {
        case MTLPixelFormatDepth32Float_Stencil8:
#if TARGET_OS_OSX
        case MTLPixelFormatDepth24Unorm_Stencil8:
#endif
            return true;
        default: return false;
        }
    }
    id<MTLDepthStencilState> resolve_depth_state() {
        if (!resolve_depth_) {
            MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = YES;
            resolve_depth_ = [dev_ newDepthStencilStateWithDescriptor:dd];
        }
        return resolve_depth_;
    }
    id<MTLDepthStencilState> depth_state(const DrawPacket &d) {
        const uint8_t mode = stencil_mode(d);
        const bool test = d.silhouette ? true : d.depth_test != 0, write = d.silhouette ? false : d.depth_write != 0;
        const uint32_t key = (test ? 1u : 0u) | (write ? 2u : 0u) | ((uint32_t)mode << 2);
        __strong id<MTLDepthStencilState> &s = depth_[key];
        if (!s) {
            MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = d.silhouette ? MTLCompareFunctionLess
                                                   : (test ? MTLCompareFunctionGreaterEqual : MTLCompareFunctionAlways);
            dd.depthWriteEnabled = write;
            if (mode != kStencilKeep) {
                MTLStencilDescriptor *st = [MTLStencilDescriptor new];
                const bool tests = mode == kStencilSilhouette || mode == kStencilFaint;
                st.stencilCompareFunction = tests ? MTLCompareFunctionEqual : MTLCompareFunctionAlways;
                st.stencilFailureOperation = MTLStencilOperationKeep;
                st.depthFailureOperation = MTLStencilOperationKeep;
                st.depthStencilPassOperation = mode == kStencilSilhouette ? MTLStencilOperationIncrementClamp
                                             : mode == kStencilFaint      ? MTLStencilOperationKeep
                                                                          : MTLStencilOperationReplace;
                st.readMask = mode == kStencilSilhouette ? kStencilFocusBits : mode == kStencilFaint ? kStencilDepthWritten : 0xFF;
                st.writeMask = mode == kStencilFaint ? 0 : 0xFF;
                dd.frontFaceStencil = st; dd.backFaceStencil = st;
            }
            s = [dev_ newDepthStencilStateWithDescriptor:dd];
        }
        return s;
    }
    id<MTLSamplerState> sampler(const fc_sampler &s, bool depth_pass = false) {
        const bool aniso16 = fine_ && !depth_pass;
        uint32_t key = s.linear | (uint32_t)s.wrap_s << 8 | (uint32_t)s.wrap_t << 16 | (aniso16 ? 1u << 24 : 0u);
        __strong id<MTLSamplerState> &st = samplers_[key];
        if (!st) {
            MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
            const MTLSamplerMinMagFilter own = s.linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            sd.magFilter = own;
            if (filter_ == TextureFilter::smooth) {
                sd.minFilter = MTLSamplerMinMagFilterLinear;
                sd.mipFilter = MTLSamplerMipFilterLinear;
                sd.maxAnisotropy = aniso16 ? 16 : 4;
            } else {
                sd.minFilter = own;
            }
            sd.sAddressMode = address(s.wrap_s); sd.tAddressMode = address(s.wrap_t);
            st = [dev_ newSamplerStateWithDescriptor:sd];
        }
        return st;
    }
    id<MTLTexture> texture(uint32_t tex_id) {
        if (!tex_id) return white_;
        if (!target_tex_.empty()) {
            auto bt = target_tex_.find(tex_id);
            if (bt != target_tex_.end())
                return bt->second <= targets_.size() && targets_[bt->second - 1].layers == 1 && targets_[bt->second - 1].color
                       ? targets_[bt->second - 1].color : white_;
        }
        auto it = textures_.find(tex_id);
        return it == textures_.end() ? white_ : it->second;
    }

    std::map<uint32_t, uint32_t> target_tex_;
    struct Ring { id<MTLBuffer> buf; NSUInteger used = 0; NSUInteger total = 0; };
    struct StageMemo { const void *src = nullptr; size_t n = 0; id<MTLBuffer> buf = nil; NSUInteger off = 0; };
    static constexpr size_t kMemoMin = 64 * 1024;
    static constexpr int kMemoSlots = 6;
    StageMemo memo_[kMemoSlots];
    int memo_next_ = 0;
    id<MTLDevice> dev_;
    // Multisample layered rendering (the vertex function writes render_target_array_index into a multisample pass): Apple7+
    // and Mac2 GPUs (Metal feature set tables); the visionOS simulator's validation rejects it whatever its GPU reports.
    const bool layered_msaa_ = !TARGET_OS_SIMULATOR && ([dev_ supportsFamily:MTLGPUFamilyApple7] || [dev_ supportsFamily:MTLGPUFamilyMac2]);
    bool shared_ = false;
    id<MTLCommandQueue> queue_;
    id<MTLCommandBuffer> cmd_;
    id<MTLCommandBuffer> last_;
    dispatch_semaphore_t inflight_;
    std::shared_ptr<std::atomic<double>> gpu_ms_ = std::make_shared<std::atomic<double>>(-1.0);   // the completed handler outlives the device
    bool frame_open_ = false;
    MTLVertexDescriptor *vdesc_;
    id<MTLTexture> white_;
    LightU light_{};
    id<MTLSamplerState> linear_clamp_;
    id<MTLTexture> under_placeholder_;
    bool placeholders_ready_ = false;
    id<MTLTexture> under_;
    id<MTLTexture> foot_;
    bool foot_valid_ = false;
    bool foot_kept_ = false;
    float foot_window_[4] = {0.f, 0.f, 1.f, 1.f};
    UndersideDesc under_desc_{};
    bool under_valid_ = false;
    id<MTLSamplerState> compare_sampler_;
    id<MTLTexture> shadow_placeholder_;
    id<MTLTexture> shadow_;
    ShadowFit shadow_fit_{};
    bool shadow_valid_ = false;
    static constexpr uint32_t kAoSlots = AoSchedule::kSlots;
    id<MTLTexture> ao_[kAoSlots];
    AoVolume ao_vol_[kAoSlots] = {};
    id<MTLTexture> ao_bound_;
    id<MTLTexture> ao_placeholder_;
    id<MTLTexture> env_bound_;
    id<MTLTexture> env_prev_bound_;
    id<MTLTexture> env_copy_[kEnvCopySlots];
    id<MTLTexture> env_placeholder_;
    id<MTLComputePipelineState> ao_pipe_ = nil;
    bool ao_pipe_tried_ = false;
    struct AoCompiled { id<MTLComputePipelineState> ps; };
    std::shared_ptr<AoCompiled> ao_compiled_ = std::make_shared<AoCompiled>();
    dispatch_group_t ao_group_ = nullptr;
    struct AoTime { std::mutex m; uint64_t gen = 0; double ms = 0.0; };
    std::shared_ptr<AoTime> ao_time_ = std::make_shared<AoTime>();
    uint64_t ao_gen_ = 0;
    bool depth_tried_ = false;
    struct PassCompiled { id<MTLRenderPipelineState> depth[2]; id<MTLComputePipelineState> env; std::string env_err; };
    std::shared_ptr<PassCompiled> passes_compiled_ = std::make_shared<PassCompiled>();
    dispatch_group_t passes_group_ = nullptr;
    // Background warm-up (warm_pipelines_background): one job per pipeline; the render thread alone reads warming_ and
    // takes a job's results after waiting on its group.
    struct WarmJob { PipeKey pk; DrawPacket d; MTLPixelFormat fmt; bool depth; uint32_t samples;
                     qos_class_t qos = QOS_CLASS_UTILITY;
                     bool promoted = false;
                     std::shared_ptr<WarmSlot> slot; };
    std::map<PipeKey, std::shared_ptr<WarmJob>> warming_;
    struct WarmStats {
        std::atomic<uint32_t> running{0}, peak{0};
        std::atomic<uint32_t> promoting{0};
        std::atomic<uint64_t> compiles{0};
    };
    static constexpr uint32_t kMaxPromotionsInFlight = 4;
    uint64_t promotions_ = 0;
    std::shared_ptr<WarmStats> warm_stats_ = std::make_shared<WarmStats>();
    static constexpr int kWarmLanes = 2;         // measured on a Mac, two lanes keep the unbounded throughput
    dispatch_queue_t warm_lanes_[kWarmLanes] = {
        dispatch_queue_create("ar.warm.0", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0)),
        dispatch_queue_create("ar.warm.1", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0))};
    unsigned warm_lane_next_ = 0;
    dispatch_group_t warm_hold_ = dispatch_group_create();
    bool warm_held_ = false;
    dispatch_group_t warm_promote_hold_ = dispatch_group_create();
    bool warm_promote_held_ = false;
    dispatch_group_t warm_compile_hold_ = dispatch_group_create();
    bool warm_compile_held_ = false;
    size_t draw_builds_ = 0;
    bool async_ = false;
    uint64_t deferred_ = 0;
    std::set<PipeKey> missed_;
    std::vector<std::string> miss_log_;
    struct ResolveJob { dispatch_group_t g; id<MTLRenderPipelineState> ps; };
    std::map<uint64_t, std::shared_ptr<ResolveJob>> resolve_jobs_;
    id<MTLRenderPipelineState> depth_opaque_, depth_cut_;
    id<MTLDepthStencilState> depth_less_write_;
    id<MTLLibrary> composite_lib_;
    std::map<uint32_t, id<MTLRenderPipelineState>> composite_;
    id<MTLLibrary> resolve_lib_;
    std::map<uint64_t, id<MTLRenderPipelineState>> resolve_;
    id<MTLDepthStencilState> resolve_depth_;
    id<MTLDepthStencilState> no_depth_;
    uint32_t encoders_ = 0, encoders_last_ = 0;
    uint64_t late_ = 0;
    bool edge_ = false;
    std::map<PipeKey, id<MTLRenderPipelineState>> pipes_;
    std::map<PipeKey, std::string> pipe_errors_;
    std::vector<std::string> refused_;
    bool msaa_refused_ = false;
    std::map<uint32_t, id<MTLDepthStencilState>> depth_;
    std::map<uint32_t, id<MTLSamplerState>> samplers_;
    TextureFilter filter_ = TextureFilter::smooth;
    std::vector<id<MTLTexture>> pending_mips_;
    std::unordered_map<uint32_t, id<MTLTexture>> textures_;
    std::vector<id<MTLTexture>> retired_;
    bool fine_ = false;
    bool fine_want_ = false;
    uint64_t mips_kernel_ = 0, mips_blit_ = 0;
    bool mip_failed_ = false;
    struct MipSlot { id<MTLComputePipelineState> ps = nil; };
    std::shared_ptr<MipSlot> mip_compiled_ = std::make_shared<MipSlot>();
    dispatch_group_t mip_group_ = nullptr;
    std::vector<id<MTLTexture>> late_gamma_;
    std::vector<id<MTLTexture>> late_blit_;
    std::vector<Target> targets_;
    Ring ring_[3];
    uint32_t ring_index_ = 0;
};
void MetalDevice::warm_compile(id<MTLDevice> dev, MTLVertexDescriptor *vd, bool lm, const WarmJob &j, WarmStats &stats,
                               dispatch_group_t compile_hold) {
    ++stats.compiles;
    dispatch_group_wait(compile_hold, DISPATCH_TIME_FOREVER);
    WarmSlot &s = *j.slot;
    s.ps = build_pipeline_on(dev, vd, lm, j.d, j.fmt, j.depth, j.samples, j.pk.a2c != 0, &s.why, &s.refused);
    dispatch_group_leave(s.done);
}
}

std::unique_ptr<IRenderDevice> create_metal_device(void *mtl_device, bool shared_storage) {
    return std::unique_ptr<IRenderDevice>(new MetalDevice((__bridge id<MTLDevice>)mtl_device, shared_storage));
}
void *metal_device_handle(IRenderDevice &dev) { return (__bridge void *)static_cast<MetalDevice &>(dev).device(); }
}

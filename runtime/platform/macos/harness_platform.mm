#import <Metal/Metal.h>
#include "harness_platform.h"
#include <cstring>
#include <vector>
#include "room_light.h"
#include "gc_pad.h"
extern "C" {
#include "ar_math.h"
}
namespace ar {
HarnessPlatform::HarnessPlatform() {
    api_.abi_version = AR_PLATFORM_ABI_VERSION;
    api_.user = this;
    api_.wait_frame = wait_frame; api_.get_table = get_table; api_.get_pad = get_pad; api_.session_state = state;
    api_.get_environment = get_environment;
    api_.get_preview = get_preview;
}
void HarnessPlatform::set_preview_test(const std::string &mode) {
    std::lock_guard<std::mutex> g(m_);
    preview_mode_ = mode == "seeking" ? AR_PREVIEW_SEEKING : mode == "dragging" ? AR_PREVIEW_DRAGGING : AR_PREVIEW_NONE;
}
int HarnessPlatform::get_preview(void *u, ar_preview *out) {
    auto *p = static_cast<HarnessPlatform *>(u);
    std::lock_guard<std::mutex> g(p->m_);
    *out = ar_preview{};
    out->state = (uint8_t)p->preview_mode_;
    out->tracking_from_preview = p->table_;
    if (p->preview_mode_ == AR_PREVIEW_SEEKING) {
        out->on_plane = 1;
        out->tracking_from_preview.pos[0] += 0.3f; out->tracking_from_preview.pos[2] += 0.2f;
        out->outline_count = 1;
        const float *o = p->table_.pos;
        const float c[4][2] = {{-0.5f, -0.3f}, {0.5f, -0.3f}, {0.5f, 0.3f}, {-0.5f, 0.3f}};
        for (int k = 0; k < 4; k++) {
            out->outlines[0][k][0] = o[0] + c[k][0]; out->outlines[0][k][1] = o[1]; out->outlines[0][k][2] = o[2] + c[k][1];
        }
    } else if (p->preview_mode_ == AR_PREVIEW_DRAGGING) {
        out->tracking_from_preview.pos[0] += 0.1f;
    }
    return 1;
}
HarnessPlatform::~HarnessPlatform() { if (fake_cube_) (void)(__bridge_transfer id<MTLTexture>)fake_cube_; }
void HarnessPlatform::set_fake_probe(void *mtl_device, const float dir[3], const float rgb[3]) {
    id<MTLDevice> d = (__bridge id<MTLDevice>)mtl_device;
    if (!d) return;
    const uint32_t n = 32;
    const std::vector<float> texels = fake_probe_texels(n, dir, rgb, 0.1f);
    // RGBA16Float: linear filtering of a float cube needs no 32-bit float filtering support.
    MTLTextureDescriptor *td = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float size:n mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [d newTextureWithDescriptor:td];
    const size_t face = (size_t)n * n * 4;
    std::vector<__fp16> half(face);
    for (NSUInteger f = 0; f < 6; f++) {
        for (size_t i = 0; i < face; i++) half[i] = (__fp16)texels[f * face + i];
        [t replaceRegion:MTLRegionMake2D(0, 0, n, n) mipmapLevel:0 slice:f withBytes:half.data() bytesPerRow:(NSUInteger)n * 8 bytesPerImage:0];
    }
    std::lock_guard<std::mutex> g(m_);
    if (fake_cube_) (void)(__bridge_transfer id<MTLTexture>)fake_cube_;
    fake_cube_ = (__bridge_retained void *)t;
    fake_gen_ = 1;
}
int HarnessPlatform::get_environment(void *u, ar_environment *out) {
    auto *p = static_cast<HarnessPlatform *>(u);
    std::lock_guard<std::mutex> g(p->m_);
    if (!p->fake_cube_) return 0;
    std::memset(out, 0, sizeof *out);
    out->cube_texture = p->fake_cube_;
    out->camera_scale_reference = 1.f;
    out->generation = p->fake_gen_ + (uint32_t)(p->time_ / 1000000000LL);
    out->tracking_from_probe = ar_pose_identity();
    out->timestamp_ns = p->time_;
    return 1;
}
void HarnessPlatform::set_size(uint32_t w, uint32_t h) { std::lock_guard<std::mutex> g(m_); w_ = w ? w : 1; h_ = h ? h : 1; }
void HarnessPlatform::set_stereo(bool on) { std::lock_guard<std::mutex> g(m_); stereo_ = on; }
bool HarnessPlatform::stereo() const { std::lock_guard<std::mutex> g(m_); return stereo_; }
void HarnessPlatform::set_head(const ar_pose &p) { std::lock_guard<std::mutex> g(m_); head_ = p; }
void HarnessPlatform::set_fov_up(float t) { std::lock_guard<std::mutex> g(m_); fov_up_ = t > 0.f && t < 100.f ? t : 0.5f; }
void HarnessPlatform::set_near(float n) { std::lock_guard<std::mutex> g(m_); near_m_ = n > 0.f && n < 10.f ? n : 0.f; }
void HarnessPlatform::set_time_ns(int64_t t) { std::lock_guard<std::mutex> g(m_); time_ = t; }
void HarnessPlatform::poll_gamepad() {
    ar_pad p{};
    const bool connected = ar::poll_gamepad(&p);
    std::lock_guard<std::mutex> lk(m_);
    pad_ = p;
    pad_connected_ = connected;
}
int HarnessPlatform::wait_frame(void *u, ar_frame_info *out) {
    auto *p = static_cast<HarnessPlatform *>(u);
    std::lock_guard<std::mutex> g(p->m_);
    std::memset(out, 0, sizeof *out);
    out->display_time_ns = p->time_;
    out->should_render = 1;
    out->tracking_from_head = p->head_;
    out->head_tracked = 1;
    uint32_t n = p->stereo_ ? 2 : 1, vw0 = p->w_ / n;
    const float up = p->fov_up_;
    out->view_count = n;
    for (uint32_t i = 0; i < n; i++) {
        ar_pose off = ar_pose_identity();
        off.pos[0] = n == 2 ? (i == 0 ? -0.0315f : 0.0315f) : 0.f;
        out->views[i].tracking_from_eye = ar_pose_mul(p->head_, off);
        uint32_t vw = i + 1 == n ? p->w_ - vw0 * (n - 1) : vw0;
        float side = up * (float)vw / (float)p->h_;
        out->views[i].fov = ar_fov{-side, side, up, -up};
        out->views[i].near_m = p->near_m_;
        out->views[i].width = vw;
        out->views[i].height = p->h_;
    }
    return 1;
}
int HarnessPlatform::get_table(void *u, ar_table *out) {
    auto *p = static_cast<HarnessPlatform *>(u);
    std::lock_guard<std::mutex> g(p->m_);
    out->tracking_from_table = p->table_;
    out->half_extent[0] = 0.85f; out->half_extent[1] = 0.85f;
    return 1;
}
int HarnessPlatform::get_pad(void *u, uint32_t player, ar_pad *out) {
    auto *p = static_cast<HarnessPlatform *>(u);
    std::lock_guard<std::mutex> g(p->m_);
    *out = ar_pad{};
    if (player != 0) return 0;
    *out = p->pad_;
    return p->pad_connected_ ? 1 : 0;
}
ar_session_state HarnessPlatform::state(void *) { return AR_SESSION_FOCUSED; }
}

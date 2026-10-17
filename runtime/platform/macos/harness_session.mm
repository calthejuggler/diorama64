#include "harness_session.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include "ar_layer.h"
#include "metal_device.h"
#include "room.h"
namespace ar {
namespace {
const float kRoomClear[4] = {0.62f, 0.66f, 0.70f, 1.f};
struct Done { ar_runtime *rt; ar_render_frame f; std::atomic<int> *inflight; };
void on_done(void *ctx) {   // Metal completion thread: release the snapshot the GPU was reading
    auto *d = static_cast<Done *>(ctx);
    ar_runtime_end_frame(d->rt, &d->f);
    d->inflight->fetch_sub(1);
    delete d;
}
void upload(void *user, uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba) { static_cast<IRenderDevice *>(user)->upload_texture(id, w, h, rgba); }
}
HarnessSession::HarnessSession(const ar_adapter *a, HarnessPlatform &p, void *mtl)
    : adapter_(a), platform_(p), dev_(create_metal_device(mtl)) {
    renderer_.reset(new Renderer(*dev_));
    room_ = fc_builder_create();
    fc_builder_reset(room_, 0, 320, 240);
    build_room(room_);
    room_frame_ = fc_builder_finish(room_);
    env_ = dev_->create_target(1, 1);
    ar_ = dev_->create_target(1, 1);
    final_ = dev_->create_target(1, 1);
}
HarnessSession::~HarnessSession() {
    dev_->wait_idle();
    while (inflight_.load() > 0) std::this_thread::yield();   // completion handlers may still be running
    if (rt_) { ar_runtime_stop_game_thread(rt_); ar_runtime_destroy(rt_); }
    fc_builder_destroy(room_);
}
bool HarnessSession::init(const std::string &rom, const std::string &save, std::string *error) {
    rt_ = ar_runtime_create(adapter_, platform_.api());
    if (!rt_) { *error = "adapter or platform ABI mismatch"; return false; }
    ar_runtime_set_texture_sink(rt_, upload, dev_.get());
    if (!ar_runtime_set_audio_mode(rt_, audio_mode_)) { *error = "the adapter cannot produce this audio mode"; return false; }
    if (!ar_runtime_init_game(rt_, rom.c_str(), save.c_str())) { *error = adapter_->last_error(); return false; }
    return true;
}
void HarnessSession::warmup(uint32_t ticks) { for (uint32_t i = 0; i < ticks; i++) ar_runtime_step_game(rt_); }
void HarnessSession::start_game_thread() { ar_runtime_start_game_thread(rt_); }
void *HarnessSession::mtl_device() { return metal_device_handle(*dev_); }
uint64_t HarnessSession::ticks_done() const { return rt_ ? ar_runtime_ticks_done(rt_) : 0; }
void HarnessSession::wait_idle() { dev_->wait_idle(); }
bool HarnessSession::read_final(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) { return dev_->read_target(final_, rgba, w, h); }
bool HarnessSession::read_ar(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) { return dev_->read_target(ar_, rgba, w, h); }
bool HarnessSession::read_game_view(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) {
    return renderer_ && renderer_->game_view_target() && dev_->read_target(renderer_->game_view_target(), rgba, w, h);
}
uint64_t HarnessSession::game_view_renders() const { return renderer_ ? renderer_->game_view_renders() : 0; }
bool HarnessSession::read_ar_depth(std::vector<float> &depth, uint32_t &w, uint32_t &h) {
    return dev_ && dev_->read_target_depth(ar_, depth, w, h);
}

bool HarnessSession::frame(bool sync, void *tex, void *drawable) {
    // Every early-out below happens before dev_->begin_frame(); once it runs, end_frame always follows (an
    // unpaired begin leaks a Metal in-flight slot and deadlocks after three frames).
    if (!rt_) return false;
    ar_render_frame rf{};
    if (!ar_runtime_begin_frame(rt_, &rf)) return false;
    const LightingFrame lf = light_.begin(*dev_, look_, rf);   // before the ticks: they already use this frame's steering
    if (sync) while (ar_runtime_ticks_due(rt_)) ar_runtime_step_game(rt_);
    uint32_t w = 0, h = 0;
    for (uint32_t i = 0; i < rf.view_count; i++) { w += rf.views[i].width; h = std::max(h, rf.views[i].height); }
    if (w == 0 || h == 0) {
        ar_render_frame none = rf;
        none.view_count = 0;
        ar_runtime_submit_frame(rt_, &none);
        ar_runtime_end_frame(rt_, &rf);
        return true;
    }
    const uint32_t samples = dev_->supported_samples(look_.msaa);
    if (w != w_ || h != h_ || samples != samples_) {
        dev_->wait_idle();
        dev_->resize_target(env_, w, h); dev_->resize_target(ar_, w, h, 1, samples); dev_->resize_target(final_, w, h);
        if (samples != samples_)
            std::printf("AR_MSAA samples=%u requested=%u memoryless=%d\n", samples, (unsigned)look_.msaa, dev_->memoryless_msaa() ? 1 : 0);
        w_ = w; h_ = h; samples_ = samples;
    }
    FrameView room[AR_MAX_VIEWS];
    for (uint32_t i = 0; i < rf.view_count; i++) {
        const ar_view_out &v = rf.views[i];
        room[i] = FrameView{v.clip_from_table, {0.f, 0.f, 0.f}, Viewport{float(v.x), 0.f, float(v.width), float(v.height)}};
    }
    ArLayerOptions lo;
    lo.overlays = overlay_; lo.silhouette = silhouette_; lo.flat_hud = flat_hud_;
    lo.lighting = &lf;
    lo.points = &points_;
    lo.decal_fix = look_.decal_fix;
    lo.world_clip_floor = wclip_floor_; lo.world_clip_box = wclip_box_;
    const float dt = have_time_ ? std::fmax(0.f, float(rf.display_time_ns - last_time_) * 1e-9f) : 1.f / 30.f;
    dev_->begin_frame();
    light_.encode(*dev_, rf);
    renderer_->draw_world(env_, *room_frame_, room, rf.view_count, 0.f, kRoomClear);
    draw_ar_layer(*renderer_, ar_, rf, lo, dt);
    dev_->encode_composite(final_, nullptr, env_, ar_);
    if (tex) dev_->encode_composite(0, tex, env_, ar_);
    ar_runtime_submit_frame(rt_, &rf);
    inflight_.fetch_add(1);
    dev_->end_frame(on_done, new Done{rt_, rf, &inflight_}, drawable);
    last_time_ = rf.display_time_ns; have_time_ = true;
    return true;
}
void HarnessSession::set_look(const LookSettings &s) {
    look_ = s;
    dev_->set_edge_coverage(s.edge_aa);
    dev_->set_texture_filter(s.tex_smooth ? TextureFilter::smooth : TextureFilter::vanilla);
    light_.begin(*dev_, look_, ar_render_frame{});
}
}

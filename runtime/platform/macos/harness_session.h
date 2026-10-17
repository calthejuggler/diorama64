#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
extern "C" {
#include "ar_runtime.h"
#include "fc_builder.h"
}
#include "harness_platform.h"
#include "look.h"
#include "render_device.h"
#include "renderer.h"
#include "room_light.h"
#include "room_light_driver.h"
namespace ar {
class HarnessSession {
public:
    HarnessSession(const ar_adapter *adapter, HarnessPlatform &platform, void *mtl_device = nullptr);
    ~HarnessSession();
    bool init(const std::string &rom, const std::string &save_dir, std::string *error);
    void warmup(uint32_t ticks);
    void start_game_thread();
    bool frame(bool sync_ticks, void *drawable_texture, void *drawable);
    void wait_idle();
    void set_audio_mode(uint32_t m) { audio_mode_ = m; }   // before init
    uint32_t pull_audio_scene(ar_audio_pull *p) { return rt_ ? ar_runtime_pull_audio_scene(rt_, p) : 0; }
    uint32_t pull_audio(int16_t *stereo, uint32_t frames) { return rt_ ? ar_runtime_pull_audio(rt_, stereo, frames) : 0; }
    bool read_final(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);
    bool read_ar(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);
    bool read_ar_depth(std::vector<float> &depth, uint32_t &w, uint32_t &h);
    bool read_game_view(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);
    uint64_t game_view_renders() const;
    void set_overlay_panel(bool on) { overlay_ = on; }
    void set_silhouette(bool on) { silhouette_ = on; }
    void set_flat_hud(bool on) { flat_hud_ = on; }
    void set_world_clip(bool floor, bool box) { wclip_floor_ = floor; wclip_box_ = box; }
    void set_debug_points(const std::vector<DebugPoint> &points) { points_ = points; }
    // Default Original N64, so scripts see unchanged pixels. set_look sends the key light's steering at once, so
    // warm-up ticks already use it.
    void set_look(const LookSettings &s);
    void set_key_light_callback(void (*fn)(void *ctx, int on, const float dir[3]), void *ctx) { light_.set_key_light_callback(fn, ctx); }
    RoomLightStatus room_light_status() const { return light_.status(); }
    void *mtl_device();
    uint64_t ticks_done() const;
    ar_runtime *runtime() const { return rt_; }
private:
    const ar_adapter *adapter_;
    HarnessPlatform &platform_;
    std::unique_ptr<IRenderDevice> dev_;
    std::unique_ptr<Renderer> renderer_;
    ar_runtime *rt_ = nullptr;
    uint32_t audio_mode_ = AR_AUDIO_MODE_STEREO;
    fc_builder *room_ = nullptr;
    const fc_frame *room_frame_ = nullptr;
    uint32_t env_ = 0, ar_ = 0, final_ = 0, w_ = 0, h_ = 0;
    uint32_t samples_ = 0;
    bool overlay_ = true;
    bool silhouette_ = true;
    bool flat_hud_ = false;
    std::vector<DebugPoint> points_;
    LookSettings look_ = [] { LookSettings s; s.roomlit = false; s.msaa = 1; return s; }();
    RoomLightDriver light_;
    int64_t last_time_ = 0;
    bool have_time_ = false;
    std::atomic<int> inflight_{0};
    std::atomic<bool> wclip_floor_{false}, wclip_box_{false};
};
}

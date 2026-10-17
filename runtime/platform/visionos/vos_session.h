// One per process; survives the immersive space closing and reopening (the game boots once).
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
extern "C" {
#include "ar_runtime.h"
#include "fc_builder.h"
}
#include "ar_audio_settings.h"
#include "ar_layer.h"
#include "look.h"
#include "vos_safe_mode.h"
#include "render_device.h"
#include "renderer.h"
#include "room_light.h"
#include "room_light_driver.h"
#include "vos_audio.h"
#include "vos_foveation.h"
#include "vos_options.h"
#include "vos_perf.h"
#include "vos_phase.h"
#include "vos_platform.h"
#include "vos_scene_pos.h"
#include "vos_state.h"
namespace ar {
class VosSession {
public:
    VosSession(const ar_adapter *adapter, VosPlatform &platform, const VosOptions &o, const std::string &documents_dir);
    // Layer thread. Boots the game once.
    bool ensure_started(void *mtl_device, std::string *error);
    bool game_entered() const { return game_entered_; }
    // Any thread: set before the game boots, cleared at the post-boot RemoteIO fallback.
    bool audio_spatial() const { return spatial_ok_.load(std::memory_order_acquire); }
    bool frame();
    // Layer thread, around each layer's frame loop: audio plays only while a layer is attached.
    void layer_attached();
    void layer_detached();
    uint64_t ticks_done() const;
    std::string status() const;
    ar_runtime *runtime() const { return rt_; }
    void set_replay(const sarec *rec) { replay_ = rec; }                 // before ensure_started; rec outlives the session
    // Before ensure_started; out outlives the session. Saved whenever a layer detaches.
    void set_recorder(sarec *out, const std::string &path) { recorder_ = out; record_path_ = path; }
    void set_save_folder(const std::string &name, bool fresh) { save_folder_ = name; save_fresh_ = fresh; }
    // Before ensure_started; the store outlives the session. The steering callback runs on the layer thread.
    void set_look_store(const LookStore *store) { look_store_ = store; }
    void set_world_clip(bool floor, bool box) { wclip_floor_ = floor; wclip_box_ = box; }
    bool foveation_refused() const { return !fov_refused_.empty(); }
    // Before ensure_started; the probe outlives the session (may be null).
    void set_render_safety(const std::string &failed, RenderProbe *probe) { render_failed_ = failed; probe_ = probe; }
    LookSettings effective_look() const;
    void apply_tex_filter();
    void set_key_light_callback(void (*fn)(void *ctx, int on, const float dir[3]), void *ctx) { light_.set_key_light_callback(fn, ctx); }
    void set_perf_window_callback(void (*fn)(void *ctx), void *ctx) { perf_window_fn_ = fn; perf_window_ctx_ = ctx; }
    // Before ensure_started; the store outlives the session. Read on the layer thread when its generation changes,
    // before PHASE's update.
    void set_audio_store(const AudioStore *store) { audio_store_ = store; }
    // Before ensure_started; outlives the session.
    void set_feed_filter(const std::atomic<int> *f) { feed_filter_ = f; }
    void set_room_light_status(RoomLightStatusBox *box) { light_box_ = box; }
private:
    LightingFrame update_lighting(const ar_render_frame &rf);
    FoveationPlan plan_foveation(bool layered, uint32_t view_count, VosPlatform::RateMap *rm);
    void log_foveation(const VosPlatform::RateMap &rm, const Viewport &vp);
    const LookStore *look_store_ = nullptr;
    uint64_t look_gen_ = 0;
    std::string render_failed_;
    RenderProbe *probe_ = nullptr;
    bool probe_frame_ = false;
    uint32_t msaa_warming_ = 0;
    std::unique_ptr<RenderProbeScope> msaa_probe_;
    bool rate_map_probed_ = false;
    int safe_logged_ = -1;
    std::string probe_config() const;
    const AudioStore *audio_store_ = nullptr;
    uint64_t audio_gen_ = 0;
    const std::atomic<int> *feed_filter_ = nullptr;
    int feed_filter_now_ = -1;
    std::unique_ptr<VosPhase> make_phase();
    bool show_sources_ = false;
    std::vector<DebugPoint> sound_points_;
    LookSettings look_{};
    RoomLightDriver light_;
    RoomLightStatusBox *light_box_ = nullptr;
    std::string light_reduce_error_logged_;
    void log_perf_window();
    void (*perf_window_fn_)(void *) = nullptr;
    void *perf_window_ctx_ = nullptr;
    int light_room_logged_ = -1;
    uint64_t ao_bakes_logged_ = 0;
    uint64_t ao_failures_logged_ = 0;
    bool fail(const std::string &why, std::string *error);
    void step_ticks();
    void update_hold();
    void report_replay();
    void warm_pipelines();
    void save_pipelines();
    void warm_in_background(uint32_t samples);
    void record_perf(int64_t display_ns, std::chrono::steady_clock::time_point cpu_start, uint64_t ticks_before);
    const ar_adapter *adapter_;
    VosPlatform &platform_;
    VosOptions o_;
    std::string docs_;
    std::string save_folder_ = "save";
    bool save_fresh_ = false;
    HoldSchedule hold_;
    bool holding_ = false;
    std::unique_ptr<IRenderDevice> dev_;
    std::unique_ptr<Renderer> renderer_;
    ar_runtime *rt_ = nullptr;
    fc_builder *table_ = nullptr;
    const fc_frame *table_frame_ = nullptr;
    uint32_t ar_target_ = 0, w_ = 0, h_ = 0, layers_ = 0;
    uint32_t samples_ = 0;
    std::string fov_logged_;
    uint32_t fov_lines_ = 0;
    std::string fov_refused_;
    bool started_ = false, failed_ = false, logged_first_ = false, have_time_ = false, game_entered_ = false;
    int logged_linearize_ = -1;
    uint8_t logged_colour_ = 0xFF;
    int edge_aa_ = -1;
    int tex_smooth_ = -1;
    std::string error_;
    int64_t last_time_ = 0;
    uint64_t frames_ = 0;
    std::atomic<int> inflight_{0};
    std::atomic<bool> wclip_floor_{false}, wclip_box_{false};
    std::unique_ptr<VosAudio> audio_;
    std::unique_ptr<VosPhase> phase_;
    std::vector<PhaseRungPlan> phase_plan_;
    // PHASE paths whose voices failed, stopped and kept for the whole process (a tap writer or a render
// block may still hold them); at most one per rung.
    std::vector<std::unique_ptr<VosPhase>> retired_phases_;
    void phase_voices_failed(const std::string &why);
    AudioBackend backend_ = AudioBackend::none;
    std::atomic<bool> spatial_ok_{false};
    bool audio_failed_ = false;
    bool tap_started_ = false;
    double step_ms_ = 0.0;
    int64_t perf_prev_ns_ = 0;
    PerfWindow perf_;
    size_t pipelines_saved_ = 0;
    size_t at_draw_queued_ = 0;
    std::string pipeline_file_;
    const sarec *replay_ = nullptr;
    sarec *recorder_ = nullptr;
    std::string record_path_;
    bool logged_diverged_ = false, logged_done_ = false;
};
}

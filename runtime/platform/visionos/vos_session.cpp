// runtime/platform/visionos/vos_session.cpp — one frame: begin (platform pose + table) → game ticks →
// AR layer into our target → resolve into the compositor's views → submit (device anchor) → present → end submission.
#include "ar_mem_stats.h"
#include "vos_session.h"
#include "pipeline_warm.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <thread>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <TargetConditionals.h>
#include "metal_device.h"
#include "scene_shapes.h"
#include "vos_color.h"
#include "vos_foveation.h"
#include "vos_log.h"
#include "vos_safe_mode.h"
#include "vos_scene_audio.h"
namespace ar {
namespace {
struct Done { ar_runtime *rt; ar_render_frame f; std::atomic<int> *inflight; };
void on_done(void *ctx) {   // Metal completion thread: release the snapshot the GPU was reading
    auto *d = static_cast<Done *>(ctx);
    ar_runtime_end_frame(d->rt, &d->f);
    d->inflight->fetch_sub(1);
    delete d;
}
void upload(void *user, uint32_t id, uint32_t w, uint32_t h, const uint8_t *rgba) { static_cast<IRenderDevice *>(user)->upload_texture(id, w, h, rgba); }
bool file_exists(const std::string &p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
bool read_text(const std::string &path, std::string *out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char b[4096];
    size_t n;
    out->clear();
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out->append(b, n);
    std::fclose(f);
    return true;
}
bool write_text(const std::string &path, const std::string &text) {
    const std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) { ::unlink(tmp.c_str()); return false; }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
struct TickLog { const ar_adapter *adapter; uint64_t every; };
void log_tick(void *ctx, uint64_t n) {
    auto *t = static_cast<TickLog *>(ctx);
    if (t->every && n % t->every == 0) {
        const char *s = t->adapter->status_line ? t->adapter->status_line() : "";
        vos_log("AR_TICK n=%llu hash=0x%016llx status=\"%s\"", (unsigned long long)n, (unsigned long long)t->adapter->state_hash(), s);
    }
}
}

VosSession::VosSession(const ar_adapter *a, VosPlatform &p, const VosOptions &o, const std::string &docs)
    : adapter_(a), platform_(p), o_(o), docs_(docs), hold_(o.hold_ticks) {
    light_.set_log_callback([](void *, const char *line) { vos_log("%s", line); }, nullptr);
}

bool VosSession::fail(const std::string &why, std::string *error) {
    failed_ = true;
    error_ = why;
    *error = why;
    vos_log("AR_ERROR %s", why.c_str());
    return false;
}

bool VosSession::ensure_started(void *mtl, std::string *error) {
    if (started_) return true;
    if (failed_) { *error = error_; return false; }
    dev_ = create_metal_device(mtl);
    // The layer thread never compiles a pipeline (a fresh install was killed for it): a draw whose pipeline is not
    // built yet is skipped while the background warm-up compiles it.
    dev_->set_async_pipelines(true);
    renderer_.reset(new Renderer(*dev_));
    ar_target_ = dev_->create_target(1, 1);
    look_ = effective_look();
    apply_tex_filter();                                 // before the game uploads any texture
    {
        RenderProbeScope probe(probe_, RenderRisk::pipelines, probe_config());
        warm_pipelines();
    }
    warm_in_background(dev_->supported_samples(look_.msaa, platform_.layered() ? 2 : 1));
    probe_frame_ = true;
    // On the device the fake table would hide the level's lowest geometry under an opaque slab, so only --fake-table
    // draws it there.
    const bool fake_table = fake_table_enabled(o_.fake_table, TARGET_OS_SIMULATOR != 0);
    vos_log("AR_RENDER fake_table=%d", fake_table ? 1 : 0);
    if (fake_table) {
        table_ = fc_builder_create();
        fc_builder_reset(table_, 0, 320, 240);
        build_fake_table(table_, kFakeTableHalfX, kFakeTableHalfZ);
        table_frame_ = fc_builder_finish(table_);
    }
    rt_ = ar_runtime_create(adapter_, platform_.api());
    if (!rt_) return fail("adapter or platform ABI mismatch", error);
    ar_runtime_set_texture_sink(rt_, upload, dev_.get());
    // the audio mode is fixed before the game boots, so PHASE's engine starts here; if it cannot, the game runs in
// stereo mode through RemoteIO.
#if TARGET_OS_SIMULATOR
    const bool simulator = true;
#else
    const bool simulator = false;
#endif
    const AudioDecision ad = decide_audio_backend(rt_, o_.audio_backend, o_.audio, [&](std::string *perr) {
        phase_ = make_phase();
        const AudioSpatializer sp = audio_store_ ? audio_store_->get().spatializer : AudioSpatializer::binaural;
        phase_plan_ = phase_rungs(o_.phase_mode, o_.phase_head, simulator, true);
        bool ok = phase_->start_engine(phase_plan_, sp, perr);
        if (ok && o_.phase_fail_test) {
            ok = false;
            *perr = "forced failure (--phase-fail-test, test only)";
        }
        if (!ok) phase_.reset();
        return ok;
    });
    if (ad.stereo_only_adapter) vos_log("AR_AUDIO backend=remoteio reason=stereo-only adapter");
    if (ad.fell_back) {
        vos_log("AR_ERROR audio: PHASE: %s fallback=remoteio mode=%s", ad.error.c_str(),
                ar_runtime_audio_mode(rt_) == AR_AUDIO_MODE_STEREO ? "stereo" : "sources");
        vos_log("AR_AUDIO phase rung=remoteio");
    }
    backend_ = ad.backend;
    spatial_ok_.store(backend_ == AudioBackend::phase, std::memory_order_release);   // before the boot publishes the game's state
    std::string folder, serr;
    bool deleted = false;
    if (!prepare_save_folder(docs_, save_folder_, save_fresh_, &folder, &deleted, &serr)) return fail(serr, error);
    if (deleted) vos_log("AR_SAVE folder=%s fresh=1", folder.c_str());
    const std::string save = docs_ + "/" + folder;
    if (::mkdir(save.c_str(), 0755) != 0 && errno != EEXIST)
        vos_log("AR_ERROR cannot create Documents/%s: %s", folder.c_str(), std::strerror(errno));
    if (::access(save.c_str(), W_OK) != 0)
        return fail("Documents/" + folder + " is not writable (created by devicectl before the app's first launch): delete the "
                    "app and install it again", error);
    const std::string rom = resolve_documents_path(docs_, o_.rom);
    game_entered_ = true;   // from here a failure may leave part of the game started
    if (!ar_runtime_init_game(rt_, rom.c_str(), save.c_str())) return fail(adapter_->last_error(), error);
    if (replay_) ar_runtime_set_replay(rt_, replay_);      // before any tick: the replay index is the tick count
    if (recorder_) ar_runtime_set_recorder(rt_, recorder_);
    vos_log("AR_GAME started game=%s", adapter_->name);
    if (backend_ == AudioBackend::phase) {
        std::string verr;
        if (phase_->start_voices(&verr))
            vos_log("AR_AUDIO started backend=phase render=%s head=%s rate=48000 lanes=%u rung=%s", phase_->render_mode(), phase_->head_mode(),
                    AR_AUDIO_MAX_SOURCES, phase_->rung());
        else phase_voices_failed(verr);
    } else if (backend_ == AudioBackend::remoteio) {
        audio_.reset(new VosAudio(rt_));
        std::string aerr;
        if (audio_->start(o_.audio_tap_s, &aerr)) vos_log("AR_AUDIO started backend=remoteio rate=48000");
        else { vos_log("AR_ERROR audio: %s (continuing without sound)", aerr.c_str()); audio_.reset(); audio_failed_ = true; }
    }
    started_ = true;
    return true;
}

// A draw pipeline compiles at its combiner's first draw, which stalled the layer thread for up to ~1 s on a level's
// first frame. Documents/pipelines.txt lists every pipeline this app has built; the next launch compiles it before its
// first frame.
void VosSession::warm_pipelines() {
    std::string list;
    if (!read_text(docs_ + "/pipelines.txt", &list)) { vos_log("AR_WARMUP none"); return; }
    pipeline_file_ = list;
    vos_log("AR_WARMUP list_bytes=%zu (background)", list.size());
}
void VosSession::warm_in_background(uint32_t samples) {
    dev_->prepare_passes();
    const size_t n = dev_->warm_pipelines_background(merge_pipeline_lists(pipeline_file_, dev_->pipeline_list()),
                                                     Renderer::fixed_pipeline_draws(), samples);
    vos_log("AR_WARMUP background=%zu samples=%u", n, samples);
}

void VosSession::save_pipelines() {
    const size_t n = dev_->pipeline_count();
    if (n == pipelines_saved_) return;
    pipelines_saved_ = n;
    const size_t at_draw = dev_->draw_time_builds();
    if (at_draw != at_draw_queued_) {
        at_draw_queued_ = at_draw;
        dev_->warm_pipelines_background(dev_->pipeline_list(), {}, samples_ ? samples_ : 1);
    }
    pipeline_file_ = merge_pipeline_lists(pipeline_file_, dev_->pipeline_list());
    vos_log("AR_PIPELINES built=%zu at_draw=%zu tick=%llu saved=%d", n, at_draw, (unsigned long long)ar_runtime_ticks_done(rt_),
            (int)write_text(docs_ + "/pipelines.txt", pipeline_file_));
}

void VosSession::layer_attached() {
    if (phase_ && !phase_->running()) { phase_->resume(); vos_log("AR_AUDIO resumed backend=phase running=%d", (int)phase_->running()); }
    if (!audio_ || audio_->running()) return;
    audio_->resume();
    vos_log("AR_AUDIO resumed running=%d", (int)audio_->running());
}

std::unique_ptr<VosPhase> VosSession::make_phase() {
    const int ff = feed_filter_ ? feed_filter_->load() : -1;
    const FeedChoice fc = feed_choice(ff);
    std::unique_ptr<VosPhase> p(new VosPhase(rt_, o_.audio_tap_s, fc.polyphase));
    if (fc.polyphase) p->set_poly_mode(fc.mode);
    feed_filter_now_ = fc.polyphase ? ff : -1;
    vos_log("AR_AUDIO resampler=%s", !fc.polyphase ? "basic" : fc.mode == PolyMode::linear ? "linear" : "sinc");
    return p;
}

void VosSession::phase_voices_failed(const std::string &why) {
    const AudioSpatializer sp = audio_store_ ? audio_store_->get().spatializer : AudioSpatializer::binaural;
    std::vector<PhaseRungPlan> rest = phase_rungs_after(phase_plan_, phase_->started_rung());
    vos_log("AR_ERROR audio: PHASE voices rung=%s: %s; next=%s", phase_->rung(), why.c_str(),
            rest.empty() ? "remoteio" : phase_rung_name(rest[0].rung));
    phase_->stop();
    retired_phases_.push_back(std::move(phase_));
    while (!rest.empty()) {
        std::unique_ptr<VosPhase> p = make_phase();
        std::string err, verr;
        if (!p->start_engine(rest, sp, &err)) { vos_log("AR_ERROR audio: PHASE: %s", err.c_str()); retired_phases_.push_back(std::move(p)); break; }
        if (p->start_voices(&verr)) {
            phase_ = std::move(p);
            audio_gen_ = ~uint64_t(0);
            vos_log("AR_AUDIO started backend=phase render=%s head=%s rate=48000 lanes=%u rung=%s (after a voice failure)",
                    phase_->render_mode(), phase_->head_mode(), AR_AUDIO_MAX_SOURCES, phase_->rung());
            return;
        }
        vos_log("AR_ERROR audio: PHASE voices rung=%s: %s", p->rung(), verr.c_str());
        rest = phase_rungs_after(rest, p->started_rung());
        p->stop();
        retired_phases_.push_back(std::move(p));
    }
    backend_ = AudioBackend::remoteio;
    spatial_ok_.store(false, std::memory_order_release);
    vos_log("AR_AUDIO phase rung=remoteio (voices failed)");
    audio_.reset(new VosAudio(rt_));
    std::string aerr;
    if (audio_->start(o_.audio_tap_s, &aerr))
        vos_log("AR_AUDIO started backend=remoteio rate=48000 lanes_mixed=%d", (int)audio_->mixes_lanes());
    else { vos_log("AR_ERROR audio: %s (continuing without sound)", aerr.c_str()); audio_.reset(); audio_failed_ = true; }
}

void VosSession::log_perf_window() {
    vos_log("%s", perf_line(perf_.report()).c_str());
    vos_log("AR_MEM phys_mb=%.1f", ar_phys_footprint_mb());
    vos_log("AR_WARMUP deferred_draws=%llu warming=%zu built=%zu promoted=%llu", (unsigned long long)dev_->deferred_draws(),
            dev_->pipelines_warming(), dev_->pipeline_count(), (unsigned long long)dev_->warm_promotions());
    if (perf_window_fn_) perf_window_fn_(perf_window_ctx_);
    perf_.reset();
}

void VosSession::layer_detached() {
    if (perf_.frames() > 0) log_perf_window();
    perf_prev_ns_ = 0;
    if (recorder_ && !record_path_.empty())   // the game steps on the layer thread: no tick runs during the save
        vos_log("AR_RECORD saved=%d path=%s ticks=%u dropped=%u", sarec_save(recorder_, record_path_.c_str()), record_path_.c_str(), recorder_->count, recorder_->dropped);
    if (phase_) {   // always: a space closed during an interruption must not restart when the interruption ends
        const bool was = phase_->running();
        phase_->pause();
        vos_log("AR_AUDIO paused backend=phase was_running=%d %s", (int)was, phase_->counters().c_str());
    }
    if (!audio_ || !audio_->running()) return;
    audio_->pause();
    vos_log("AR_AUDIO paused frames=%llu missing=%llu", (unsigned long long)audio_->frames_out(), (unsigned long long)audio_->missing_in());
}

uint64_t VosSession::ticks_done() const { return rt_ ? ar_runtime_ticks_done(rt_) : 0; }

std::string VosSession::status() const {
    char b[256];
    std::snprintf(b, sizeof b, "%s: tick %llu, table %s%s%s", adapter_->name, (unsigned long long)ticks_done(),
                  platform_.placed() ? "placed" : "not placed yet", holding_ ? ", holding (--hold-ticks)" : "",
                  audio_failed_ ? ", no sound (see log)" : "");
    std::string s = b;
    if (platform_.pad_lost()) s += ", controller disconnected — reconnect it to continue";
    const std::string hint = platform_.placement_hint();
    if (!hint.empty()) s += ", " + hint;
    return s;
}

void VosSession::step_ticks() {
    TickLog tl{adapter_, o_.log_hash_every};
    vos_step_due_ticks(rt_, hold_.limit(), log_tick, &tl);
}

void VosSession::report_replay() {
    if (!replay_) return;
    const ar_replay_status st = ar_runtime_replay_status(rt_);
    if (st.diverged && !logged_diverged_) {
        vos_log("AR_REPLAY diverged tick=%llu expected=0x%016llx got=0x%016llx", (unsigned long long)st.first_bad_tick,
                (unsigned long long)st.expected, (unsigned long long)st.got);
        logged_diverged_ = true;
    }
    if (st.done && !logged_done_) {
        vos_log("AR_REPLAY done played=%llu checked=%llu diverged=%d", (unsigned long long)st.played,
                (unsigned long long)st.checked, st.diverged);
        logged_done_ = true;
    }
}

void VosSession::update_hold() {
    const uint64_t n = ar_runtime_ticks_done(rt_);
    if (!holding_ && hold_.holding(n)) {
        holding_ = true;
        platform_.set_holding(true);
        vos_log("AR_HOLD tick=%llu", (unsigned long long)n);
    }
    if (holding_ && frames_ % 10 == 0) {
        const std::string resume = docs_ + "/resume-" + std::to_string(hold_.current());
        if (file_exists(resume)) {
            ::unlink(resume.c_str());
            vos_log("AR_RESUME tick=%llu", (unsigned long long)hold_.current());
            hold_.release();
            holding_ = false;
            platform_.set_holding(false);
        }
    }
}

void VosSession::record_perf(int64_t display_ns, std::chrono::steady_clock::time_point c0, uint64_t ticks_before) {
    const double cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
    const uint32_t k = (uint32_t)(ar_runtime_ticks_done(rt_) - ticks_before);
    perf_.frame(display_ns, cpu_ms, k, dev_->last_gpu_ms());
    const double gap_ms = perf_prev_ns_ ? double(display_ns - perf_prev_ns_) * 1e-6 : 0.0;
    perf_prev_ns_ = display_ns;
    if (cpu_ms > 100.0 || gap_ms > 100.0)
        vos_log("AR_HITCH tick=%llu gap_ms=%.1f cpu_ms=%.1f step_ms=%.1f ticks=%u", (unsigned long long)ar_runtime_ticks_done(rt_),
                gap_ms, cpu_ms, step_ms_, k);
    const RoomLightStatus ls = light_.status();
    const bool flipped = (ls.room ? 1 : 0) != light_room_logged_;
    bool window = false;
    if (perf_.frames() >= 900) { log_perf_window(); window = true; }
    if (ls.reduce_error[0] && light_reduce_error_logged_ != ls.reduce_error) {
        vos_log("AR_LIGHT reduce_error=%s", ls.reduce_error);
        light_reduce_error_logged_ = ls.reduce_error;
    }
    if (window && dev_) vos_log("AR_MSAA samples=%u late_passes=%llu", samples_, (unsigned long long)dev_->late_passes());
    if (window || flipped) {
        vos_log("AR_LIGHT %s", room_light_line(ls).c_str());
        light_room_logged_ = ls.room ? 1 : 0;
    }
    if (renderer_) {
        const uint64_t bakes = renderer_->ao_bakes();
        uint64_t timed = 0;
        double bake_ms = -1.0;
        const bool have = dev_->ao_bake_time(&timed, &bake_ms);
        if (bakes > 0 && ((bakes != ao_bakes_logged_ && have && timed == bakes) || window)) {
            const AoVolume v = renderer_->last_ao();
            vos_log("AR_AO bakes=%llu dims=%u,%u,%u voxel=%.1f bake ms=%.2f cpu_ms=%.2f", (unsigned long long)bakes, v.dims[0],
                    v.dims[1], v.dims[2], v.voxel, have && timed == bakes ? bake_ms : -1.0, renderer_->last_ao_cpu_ms());
            ao_bakes_logged_ = bakes;
        }
        if (renderer_->ao_failures() != ao_failures_logged_) {
            ao_failures_logged_ = renderer_->ao_failures();
            vos_log("AR_AO failed=%llu", (unsigned long long)ao_failures_logged_);
        }
    }
}

LookSettings VosSession::effective_look() const {
    const LookSettings wanted = look_store_ ? look_store_->get() : look_;
    return safe_mode_look(wanted, render_failed_, nullptr);
}
void VosSession::apply_tex_filter() {
    if (int(look_.edge_aa) != edge_aa_) {
        edge_aa_ = int(look_.edge_aa);
        dev_->set_edge_coverage(look_.edge_aa);
        vos_log("AR_RENDER edge_aa=%d", edge_aa_);
    }
    if (int(look_.tex_smooth) == tex_smooth_) return;
    tex_smooth_ = int(look_.tex_smooth);
    dev_->set_texture_filter(look_.tex_smooth ? TextureFilter::smooth : TextureFilter::vanilla);
    vos_log("AR_RENDER tex_filter=%s", look_.tex_smooth ? "smooth" : "vanilla");
}
std::string VosSession::probe_config() const { return render_config_key(render_config_of(look_)); }
LightingFrame VosSession::update_lighting(const ar_render_frame &rf) {
    if (look_store_ && look_store_->generation() != look_gen_) {
        bool safe = false;
        const LookSettings wanted = look_store_->get(&look_gen_);
        look_ = safe_mode_look(wanted, render_failed_, &safe);
        if ((safe ? 1 : 0) != safe_logged_) {
            if (safe || safe_logged_ == 1)
                vos_log("AR_SAFE_MODE %s config=%s using=%s", safe ? "fallback=probe_crash" : "lifted",
                        render_config_key(render_config_of(wanted)).c_str(), render_config_key(render_config_of(look_)).c_str());
            safe_logged_ = safe ? 1 : 0;
        }
    }
    const LightingFrame lf = light_.begin(*dev_, look_, rf);
    if (light_box_) light_box_->set(light_.status());
    return lf;
}
FoveationPlan VosSession::plan_foveation(bool layered, uint32_t view_count, VosPlatform::RateMap *out) {
    VosPlatform::RateMap &rm = *out;
    if (!platform_.rate_map(&rm)) return FoveationPlan{};
    RateMapShape shape;
    shape.maps = rm.maps; shape.layers = rm.layers;
    shape.screen[0] = rm.screen[0]; shape.screen[1] = rm.screen[1];
    shape.physical = rm.physical;
    shape.texture[0] = rm.texture[0]; shape.texture[1] = rm.texture[1];
    const FoveationPlan p = vos_foveation_plan(shape, layered, view_count);
    if (!p.use && fov_refused_ != p.reason) {
        fov_refused_ = p.reason;
        vos_log("AR_ERROR foveation: %s (maps=%u layers=%u screen=%ux%u physical=%ux%u tex=%ux%u layered=%d views=%u); rendering "
                "without the rate map (the picture may be warped: use --foveation off)", p.reason, rm.maps, rm.layers, rm.screen[0],
                rm.screen[1], rm.physical[0][0], rm.physical[0][1], rm.texture[0], rm.texture[1], layered ? 1 : 0, view_count);
    }
    return p;
}
void VosSession::log_foveation(const VosPlatform::RateMap &rm, const Viewport &vp) {
    char b[256];
    std::snprintf(b, sizeof b, "screen=%ux%u physical=%ux%u tex=%ux%u vp=%g,%g,%gx%g maps=%u layers=%u", rm.screen[0], rm.screen[1],
                  rm.physical[0][0], rm.physical[0][1], rm.texture[0], rm.texture[1], (double)vp.x, (double)vp.y, (double)vp.w,
                  (double)vp.h, rm.maps, rm.layers);
    if (fov_logged_ == b) return;
    fov_logged_ = b;
    if (++fov_lines_ <= 20) vos_log("AR_FOVEATION %s%s", b, fov_lines_ == 20 ? " (further changes not logged)" : "");
}
bool VosSession::frame() {
    ar_render_frame rf{};
    if (!ar_runtime_begin_frame(rt_, &rf)) return false;
    const auto c0 = std::chrono::steady_clock::now();
    const uint64_t ticks_before = ar_runtime_ticks_done(rt_);
    const LightingFrame lf = update_lighting(rf);       // before step_ticks: this frame's ticks already use the steering
    // The ticks stepped below read this frame's head but are first shown by the next frame, so billboards face a head 1
    // to 3 display frames old; the view matrices always use the current head.
    step_ticks();
    if (audio_store_ && audio_store_->generation() != audio_gen_) {
        const AudioSettings a = audio_store_->get(&audio_gen_);
        show_sources_ = a.show_sources;
        if (phase_) {
            const ar_audio_tuning t = audio_tuning_from_settings(a);
            phase_->set_tuning(t);
            vos_log("AR_AUDIO tuning head_focus=%g distance_exponent=%g spotlight=%g cone=%g widen=%g sfx_db=%g music_db=%g",
                    (double)a.head_focus, (double)t.distance_exponent, (double)t.spotlight_floor_db, (double)t.spotlight_cone_deg,
                    (double)t.widen, (double)t.sfx_db, (double)t.music_db);
            phase_->set_spatializer(a.spatializer);
        }
    }
    if (phase_ && feed_filter_ && feed_filter_now_ >= 0) {
        const int ff = feed_filter_->load();
        const FeedChoice fc = feed_choice(ff);
        if (fc.polyphase && ff != feed_filter_now_) {
            phase_->set_poly_mode(fc.mode);
            feed_filter_now_ = ff;
            vos_log("AR_AUDIO resampler=%s", fc.mode == PolyMode::linear ? "linear" : "sinc");
        }
    }
    if (phase_) phase_->update(rf);
    if (phase_ && show_sources_)
        phase_->sound_points(&sound_points_, layer_size_factor(rf.level.units_per_meter, rf.level.reference_units_per_meter));
    else sound_points_.clear();
    step_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
    report_replay();
    update_hold();
    frames_++;
    uint32_t w = 0, h = 0;
    if (vos_submit_if_empty(rt_, rf, &w, &h)) {
        // Nothing is presented: without a built-in drawable there is nothing to present, and a drawable whose view has
        // no pixels cannot be rendered into; ending the frame's submission without a present leaves the compositor
        // showing the previous frame, as the original spike loop did for frames without a drawable.
        platform_.end_submission();
        record_perf(rf.display_time_ns, c0, ticks_before);
        return true;
    }
    const bool layered = platform_.layered();
    uint32_t tw = w, th = h, tl = 1;
    if (layered) {
        tw = 0; th = h; tl = rf.view_count;
        for (uint32_t i = 0; i < rf.view_count; i++) tw = std::max(tw, rf.views[i].width);
    }
    // foveation. With the compositor's rate map the target is the drawable texture's size (physical texels),
// the passes' viewports are the map's screen size, and only the target's own encoder is rate-mapped.
    VosPlatform::RateMap rm;
    const FoveationPlan fov = plan_foveation(layered, rf.view_count, &rm);
    if (fov.use) { tw = fov.target_w; th = fov.target_h; tl = fov.target_layers; }
    // A change recreates the target; its pipelines are first warmed in the background (under the crash probe) while
    // frames keep the old count, so the change never stalls the layer thread.
    uint32_t samples = dev_->supported_samples(look_.msaa, tl);
    if (samples != samples_ && samples_ != 0) {
        if (msaa_warming_ != samples) {
            msaa_warming_ = samples;
            msaa_probe_.reset();
            msaa_probe_.reset(new RenderProbeScope(probe_, RenderRisk::pipelines, probe_config()));
            const size_t n = dev_->warm_pipelines_background(merge_pipeline_lists(pipeline_file_, dev_->pipeline_list()),
                                                             Renderer::fixed_pipeline_draws(), samples);
            vos_log("AR_WARMUP samples=%u background=%zu (applies once built)", samples, n);
        }
        if (dev_->pipelines_warming() > 0) samples = samples_;
    } else if (msaa_probe_ && dev_->pipelines_warming() == 0) {
        msaa_probe_.reset();
        msaa_warming_ = 0;
    }
    if (tw != w_ || th != h_ || tl != layers_ || samples != samples_) {
        dev_->wait_idle();
        if (samples != samples_ && samples_ != 0) {
            RenderProbeScope probe(probe_, RenderRisk::pipelines, probe_config());
            probe_frame_ = true;
            const auto t0 = std::chrono::steady_clock::now();
            const size_t n = dev_->warm_pipelines(merge_pipeline_lists(pipeline_file_, dev_->pipeline_list()), samples);
            vos_log("AR_WARMUP samples=%u pipelines=%zu ms=%.0f", samples, n,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            msaa_probe_.reset();
            msaa_warming_ = 0;
        }
        dev_->resize_target(ar_target_, tw, th, tl, samples);
        if (samples != samples_)
            vos_log("AR_MSAA samples=%u requested=%u memoryless=%d late_passes=%llu", samples, (unsigned)look_.msaa,
                    dev_->memoryless_msaa() ? 1 : 0, (unsigned long long)dev_->late_passes());
        w_ = tw; h_ = th; layers_ = tl; samples_ = samples;
        vos_log("AR_RENDER layered=%d target=%ux%ux%u samples=%u", layered ? 1 : 0, tw, th, tl, samples);
    }
    // The crash probe over this frame's encoding when it is the first after a start or an MSAA change (pipelines not in the
    // warmed list compile at their first draw) or the first with a rate map; cleared once it is submitted.
    const bool probe_this = probe_frame_ || (fov.use && !rate_map_probed_);
    std::unique_ptr<RenderProbeScope> frame_probe;
    if (probe_this) frame_probe.reset(new RenderProbeScope(probe_, RenderRisk::first_frame, probe_config()));
    probe_frame_ = false;
    rate_map_probed_ = rate_map_probed_ || fov.use;
    dev_->set_rate_map(ar_target_, fov.use ? rm.map : nullptr);
    ArLayerOptions lo;
    lo.layered = layered;
    lo.view_w = fov.view_w;
    lo.view_h = fov.view_h;
    lo.table = table_frame_;
    lo.lighting = &lf;
    lo.points = &sound_points_;
    const float dt = have_time_ ? std::max(0.f, float(rf.display_time_ns - last_time_) * 1e-9f) : 1.f / 90.f;
    lo.decal_fix = look_.decal_fix;
    lo.world_clip_floor = wclip_floor_; lo.world_clip_box = wclip_box_;
    apply_tex_filter();
    const bool linearize = vos_linearize(look_.colour, platform_.color_wants_linear());
    if (int(linearize) != logged_linearize_ || look_.colour != logged_colour_) {
        vos_log("AR_COLOR format=%u linear_target=%d colour=%s linearize=%d", platform_.color_format(), (int)platform_.color_wants_linear(),
                look_get(look_, "colour").c_str(), (int)linearize);
        logged_linearize_ = int(linearize);
        logged_colour_ = look_.colour;
    }
    dev_->begin_frame();
    light_.encode(*dev_, rf);
    draw_ar_layer(*renderer_, ar_target_, rf, lo, dt);
    for (uint32_t i = 0; i < rf.view_count; i++) {
        ExternalView ev{};
        if (!platform_.external_view(i, &ev)) continue;
        ev.src_x = layered ? 0 : rf.views[i].x;
        ev.src_y = 0;
        ev.src_layer = layered ? i : 0;
        ev.linearize = linearize;
        if (fov.use) {   // the physical texels 1:1, no rate map on the resolve: the compositor applies the map
            if (i == 0) log_foveation(rm, ev.viewport);
            ev.viewport = Viewport{0.f, 0.f, float(rm.physical[i][0]), float(rm.physical[i][1])};
        }
        dev_->encode_resolve(ar_target_, ev);
    }
    ar_runtime_submit_frame(rt_, &rf);
    platform_.encode_present(dev_->command_buffer());
    inflight_.fetch_add(1);
    dev_->end_frame(on_done, new Done{rt_, rf, &inflight_}, nullptr);
    platform_.end_submission();
    frame_probe.reset();
    for (std::string why; dev_->take_pipeline_error(&why);)
        vos_log("AR_ERROR pipeline refused: %s (MSAA now %u)", why.c_str(), dev_->supported_samples(4, layers_));
    save_pipelines();
    for (std::string key; dev_->take_deferred_miss(&key);)
        vos_log("AR_WARMUP miss=%s deferred", key.c_str());
    last_time_ = rf.display_time_ns;
    have_time_ = true;
    if (!logged_first_) { vos_log("AR_FIRST_FRAME views=%u size=%ux%u", rf.view_count, w, h); logged_first_ = true; }
    if (audio_) {
        if (frames_ % 900 == 0)
            vos_log("AR_AUDIO frames=%llu missing=%llu trimmed=%llu", (unsigned long long)audio_->frames_out(),
                    (unsigned long long)audio_->missing_in(), (unsigned long long)audio_->trimmed_in());
        if (!tap_started_ && audio_->tap_ready()) {   // a long tap is ~115 MB of WAV: never write it on the layer thread
            tap_started_ = true;
            VosAudio *a = audio_.get();                // the session and its audio live for the whole process
            const std::string path = docs_ + "/audio_tap.wav";
            std::thread([a, path] {
                const bool ok = a->write_tap(path);
                vos_log("AR_AUDIO tap written path=%s ok=%d frames=%llu", path.c_str(), (int)ok, (unsigned long long)a->frames_out());
            }).detach();
        }
    }
    if (phase_) {
        if (frames_ % 900 == 0) {
            vos_log("AR_AUDIO phase %s", phase_->counters().c_str());
            vos_log("AR_AUDIO lanes_dbg head=%s %s", phase_->head_mode(), phase_->lanes_debug().c_str());
        }
        if (phase_->voices_failed()) phase_voices_failed("PHASE reported a voice start as failed");
    }
    if (phase_) {
        // The feed's tap belongs to one thread at a time: the layer thread asks tap_ready() until it is, then hands the
        // tap to a writer thread (the thread's start orders the two) and never touches it again (tap_started_). A tap of
        // kSceneChannels channels is large: never write it on the layer thread.
        if (!tap_started_ && phase_->tap_ready()) {
            tap_started_ = true;
            VosPhase *p = phase_.get();               // the session and its PHASE paths (retired ones too) live for the whole process
            const std::string path = docs_ + "/audio_tap.wav";
            std::thread([p, path] { vos_log("AR_AUDIO tap written path=%s ok=%d", path.c_str(), (int)p->write_tap(path)); }).detach();
        }
    }
    record_perf(rf.display_time_ns, c0, ticks_before);
    return true;
}
}

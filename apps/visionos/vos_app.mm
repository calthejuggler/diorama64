#import <ARKit/ARKit.h>
#import <Foundation/Foundation.h>
#include "vos_api.h"
#include <TargetConditionals.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ar_adapter_sm64.h"
#include "sm64_ar_setup.h"
#include "sm64_packs.h"
#include "sm64_relevance.h"
#include "sm64_story.h"
#include "ar_audio_settings.h"
#include "ar_game_args.h"
#include "cube_adapter.h"
#include "look.h"
#include "room_light.h"
#include "scene_shapes.h"
#include "vos_log.h"
#include "vos_options.h"
#include "vos_pads.h"
#include "vos_platform.h"
#include "vos_safe_mode.h"
#include "vos_session.h"
namespace {
const std::vector<ar::GameSpec> &games() {
    static const std::vector<ar::GameSpec> g = {
        {"sm64", ar::PlaceParams{1.9f, 0.7f, 0.f}},
        {"cube", ar::PlaceParams{1.2f, 0.5f, 0.f}},
    };
    return g;
}
struct AppState {
    std::mutex m;                  // status, error, refusal
    std::mutex run;                // one layer loop at a time
    std::string bundle, docs, status = "not configured", error;
    ar::GameSettingLock game_lock;
    int start_in = 1;
    std::string refusal;
    std::chrono::steady_clock::time_point refusal_at;
    ar::VosOptions opts;
    bool options_ok = false;
    sarec rec{};                   // outlive the session
    sarec out{};
    std::unique_ptr<ar::VosPlatform> platform;   // created on the first layer thread, under `run`
    std::unique_ptr<ar::VosPads> pads;             // created by arvos_configure; polled on the main thread
    dispatch_source_t pad_timer = nil;
    std::unique_ptr<ar::VosSession> session;
    ar::LookStore look;
    ar::AudioStore audio;
    std::atomic<int> feed_filter{-1};
    ar::RoomLightStatusBox light_status;
    std::atomic<ar::VosPlatform *> live{nullptr};
    std::atomic<bool> background{false};
    std::string render_failed;
    ar::RenderProbe probe;
};
AppState &state() { static AppState s; return s; }
// synchronize: the process may abort right after the write.
NSString *const kRenderPending = @"render.pending", *const kRenderFailed = @"render.failed";
void probe_write(const char *config) {
    [[NSUserDefaults standardUserDefaults] setObject:@(config) forKey:kRenderPending];
    [[NSUserDefaults standardUserDefaults] synchronize];
}
void probe_clear() {
    if (![[NSUserDefaults standardUserDefaults] stringForKey:kRenderPending]) return;
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:kRenderPending];
    [[NSUserDefaults standardUserDefaults] synchronize];
}
ar::LookSettings effective_look() { return ar::safe_mode_look(state().look.get(), state().render_failed, nullptr); }
constexpr double kRefusalShowS = 8.0;
void set_status(const std::string &s) { std::lock_guard<std::mutex> g(state().m); state().status = s; }
const ar_adapter *pick_adapter(const std::string &game, const std::string &bundle, std::string *err) {
    if (game == "cube") return cube_adapter();
    if (game == "sm64") {
        const std::string res = bundle + "/res";
        struct stat st;
        if (::stat((res + "/lang/English.ini").c_str(), &st) != 0) {
            *err = "the app bundle has no res/lang/English.ini (apps/visionos/CMakeLists.txt POST_BUILD copy)";
            return nullptr;
        }
        ar_adapter_sm64_set_resource_dir(res.c_str());   // before the game boots: the bundle is flat (0002-Library-seams-and-host-hooks.patch)
        ar_adapter_sm64_set_pack_stamps_required(1);      // only packs staged from the Mac (stamp + sha256) load
        ar_adapter_sm64_set_bundled_pack_dir((bundle + "/Bundled/packs").c_str());
        ar_adapter_sm64_set_screen_log([](int screen, int story, uint32_t tick) {
            const char *name = sm64_screen_name(screen);
            ar::vos_log("AR_SCREEN name=%s story=%d tick=%u", name ? name : "?", story, tick);
        });
        ar_adapter_sm64_set_dropped_log([](const char *what, const char *why, uint32_t tick) {
            ar::vos_log("AR_LEVEL dropped %s: %s tick=%u", what, why, tick);
            AppState &st = state();
            std::lock_guard<std::mutex> g(st.m);
            st.refusal = std::string(what) + " dropped: " + why; st.refusal_at = std::chrono::steady_clock::now();
        });
        ar_adapter_sm64_set_touch_log([](const char *line, uint32_t tick) { ar::vos_log("AR_TOUCH %s tick=%u", line, tick); });
        ar_adapter_sm64_set_title_log([](const char *line, uint32_t tick) { ar::vos_log("AR_TITLE %s tick=%u", line, tick); });
        ar_adapter_sm64_set_title_place_on_request(1);
        // Only in the live AR runtime (the harness and replays stay vanilla), never while recording or replaying.
        ar_adapter_sm64_set_eyes_log([](const char *line, uint32_t tick) { ar::vos_log("AR_EYES %s tick=%u", line, tick); });
        ar_adapter_sm64_set_title_eyes_active(1);
        return ar_adapter_sm64();
    }
    *err = "unknown game " + game;
    return nullptr;
}
}

static void apply_audio_to_game(const ar::AudioSettings &a);

// SM64LookModel.load() iterates only the table's keys, so an unknown --game-set KEY would be ignored silently: log it.
static void log_unknown_game_set_keys() {
    if (state().options_ok && state().opts.game == "sm64")
        for (const auto &kv : state().opts.game_set) {
            char b[32];
            if (!ar_adapter_sm64_get_setting(kv.first.c_str(), b, sizeof b))
                ar::vos_log("AR_SETTING sm64.%s rejected (unknown key)", kv.first.c_str());
        }
}
extern "C" int arvos_configure(int argc, const char *const *argv, const char *bundle_dir, const char *documents_dir) {
    AppState &s = state();
    s.bundle = bundle_dir ? bundle_dir : "";
    s.docs = documents_dir ? documents_dir : "";
    std::vector<std::string> args(argv, argv + argc);
    ar::vos_log_open(s.docs.empty() ? nullptr : (s.docs + "/sm64ar.log").c_str());
    // GameController is read on the main thread only: poll it at 250 Hz into VosPads, which the game tick
// and the platform (controller loss, the placement's A) read. A 30 Hz tick then sees a pad at most 4 ms old.
    s.pads.reset(new ar::VosPads);
    s.pad_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(s.pad_timer, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_SEC / 250, NSEC_PER_MSEC);
    ar::VosPads *pads = s.pads.get();
    dispatch_source_set_event_handler(s.pad_timer, ^{ pads->poll(); });
    dispatch_resume(s.pad_timer);
    std::string joined;
    for (size_t i = 1; i < args.size(); i++) { joined += ' '; joined += args[i]; }
    ar::vos_log("AR_CONFIG args=%s", joined.c_str());
    ar::vos_log("AR_ADAPTERS sm64=%s abi=%u", ar_adapter_sm64()->name, ar_adapter_sm64()->abi_version);
    std::string err;
    s.options_ok = ar::parse_vos_options(args, games(), &s.opts, &err);
    if (!s.options_ok) {
        s.error = "bad arguments: " + err;
        ar::vos_log("AR_ERROR %s", s.error.c_str());
        set_status(s.error);
        return 0;
    }
    s.opts.rom = ar::bundled_data(s.bundle, "rom.z64");
    log_unknown_game_set_keys();
    {
        NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
        NSString *pending = [d stringForKey:kRenderPending], *failed = [d stringForKey:kRenderFailed];
        const std::string p = pending ? pending.UTF8String : "", f = (failed && !s.opts.reset_safe_mode) ? failed.UTF8String : "";
        if (s.opts.reset_safe_mode) ar::vos_log("AR_SAFE_MODE reset (was %s)", failed ? failed.UTF8String : "");
        s.render_failed = ar::safe_mode_failed_after_launch(p, f);
        if (!p.empty()) ar::vos_log("AR_SAFE_MODE probe_crash config=%s", p.c_str());
        if (!s.render_failed.empty()) ar::vos_log("AR_SAFE_MODE failed=%s", s.render_failed.c_str());
        [d setObject:@(s.render_failed.c_str()) forKey:kRenderFailed];
        [d removeObjectForKey:kRenderPending];
        [d synchronize];
        s.probe.set_sink(ar::RenderProbe::Sink{[](void *, const char *config) { probe_write(config); }, [](void *) { probe_clear(); }, nullptr});
    }
    apply_audio_to_game(s.audio.get());
    set_status("ready: game " + s.opts.game);
    return 1;
}
extern "C" int arvos_autostart(void) { return state().options_ok && state().opts.autostart ? 1 : 0; }
extern "C" int arvos_lifecycle_test(double *close_s, double *reopen_s) {
    const AppState &s = state();
    if (!s.options_ok || !(s.opts.lifecycle_close_s > 0)) return 0;
    *close_s = s.opts.lifecycle_close_s;
    *reopen_s = s.opts.lifecycle_reopen_s;
    return 1;
}
extern "C" int arvos_hide_status(void) {
    return state().options_ok && (!state().opts.hold_ticks.empty() || state().opts.hide_status) ? 1 : 0;
}
extern "C" int arvos_status(char *buf, int cap) {
    if (!buf || cap <= 0) return 0;
    AppState &s = state();
    std::lock_guard<std::mutex> g(s.m);
    const bool show_refusal = !s.refusal.empty() &&
        std::chrono::duration<double>(std::chrono::steady_clock::now() - s.refusal_at).count() < kRefusalShowS;
    std::snprintf(buf, (size_t)cap, "%s%s%s%s", show_refusal ? "level not changed: " : "", show_refusal ? s.refusal.c_str() : "",
                  show_refusal ? "\n" : "", s.status.c_str());
    return 1;
}
extern "C" void arvos_log(const char *line) { ar::vos_log("%s", line ? line : ""); }
extern "C" void arvos_space_closed(void) {
    AppState &s = state();
    if (ar::VosPlatform *p = s.live.load()) p->space_closed();
    ar::vos_log("AR_SPACE closed");
}
extern "C" int arvos_prepare_quit(double wait_s) {
    AppState &s = state();
    if (ar::VosPlatform *p = s.live.load()) p->space_closed();
    // The loop holds `run` while it runs; layer_detached (the recorder's save) runs before it lets go.
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(wait_s);
    bool ended = false;
    while (!(ended = s.run.try_lock()) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (ended) s.run.unlock();
    ar::vos_log("AR_APP quit loop_ended=%d", ended ? 1 : 0);
    if (!ended && s.options_ok && !s.opts.record.empty())
        ar::vos_log("AR_APP quit: the frame loop did not end in %.1f s; the recording %s may be incomplete", wait_s, s.opts.record.c_str());
    return ended ? 1 : 0;
}
// Exit without teardown: AppState is a function-local static, so exit() would destroy the session and the platform
// while the PHASE render thread, Metal completion handlers, a tap writer and the layer thread may still use them.
// Everything worth keeping is saved by now.
extern "C" void arvos_quit_now(void) {
    ar::vos_log("AR_APP quit exit (no teardown)");
    ar::vos_exit_now(0);
}
extern "C" int arvos_replace_test(double *after_s) {
    const AppState &s = state();
    if (!s.options_ok || !(s.opts.replace_after_s > 0)) return 0;
    *after_s = s.opts.replace_after_s;
    return 1;
}
extern "C" void arvos_replace_table(void) {
    ar::vos_log("AR_UI replace table");
    if (ar::VosPlatform *p = state().live.load()) p->replace();
}
static int copy_out(const std::string &v, char *buf, int cap);
extern "C" int arvos_game(char *buf, int cap) { return state().options_ok ? copy_out(state().opts.game, buf, cap) : 0; }
extern "C" int arvos_game_setting_option(const char *key, char *buf, int cap) {
    if (!key || !state().options_ok) return 0;
    const std::string *last = nullptr;
    for (const auto &kv : state().opts.game_set) if (kv.first == key) last = &kv.second;
    return last ? copy_out(*last, buf, cap) : 0;
}
static bool has_levels() { return state().options_ok && state().opts.game == "sm64"; }
static bool is_sm64_session() { return has_levels(); }
static int sm64_resampler_choice() {
    char v[16];
    return ar_adapter_sm64_get_setting("resampler", v, sizeof v) && std::strcmp(v, "linear") == 0 ? 1 : 0;
}
extern "C" int arvos_sm64_setting_count(void) { return has_levels() ? ar_adapter_sm64_setting_count() : 0; }
extern "C" int arvos_sm64_setting_key(int i, char *buf, int cap) {
    const char *k = has_levels() ? ar_adapter_sm64_setting_key(i) : nullptr;
    return k ? copy_out(k, buf, cap) : 0;
}
extern "C" int arvos_sm64_setting_default(const char *key, char *buf, int cap) {
    const char *d = has_levels() && key ? ar_adapter_sm64_setting_default(key) : nullptr;
    return d ? copy_out(d, buf, cap) : 0;
}
extern "C" int arvos_sm64_setting_set(const char *key, const char *value) {
    if (!has_levels() || !key || !value) return 0;
    char why[96];
    if (!ar_adapter_sm64_set_setting(key, value, why, sizeof why)) {
        ar::vos_log("AR_SETTING sm64.%s rejected value=%s (%s)", key, value, why);
        return 0;
    }
    ar::vos_log("AR_SETTING sm64.%s=%s", key, value);
    if (!std::strcmp(key, "title_eyes") && ar_adapter_sm64_wants_hands() && is_sm64_session())
        if (ar::VosPlatform *p = state().live.load()) p->start_hand_tracking();
    if (!std::strcmp(key, "resampler") && state().feed_filter.load() >= 0)
        state().feed_filter.store(std::strcmp(value, "linear") == 0 ? 1 : 0);
    return 1;
}
extern "C" int arvos_sm64_settings_revision(void) { return has_levels() ? ar_adapter_sm64_settings_revision() : 0; }
extern "C" int arvos_sm64_setting_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override) {
    return has_levels() ? ar_adapter_sm64_settings_drop_stored(key, stored, stored_rev, has_launch_override) : 0;
}
extern "C" int arvos_level_count(void) { return has_levels() ? ar_adapter_sm64_level_count() : 0; }
extern "C" int arvos_level_name(int index, char *buf, int cap) {
    const sm64_level_entry *e = has_levels() ? ar_adapter_sm64_level(index) : nullptr;
    if (!e || !buf || cap <= 0) return 0;
    std::snprintf(buf, (size_t)cap, "%s", e->name);
    return 1;
}
extern "C" int arvos_level_has_acts(int index) {
    const sm64_level_entry *e = has_levels() ? ar_adapter_sm64_level(index) : nullptr;
    return e && e->course > 0 ? 1 : 0;
}
extern "C" int arvos_level_section(int index) {
    const sm64_level_entry *e = has_levels() ? ar_adapter_sm64_level(index) : nullptr;
    return e ? e->section : -1;
}
extern "C" int arvos_play_from_start(void) {
    AppState &s = state();
    std::string why;
    if (!has_levels()) why = "no levels in this game";
    else if (!ar::level_switch_allowed(s.opts, &why)) {}
    else if (!ar_adapter_sm64_play_from_start()) why = ar_adapter_sm64_request_refusal() ? ar_adapter_sm64_request_refusal() : "refused";
    if (!why.empty()) {
        ar::vos_log("AR_STORY play_from_start refused: %s", why.c_str());
        std::lock_guard<std::mutex> g(s.m);
        s.refusal = why; s.refusal_at = std::chrono::steady_clock::now();
        return 0;
    }
    ar::vos_log("AR_STORY play_from_start");
    return 1;
}
extern "C" int arvos_select_level(int index, int act) {
    AppState &s = state();
    std::string why;
    const sm64_level_entry *e = has_levels() ? ar_adapter_sm64_level(index) : nullptr;
    if (!e) why = "no such level";
    else if (!ar::level_switch_allowed(s.opts, &why)) {}
    else if (act < 0 || act > 6) why = "bad act " + std::to_string(act) + " (0..6)";
    else if (!ar_adapter_sm64_request_level(index, act >= 1 ? act : -1))
        why = ar_adapter_sm64_request_refusal() ? ar_adapter_sm64_request_refusal() : "the game did not take the pick";
    if (!why.empty()) {
        ar::vos_log("AR_LEVEL refused index=%d act=%d: %s", index, act, why.c_str());
        std::lock_guard<std::mutex> g(s.m);
        s.refusal = why;
        s.refusal_at = std::chrono::steady_clock::now();
        return 0;
    }
    ar::vos_log("AR_LEVEL request key=%s act=%d", e->key, act);
    std::lock_guard<std::mutex> g(s.m);
    s.refusal.clear();
    return 1;
}
extern "C" int arvos_level_test(double *after_s, int *index, int *act) {
    const AppState &s = state();
    if (!has_levels() || !(s.opts.select_test_s > 0)) return 0;
    const int i = sm64_level_find(s.opts.select_test_key.c_str());
    if (i < 0) { ar::vos_log("AR_ERROR --select-test: unknown level %s", s.opts.select_test_key.c_str()); return 0; }
    *after_s = s.opts.select_test_s; *index = i; *act = s.opts.select_test_n;
    return 1;
}
extern "C" int arvos_level_view_get(arvos_level_view *out) {
    sm64_level_state ls;
    if (!out || !has_levels() || !ar_adapter_sm64_level_state(&ls)) return 0;
    out->current = ls.index; out->home = ls.home_index; out->pending = ls.pending; out->act = ls.act; out->story = ls.story;
    sm64_level_current_name(&ls, out->current_name, (int)sizeof out->current_name);
    out->screen = ls.screen;
    if (const char *title = ls.screen != 0 ? sm64_screen_title(ls.screen) : nullptr) {
        std::snprintf(out->current_name, sizeof out->current_name, "%s", title); out->current = -1;
    }
    return 1;
}
extern "C" int arvos_level_label(int index, char *buf, int cap) {
    const sm64_level_entry *e = has_levels() ? ar_adapter_sm64_level(index) : nullptr;
    if (!e || !buf || cap <= 0) return 0;
    sm64_level_state ls{};
    const bool have = ar_adapter_sm64_level_state(&ls) != 0;
    sm64_level_label(buf, cap, e->name, have ? ls.stars[index] : 0, e->star_max, have && ls.index == index,
                     have && ls.home_index == index);
    return 1;
}
extern "C" int arvos_has_draw_all_rooms(void) { return has_levels() ? 1 : 0; }
extern "C" int arvos_draw_all_rooms_option(void) {
    const AppState &s = state();
    if (!s.options_ok) return -1;
    return s.opts.draw_all_rooms >= 0 || !has_levels() ? s.opts.draw_all_rooms : sm64_setup_rooms(ar::join_game_args(s.opts.game_args).c_str());
}
extern "C" int arvos_draw_all_rooms(void) { return has_levels() ? ar_adapter_sm64_draw_all_rooms() : -1; }
extern "C" void arvos_set_draw_all_rooms(int on) {
    if (!has_levels()) return;
    std::string why;
    if (!state().game_lock.change([&] { ar_adapter_sm64_set_draw_all_rooms(on ? 1 : 0); }, &why)) {
        ar::vos_log("AR_SETTING draw_all_rooms locked (%s): kept %d", why.c_str(), ar_adapter_sm64_draw_all_rooms());
        return;
    }
    ar::vos_log("AR_SETTING draw_all_rooms=%d", on ? 1 : 0);
}
extern "C" int arvos_hide_outside_option(void) { return state().options_ok ? state().opts.hide_outside : -1; }
extern "C" void arvos_set_hide_outside(int on) {
    if (!has_levels()) return;
    ar_adapter_sm64_set_hide_outside(on ? 1 : 0);
    ar::vos_log("AR_SETTING hide_outside=%d", on ? 1 : 0);
}
extern "C" double arvos_mario_cm_option(void) {
    if (!has_levels()) return -1.0;
    const float cm = sm64_setup_mario_cm(ar::join_game_args(state().opts.game_args).c_str());
    return std::isnan(cm) ? -1.0 : (double)cm;
}
extern "C" int arvos_start_option(void) {
    return has_levels() ? sm64_setup_start(ar::join_game_args(state().opts.game_args).c_str()) : -1;
}
extern "C" int arvos_set_start_in(int story) {
    if (!has_levels()) return 0;
    AppState &s = state();
    const std::string why = s.game_lock.why();
    if (!why.empty()) { ar::vos_log("AR_SETTING start_in locked (%s)", why.c_str()); return 0; }
    s.start_in = story ? 1 : 0;
    ar::vos_log("AR_SETTING start_in=%s%s", story ? "story" : "level", s.session ? " (applies at the next launch)" : "");
    return 1;
}
extern "C" int arvos_start_effective(char *source, int cap) {
    const AppState &s = state();
    if (source && cap > 0) source[0] = 0;
    if (!has_levels() || !s.options_ok || !s.opts.sarec.empty()) return -1;
    const char *src = "saved";
    const int story = sm64_start_resolve(ar::join_game_args(s.opts.game_args).c_str(), s.start_in, &src);
    if (source && cap > 0) std::snprintf(source, (size_t)cap, "%s", src);
    return story;
}
extern "C" double arvos_mario_cm(void) { return has_levels() ? (double)ar_adapter_sm64_mario_cm() : -1.0; }
extern "C" int arvos_mario_cm_lock(char *buf, int cap) {
    const std::string why = state().game_lock.why();
    if (why.empty()) return 0;
    if (buf && cap > 0) std::snprintf(buf, (size_t)cap, "%s", why.c_str());
    return 1;
}
extern "C" void arvos_set_mario_cm(double cm, int final_value) {
    if (!has_levels()) return;
    std::string why;
    if (!state().game_lock.change([&] { ar_adapter_sm64_set_mario_cm((float)cm); }, &why)) {
        if (final_value) ar::vos_log("AR_SETTING mario_cm locked (%s): kept %.2f", why.c_str(), (double)ar_adapter_sm64_mario_cm());
        return;
    }
    if (final_value) {
        const float c = ar_adapter_sm64_mario_cm();
        ar::vos_log("AR_SETTING mario_cm=%.2f upm=%.0f", (double)c, (double)sm64_units_per_meter_for_mario_cm(c));
    }
}
extern "C" int arvos_scale_readout(double cm, char *buf, int cap) {
    if (!has_levels() || !buf || cap <= 0) return 0;
    sm64_level_state ls{};
    const float extent = ar_adapter_sm64_level_state(&ls) ? ls.extent_units : 0.f;
    sm64_scale_readout(buf, cap, (float)cm, extent);
    return 1;
}
extern "C" double arvos_mario_focus_far_m(double cm) {
    return has_levels() ? (double)(SM64_RELEVANCE_FAR / sm64_units_per_meter_for_mario_cm((float)cm)) : 0.0;
}
extern "C" int arvos_other_rooms_option(void) {
    const AppState &s = state();
    if (!s.options_ok) return -1;
    return s.opts.other_rooms >= 0 || !has_levels() ? s.opts.other_rooms : sm64_setup_other_rooms(ar::join_game_args(s.opts.game_args).c_str());
}
extern "C" int arvos_other_rooms(void) { return has_levels() ? ar_adapter_sm64_other_room_objects() : -1; }
extern "C" void arvos_set_other_rooms(int mode) {
    const char *name = ar_adapter_sm64_other_rooms_name(mode);
    if (!has_levels() || !name) return;
    std::string why;
    if (!state().game_lock.change([&] { ar_adapter_sm64_set_other_room_objects(mode); }, &why)) {
        ar::vos_log("AR_SETTING other_rooms locked (%s): kept %s", why.c_str(),
                    ar_adapter_sm64_other_rooms_name(ar_adapter_sm64_other_room_objects()));
        return;
    }
    ar::vos_log("AR_SETTING other_rooms=%s", name);
}
extern "C" int arvos_tree_style_option(void) { return state().options_ok ? state().opts.tree_style : -1; }
extern "C" void arvos_set_tree_style(int style) {
    const char *name = ar_adapter_sm64_tree_style_name(style);
    if (!has_levels() || !name) return;
    ar_adapter_sm64_set_tree_style(style);
    ar::vos_log("AR_SETTING tree_style=%s", name);
}
extern "C" int arvos_billboards_option(void) { return state().options_ok ? state().opts.billboards : -1; }
extern "C" void arvos_set_billboards(int style) {
    const char *name = ar_adapter_sm64_billboard_style_name(style);
    if (!has_levels() || !name) return;
    ar_adapter_sm64_set_billboard_style(style);
    ar::vos_log("AR_SETTING billboards=%s", name);
}
extern "C" int arvos_cannon_visible_option(void) {
    return has_levels() ? sm64_setup_cannon(ar::join_game_args(state().opts.game_args).c_str()) : -1;
}
extern "C" int arvos_cannon_visible(void) { return has_levels() ? ar_adapter_sm64_cannon_visible() : -1; }
extern "C" void arvos_set_cannon_visible(int on) {
    if (!has_levels()) return;
    std::string why;
    if (!state().game_lock.change([&] { ar_adapter_sm64_set_cannon_visible(on ? 1 : 0); }, &why)) {
        ar::vos_log("AR_SETTING cannon_visible locked (%s): kept %d", why.c_str(), ar_adapter_sm64_cannon_visible());
        return;
    }
    ar::vos_log("AR_SETTING cannon_visible=%d", on ? 1 : 0);
}
static int copy_out(const std::string &v, char *buf, int cap) {
    if (!buf || cap <= 0) return 0;
    std::snprintf(buf, (size_t)cap, "%s", v.c_str());
    return 1;
}
extern "C" int arvos_look_key_count(void) { return (int)ar::look_keys().size(); }
extern "C" int arvos_look_key(int i, char *buf, int cap) {
    const auto &k = ar::look_keys();
    return i >= 0 && i < (int)k.size() ? copy_out(k[(size_t)i], buf, cap) : 0;
}
extern "C" int arvos_look_default(const char *key, char *buf, int cap) {
    std::string v;
    return key && ar::look_default(key, &v) ? copy_out(v, buf, cap) : 0;
}
extern "C" int arvos_look_defaults_rev(void) { return ar::look_defaults_rev(); }
extern "C" int arvos_look_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override) {
    return key && stored && ar::look_drop_stored(key, stored, stored_rev, has_launch_override != 0) ? 1 : 0;
}
extern "C" int arvos_look_option(const char *key, char *buf, int cap) {
    if (!key || !state().options_ok) return 0;
    const std::string *last = nullptr;
    for (const auto &kv : state().opts.look) if (kv.first == key) last = &kv.second;
    return last ? copy_out(*last, buf, cap) : 0;
}
static struct PackChoice { int on = 0; std::string name; bool set = false; } g_packs[3];
static void log_pack_load_if_changed(void *) {
    static double logged = 0;   // layer thread only
    const double ms = ar_adapter_sm64_pack_load_ms();
    if (ms == logged) return;
    logged = ms;
    uint64_t up = 0, peak = 0;
    ar_adapter_sm64_texfifo_stats(&up, &peak);
    ar::vos_log("AR_PACKS load_ms=%.1f tex_up_mb=%.1f tex_peak_mb=%.1f", ms, up / 1048576.0, peak / 1048576.0);
}
static void apply_packs(int only_kind = 0) {
    if (!has_levels()) return;
    const bool roomlit = state().look.get().roomlit;
    for (int kind : {SM64_PACK_TEXTURES, SM64_PACK_MODELS}) {
        if (!g_packs[kind].set || (only_kind && kind != only_kind)) continue;
        const int on = g_packs[kind].on;
        const int idx = ar_adapter_sm64_set_packs(kind, on && roomlit, g_packs[kind].name.c_str());
        char used[128] = "", choice[160];
        if (on && roomlit && idx >= 0) ar_adapter_sm64_pack_info(idx, used, sizeof used, nullptr, nullptr, nullptr, nullptr);
        sm64_packs_choice_string(kind, on, on && roomlit && idx >= 0 ? used : g_packs[kind].name.c_str(), choice, sizeof choice);
        ar::vos_log("AR_SETTING %s=%s%s", sm64_packs_kind_name(kind), choice,
                    !on ? "" : !roomlit ? " (off: Look is Original N64)" : idx < 0 ? " (none installed)" : "");
    }
}
extern "C" int arvos_pack_count(void) {
    if (!has_levels()) return -1;
    const int n = ar_adapter_sm64_pack_count();
    static bool logged = false;
    if (n >= 0 && !logged) {
        logged = true;
        ar::vos_log("AR_PACKS n=%d", n);
        for (int i = 0; i < n; i++) {
            char name[128]; int kind = 0, tex = 0, bins = 0; double mb = 0;
            if (!ar_adapter_sm64_pack_info(i, name, sizeof name, &kind, &tex, &bins, &mb)) continue;
            ar::vos_log("AR_PACK i=%d kind=%s name=%s tex=%d bins=%d mb=%.1f", i,
                        sm64_packs_kind_name(kind) ? sm64_packs_kind_name(kind) : "none", name, tex, bins, mb);
            if (const char *why = ar_adapter_sm64_pack_refusal(i)) ar::vos_log("AR: refusing unstaged pack \"%s\" (%s)", name, why);
        }
        int refused = 0; double ms = 0;
        const int ok = ar_adapter_sm64_pack_stamps(&refused, &ms);
        if (ok >= 0) ar::vos_log("AR_PACKS_STAMPED ok=%d refused=%d ms=%.1f", ok, refused, ms);
    }
    return n;
}
extern "C" int arvos_pack_info(int index, char *name, int cap) {
    int kind = 0;
    if (!has_levels() || !name || cap <= 0 || !ar_adapter_sm64_pack_info(index, name, cap, &kind, nullptr, nullptr, nullptr)) return 0;
    return kind;
}
extern "C" int arvos_pack_option(int kind, char *buf, int cap) {
    if (!state().options_ok || !buf || cap <= 0) return 0;
    const std::string empty;
    const std::string &v = kind == SM64_PACK_TEXTURES ? state().opts.textures : kind == SM64_PACK_MODELS ? state().opts.models : empty;
    if (v.empty() || (int)v.size() >= cap) return 0;
    std::memcpy(buf, v.c_str(), v.size() + 1);
    return 1;
}
extern "C" void arvos_set_packs(int kind, int on, const char *name) {
    if (kind != SM64_PACK_TEXTURES && kind != SM64_PACK_MODELS) return;
    g_packs[kind].on = on ? 1 : 0; g_packs[kind].name = name ? name : ""; g_packs[kind].set = true;
    apply_packs(kind);
}
extern "C" int arvos_trees_3d(void) { return has_levels() ? ar_adapter_sm64_trees_3d() : 0; }

extern "C" int arvos_set_look(const char *key, const char *value, int final_value) {
    if (!key || !value) return 0;
    std::string err;
    if (!state().look.set(key, value, &err)) {
        ar::vos_log("AR_SETTING look.%s rejected value=%s (%s)", key, value, err.c_str());
        return 0;
    }
    if (final_value) ar::vos_log("AR_SETTING look.%s=%s", key, value);
    if (has_levels()) ar_adapter_sm64_set_normal_flags(ar::look_normal_flags(state().look.get()));
    if (std::strcmp(key, "mode") == 0) apply_packs();
    return 1;
}
extern "C" int arvos_foveation(void) { return effective_look().foveation ? 1 : 0; }
extern "C" void arvos_render_probe_begin(void) {
    const ar::LookSettings l = effective_look();
    state().probe.begin(ar::RenderRisk::layer_create, ar::render_config_key(ar::render_config_of(l)));
}
extern "C" void arvos_render_probe_end(void) { state().probe.end(ar::RenderRisk::layer_create); }
extern "C" int arvos_render_quality(double *quality) {
    const float q = effective_look().render_quality;
    if (!(q > 0.f)) return 0;
    if (quality) *quality = q;
    return 1;
}
static void apply_audio_to_game(const ar::AudioSettings &a) {
    if (!has_levels()) return;
    ar_adapter_sm64_set_player_focus(a.player_focus);
    ar_adapter_sm64_set_object_focus(a.object_focus);
    ar_adapter_sm64_set_music_ring(a.music_ring, a.ring_radius, a.ring_rotate);
    ar_adapter_sm64_set_ring_listener(a.ring_at_head, a.ring_focus);
}
// A game booted in SOURCES (1) whose PHASE voices then fell back to RemoteIO shows 0. The session is read only when the
// game says 1: it published that at its boot, after the session was made, so the session pointer is set and stays.
static int shown_game_audio_state(int game_state) {
    AppState &s = state();
    return game_state == 1 && s.session ? ar::shown_audio_state(game_state, s.session->audio_spatial()) : game_state;
}
extern "C" int arvos_sm64_audio_state(void) { return is_sm64_session() ? shown_game_audio_state(ar_adapter_sm64_audio_state()) : -1; }
extern "C" int arvos_audio_key_count(void) { return (int)ar::audio_keys().size(); }
extern "C" int arvos_audio_key(int i, char *buf, int cap) {
    const auto &k = ar::audio_keys();
    return i >= 0 && i < (int)k.size() ? copy_out(k[(size_t)i], buf, cap) : 0;
}
extern "C" int arvos_audio_default(const char *key, char *buf, int cap) {
    std::string v;
    return key && ar::audio_default(key, &v) ? copy_out(v, buf, cap) : 0;
}
extern "C" int arvos_audio_option(const char *key, char *buf, int cap) {
    if (!key || !state().options_ok) return 0;
    const std::string *last = nullptr;
    for (const auto &kv : state().opts.audio_set) if (kv.first == key) last = &kv.second;
    return last ? copy_out(*last, buf, cap) : 0;
}
extern "C" int arvos_audio_preset_count(void) { return (int)ar::audio_preset_names().size(); }
extern "C" int arvos_audio_preset_name(int i, char *buf, int cap) {
    const auto &n = ar::audio_preset_names();
    return i >= 0 && i < (int)n.size() ? copy_out(n[(size_t)i], buf, cap) : 0;
}
extern "C" int arvos_audio_preset_value(const char *preset, const char *key, char *buf, int cap) {
    std::vector<std::pair<std::string, std::string>> kv;
    if (!preset || !key || !ar::audio_preset_values(preset, &kv)) return 0;
    for (const auto &p : kv) if (p.first == key) return copy_out(p.second, buf, cap);
    return 0;
}
extern "C" int arvos_apply_audio_preset(const char *preset) {
    std::vector<std::pair<std::string, std::string>> kv;
    std::string err, line;
    if (!preset || !ar::audio_preset_values(preset, &kv) || !state().audio.set_many(kv, &err)) {
        ar::vos_log("AR_SETTING audio.preset rejected value=%s%s", preset ? preset : "(null)", err.empty() ? "" : (" (" + err + ")").c_str());
        return 0;
    }
    apply_audio_to_game(state().audio.get());
    for (const auto &p : kv) line += " " + p.first + "=" + p.second;
    ar::vos_log("AR_SETTING audio.preset=%s%s", preset, line.c_str());
    return 1;
}
extern "C" int arvos_set_audio(const char *key, const char *value, int final_value) {
    if (!key || !value) return 0;
    std::string err;
    if (!state().audio.set(key, value, &err)) {
        ar::vos_log("AR_SETTING audio.%s rejected value=%s (%s)", key, value, err.c_str());
        return 0;
    }
    apply_audio_to_game(state().audio.get());
    if (final_value) ar::vos_log("AR_SETTING audio.%s=%s", key, value);
    return 1;
}
extern "C" int arvos_room_light_get(arvos_room_light *out) {
    if (!out || !state().light_status.seen()) return 0;
    const ar::RoomLightStatus st = state().light_status.get();
    out->room = st.room ? 1 : 0; out->abs_lum = st.abs_lum; out->neutral = st.neutral; out->gain = st.gain;
    for (int k = 0; k < 3; k++) { out->dir[k] = st.dir[k]; out->tint[k] = st.tint[k]; }
    out->probes = st.probes;
    return 1;
}
extern "C" void arvos_push_spatial_event(const arvos_spatial_event *in) {
    if (!in) return;
    ar::SpatialEvent e;
    e.id = in->id;
    e.kind = in->kind >= 0 && in->kind <= 3 ? (ar::SpatialKind)in->kind : ar::SpatialKind::other;
    e.phase = in->phase == 1 ? ar::SpatialPhase::ended : in->phase == 2 ? ar::SpatialPhase::cancelled : ar::SpatialPhase::active;
    e.t_s = in->timestamp;
    e.has_ray = in->has_ray != 0; std::copy(in->ray_origin, in->ray_origin + 3, e.ray_o); std::copy(in->ray_direction, in->ray_direction + 3, e.ray_d);
    e.has_pose = in->has_pose != 0; std::copy(in->pose_position, in->pose_position + 3, e.pose_pos); std::copy(in->pose_orientation, in->pose_orientation + 4, e.pose_quat);
    e.chirality = (uint8_t)in->chirality;
    if (ar::VosPlatform *p = state().live.load()) p->push_spatial_event(e);
}
extern "C" void arvos_set_background(int background) {
    AppState &s = state();
    s.background = background != 0;
    if (ar::VosPlatform *p = s.live.load()) p->set_background(background != 0);
    ar::vos_log("AR_SCENE background=%d", background != 0);
}

extern "C" void arvos_layer_run(void *layer) {
    AppState &s = state();
    // A new layer means the old space is gone even when arvos_space_closed never ran (Digital Crown, another app's
    // space): end a loop still waiting on its paused layer before taking the one-loop lock.
    if (ar::VosPlatform *p = s.live.load()) p->space_closed();
    std::lock_guard<std::mutex> run(s.run);
    struct ProbeClear { ~ProbeClear() { state().probe.end_all(); } } probe_guard;
    if (!s.options_ok) { ar::vos_log("AR_ERROR not starting: %s", s.error.c_str()); return; }
    if (!s.session) {
        std::string aerr;
        const ar_adapter *a = pick_adapter(s.opts.game, s.bundle, &aerr);
        if (!a) { set_status("error: " + aerr); ar::vos_log("AR_ERROR %s", aerr.c_str()); return; }
        ar::VosPlatform::Config pc;
        pc.placer.place = s.opts.place;
        pc.placer.game = s.opts.game;
        pc.placer.record_path = s.docs + "/placement.json";
        pc.placer.reset = s.opts.reset_placement;
        pc.placer.planes = ar_plane_detection_provider_is_supported() || s.opts.sim_plane_seek;
        pc.sim_pinch = s.opts.sim_pinch;
        if (ar::fake_table_enabled(s.opts.fake_table, TARGET_OS_SIMULATOR != 0)) { pc.table_half_x = ar::kFakeTableHalfX; pc.table_half_z = ar::kFakeTableHalfZ; }
        // After a failed start the platform is kept: its ARKit handlers hold it, so it is never destroyed once attached.
        const bool fresh_platform = !s.platform;
        if (fresh_platform) s.platform.reset(new ar::VosPlatform(pc));
        else ar::vos_log("AR_PLATFORM kept for game %s (after a failed start)", s.opts.game.c_str());
        auto drop_platform = [&] { if (fresh_platform) s.platform.reset(); };
        s.platform->set_replaying(!s.opts.sarec.empty());
        if (!s.opts.sarec.empty()) {
            char why[200];
            const std::string path = ar::resolve_documents_path(s.docs, s.opts.sarec);
            if (!sarec_load_err(&s.rec, path.c_str(), why, sizeof why)) {
                const std::string perr = "cannot read " + path + ": " + why;
                set_status("error: " + perr);
                ar::vos_log("AR_ERROR %s", perr.c_str());
                drop_platform();
                return;
            }
            ar::vos_log("AR_INPUT source=sarec ticks=%u version=%u hashes=%d input_frame=%d setup=%s", s.rec.count, s.rec.version,
                        (s.rec.flags & SAREC_FLAG_HASHES) ? 1 : 0, (s.rec.flags & SAREC_FLAG_INPUT_FRAME) ? 1 : 0, s.rec.setup);
        } else {
            ar::vos_log("AR_INPUT source=gamecontroller");
        }
        std::string setup, serr;
        if (!ar::resolve_setup(ar::join_game_args(s.opts.game_args), s.opts.sarec.empty() ? "" : s.rec.setup, &setup, &serr, !s.opts.sarec.empty())) {
            set_status("error: " + serr); ar::vos_log("AR_ERROR %s", serr.c_str()); drop_platform(); return;
        }
        if (s.opts.game == "sm64") {
            // Locked first, so no live change lands between the values used here and the ones recorded.
            std::string lock;
            const bool locked = !ar::game_setting_change_allowed(s.opts, &lock);
            if (locked) s.game_lock.lock(lock);
            if (s.opts.sarec.empty()) {
                const char *src = "saved";
                const int story = sm64_start_resolve(ar::join_game_args(s.opts.game_args).c_str(), s.start_in, &src);
                if (story && sm64_setup_start(setup.c_str()) < 0) setup = ar::setup_with_items(setup, "start=story");
                ar::vos_log("AR_STORY start=%s source=%s", story ? "story" : "level", src);
            } else {
                ar::vos_log("AR_STORY start=%s source=recording", sm64_setup_start(setup.c_str()) == 1 ? "story" : "level");
            }
            if (!s.opts.sarec.empty()) ar_adapter_sm64_reset_game_settings();
            char why[160];
            if (!ar_adapter_sm64_setup(setup.c_str(), why, sizeof why)) {
                const std::string e = std::string("setup: ") + why;
                set_status("error: " + e); ar::vos_log("AR_ERROR %s", e.c_str()); drop_platform(); return;
            }
            if (!s.opts.record.empty()) {
                char items[96];
                const bool fit = sm64_game_setting_items(ar_adapter_sm64_mario_cm(), ar_adapter_sm64_cannon_visible(),
                                                         ar_adapter_sm64_draw_all_rooms(), ar_adapter_sm64_other_room_objects(),
                                                         items, sizeof items) >= 0;
                if (fit) setup = ar::setup_with_items(setup, items);
                if (!fit || setup.size() > SAREC_SETUP_MAX) {
                    const std::string e = ar::setup_too_long(fit ? items : "the game settings");
                    set_status("error: " + e); ar::vos_log("AR_ERROR %s", e.c_str()); drop_platform(); return;
                }
            }
            if (locked) {
                ar::vos_log("AR_SETTING draw_all_rooms=%d locked (%s)", ar_adapter_sm64_draw_all_rooms(), lock.c_str());
                ar::vos_log("AR_SETTING other_rooms=%s locked (%s)", ar_adapter_sm64_other_rooms_name(ar_adapter_sm64_other_room_objects()), lock.c_str());
                ar::vos_log("AR_SETTING cannon_visible=%d locked (%s)", ar_adapter_sm64_cannon_visible(), lock.c_str());
                const float c = ar_adapter_sm64_mario_cm();
                ar::vos_log("AR_SETTING mario_cm=%.2f upm=%.0f locked (%s)", (double)c, (double)sm64_units_per_meter_for_mario_cm(c), lock.c_str());
            }
        } else if (!setup.empty()) {
            set_status("error: game " + s.opts.game + " takes no --game-arg"); ar::vos_log("AR_ERROR game %s takes no --game-arg", s.opts.game.c_str());
            drop_platform(); return;
        }
        ar::vos_log("AR_SETUP %s", setup.c_str());
        s.platform->set_pads(s.pads.get());
        if (s.opts.game == "sm64") {
            std::string lock;
            ar_adapter_sm64_set_pointer_lock(!ar::game_setting_change_allowed(s.opts, &lock));
            s.platform->set_pointer_claim([](const ar::PointerEvent &e) {
                sm64_pointer p{};
                p.id = e.id; p.phase = (int)e.phase; p.has_hand = e.has_hand ? 1 : 0; p.head = e.head; p.no_ray = e.has_ray ? 0 : 1;
                for (int k = 0; k < 3; k++) { p.ray_o[k] = e.ray_o[k]; p.ray_d[k] = e.ray_d[k]; p.hand[k] = e.hand[k]; }
                const int claimed = ar_adapter_sm64_pointer(&p);
                if (e.phase == ar::PointerEvent::begin)
                    ar::vos_log("AR_POINTER begin id=%llu claimed=%d target=%d ndc=%.3f,%.3f", (unsigned long long)e.id, claimed,
                                p.target, (double)p.ndc[0], (double)p.ndc[1]);
                return claimed != 0;
            }, true);
            s.platform->set_hand_sink([](const ar::HandTips h[2]) {
                sm64_hand_tips t[2] = {};
                for (int i = 0; i < 2; i++) {
                    t[i].tracked = h[i].tracked ? 1 : 0;
                    for (int k = 0; k < 3; k++) { t[i].index_tip[k] = h[i].index_tip[k]; }
                }
                ar_adapter_sm64_set_hands(t);
            });
            s.platform->set_table_placed_sink([] { ar_adapter_sm64_table_placed(); });
            if (ar_adapter_sm64_wants_hands()) s.platform->start_hand_tracking();
            else ar::vos_log("AR_ARKIT hand_tracking not started (title_eyes=off)");
        } else {
            s.platform->set_pointer_claim({});
            s.platform->set_hand_sink({});
            s.platform->set_table_placed_sink({});
        }
        s.platform->set_background(s.background);
        s.opts.rom = ar::bundled_data(s.bundle, "rom.z64");
        ar::vos_log("AR_DATA rom=%s", s.opts.rom.c_str());
        s.session.reset(new ar::VosSession(a, *s.platform, s.opts, s.docs));
        const ar::SaveFolderChoice sf = ar::save_folder_for(s.opts.game, s.opts.game == "sm64" && sm64_setup_start(setup.c_str()) == 1,
                                                            !s.opts.sarec.empty(), !s.opts.record.empty());
        if (sf.fresh) {
            s.session->set_save_folder(sf.name, sf.fresh);
            ar::vos_log("AR_STORY scratch_save=Documents/%s", sf.name.c_str());
        }
        s.session->set_look_store(&s.look);
        s.session->set_render_safety(s.render_failed, &s.probe);
        s.session->set_audio_store(&s.audio);
        s.feed_filter.store(is_sm64_session() ? sm64_resampler_choice() : -1);
        s.session->set_feed_filter(&s.feed_filter);
        s.session->set_room_light_status(&s.light_status);
        if (s.opts.game == "sm64") {
            s.session->set_perf_window_callback(log_pack_load_if_changed, nullptr);
            s.session->set_key_light_callback([](void *, int on, const float d[3]) { ar_adapter_sm64_set_key_light(on, d); }, nullptr);
            ar_adapter_sm64_set_normal_flags(ar::look_normal_flags(s.look.get()));
        }
        if (!s.opts.sarec.empty()) s.session->set_replay(&s.rec);
        if (!s.opts.record.empty()) {
            sarec_init(&s.out, a->name, setup.c_str());
            s.session->set_recorder(&s.out, ar::resolve_documents_path(s.docs, s.opts.record));
        }
        s.live = s.platform.get();
    }
    s.platform->attach(layer);
    {
        const ar::LookSettings l = effective_look();
        ar::RenderProbeScope probe(l.render_quality > 0.f ? &s.probe : nullptr, ar::RenderRisk::render_quality,
                                   ar::render_config_key(ar::render_config_of(l)));
        s.platform->set_render_quality(l.render_quality);
    }
    std::string err;
    if (!s.session->ensure_started(s.platform->metal_device(), &err)) {
        if (ar::is_rom_error(err)) err = ar::rom_error_hint(err);
        set_status("error: " + err);
        s.platform->detach();
        return;
    }
    ar::vos_log("AR_LAYER attached ticks=%llu", (unsigned long long)s.session->ticks_done());
    s.session->layer_attached();
    uint64_t frames = 0;
    bool fov_turned_off = false;
    int last_level = -1, last_act = -1, last_pending = 1;
    int last_forced = 0, last_save = 0;
    int last_wait_s = 0;
    const auto opened = std::chrono::steady_clock::now();
    size_t look_tests = 0;
    std::vector<ar::VosOptions::LookTest> tests = s.opts.look_test;
    std::stable_sort(tests.begin(), tests.end(), [](const auto &a, const auto &b) { return a.at_s < b.at_s; });
    for (;;) {
        bool more;
        @autoreleasepool { more = s.session->frame(); }    // this thread has no run loop to drain Metal's autoreleases
        if (!more) break;
        const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count();
        for (; look_tests < tests.size() && since >= tests[look_tests].at_s; look_tests++) {
            const auto &t = tests[look_tests];
            std::string terr;
            const bool ok = s.look.set(t.key, t.value, &terr);
            ar::vos_log("AR_SETTING look.%s=%s reason=look-test ok=%d", t.key.c_str(), t.value.c_str(), ok ? 1 : 0);
        }
        if (++frames % 30 == 0) set_status(s.session->status());
        if (!fov_turned_off && s.session->foveation_refused() && s.look.get().foveation) {
            fov_turned_off = true;
            std::string ferr;
            if (s.look.set("foveation", "0", &ferr)) {
                [[NSUserDefaults standardUserDefaults] setObject:@"0" forKey:@"look.foveation"];
                ar::vos_log("AR_SETTING look.foveation=0 reason=rate_map_refused (applies at the next Open table)");
            }
        }
        sm64_level_state ls;
        if (s.opts.game == "sm64" && ar_adapter_sm64_level_state(&ls)) {
            if (!ls.pending && (last_pending || ls.level_num != last_level || ls.act != last_act)) {
                const sm64_level_entry *cur = sm64_level_at(ls.index), *home = sm64_level_at(ls.home_index);
                ar::vos_log("AR_LEVEL now level=%d key=%s act=%d home=%s", ls.level_num, cur ? cur->key : "-", ls.act,
                            home ? home->key : "-");
                last_level = ls.level_num; last_act = ls.act;
            }
            last_pending = ls.pending;
            const int wait_s = (ls.wait_ticks + 29) / 30;
            if (wait_s != last_wait_s && wait_s > 0)
                ar::vos_log("AR_LEVEL waiting reason=%s ticks=%d", ls.wait_reason, ls.wait_ticks);
            last_wait_s = wait_s;
            if (ls.save_count != last_save) {
                ar::vos_log("AR_SAVE course=%d star=%d", ls.save_course, ls.save_star);
                last_save = ls.save_count;
            }
            if (ls.forced_count != last_forced) {
                ar::vos_log("AR_LEVEL forced reason=%s", ls.forced_reason);
                last_forced = ls.forced_count;
            }
        }
    }
    s.session->layer_detached();
    s.platform->detach();
    ar::vos_log("AR_LAYER invalidated frames=%llu ticks=%llu", (unsigned long long)frames, (unsigned long long)s.session->ticks_done());
    set_status("table closed; " + s.session->status());
}

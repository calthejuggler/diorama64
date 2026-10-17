#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include "ar_game_args.h"
#include "ar_mem_stats.h"
#include "free_fly.h"
#include "wav_io.h"
#include "harness_platform.h"
#include "harness_session.h"
#include "png_io.h"
#include "depth_stats.h"
#include "look.h"
#include "ar_audio_settings.h"
#include "ar_audio_tuning.h"
#include "ar_layer.h"
#include "debug_points.h"
#include "room_light.h"
extern "C" {
#include "ar_adapter_sm64.h"
#include "ar_sarec.h"
#include "ar_scene_track.h"
#include "sm64_ar_setup.h"
#include "sm64_host.h"
#include "sm64_packs.h"
#include "sm64_story.h"
}
using namespace ar;

// Declared before the session that uses it, freed after it.
struct OwnedSarec {
    sarec r{};
    OwnedSarec() = default;
    OwnedSarec(const OwnedSarec &) = delete;
    OwnedSarec &operator=(const OwnedSarec &) = delete;
    ~OwnedSarec() { sarec_free(&r); }
};

struct Options {
    std::string rom, save, sarec, record, out, screenshot, camera = "near", test_close, audio_out, audio_mode = "stereo", spatial_out;
    std::string start;
    std::string level;
    std::string game = "sm64";
    std::vector<std::pair<std::string, std::string>> game_set;
    std::string preview_test;
    std::vector<std::string> game_args;
    int draw_all_rooms = -1;
    int other_rooms = -1;
    int tree_style = -1;
    int textures_on = 0, models_on = 0;
    std::string textures_pack, models_pack;
    std::string generate_tex_in, generate_tex_out, generate_actors;
    bool rom_given = false;
    bool probe_layer_clamp = false;
    bool pack_stats = false;
    int probe_moving_coin = -1;
    int probe_metal_cap = -1;
    bool mem_stats = false;
    std::string actor_counts;
    int billboards = -1;
    int hide_outside = -1;
    float mario_cm = 0.f;
    bool place_mario = false;
    float mario_at[3] = {0, 0, 0};
    // Harness defaults Original N64 and msaa 1 keep the goldens' and the ROM references' pixels.
    ar::LookSettings look = [] { ar::LookSettings s; s.roomlit = false; s.msaa = 1; return s; }();
    ar::AudioSettings audio = ar::audio_settings_off();
    bool offscreen = false, stereo = false, overlay = true;
    bool silhouette = true, log_hash = false, log_deep_hash = false, log_frame_hash = false;
    bool flat_hud = false, has_eye = false, has_target = false;
    bool depth_stats = false, depth_only = false;
    bool depth_holes_png = false;
    bool pip_sky = true;
    bool title_eyes = false;
    bool has_move_eye = false; uint32_t move_eye_frame = 0; float move_eye[3] = {0, 0, 0};
    bool pip_png = false;
    bool vis_stats = false;
    float eye[3] = {0, 0, 0}, target[3] = {0, 0, 0}, orbit_center[3] = {0.f, 0.75f, 0.f};
    float orbit_radius = 1.9f, orbit_height = 1.5f, orbit_period = 12.f;
    uint32_t frames = 1, fps = 30, w = 1280, h = 720, warmup = 240, screenshot_after = 0, quit_after = 0, test_close_after = 0;
    uint32_t png_every = 1;
    bool fake_probe = false;
    float probe_dir[3] = {0.f, 1.f, 0.f}, probe_rgb[3] = {1.f, 1.f, 1.f};
    std::vector<uint32_t> png_at;
    struct AtAction { uint32_t tick; std::string kind; int n; float x = 0, y = 0; };
    std::vector<AtAction> at;
    bool story_events = false;
};
static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static bool parse_u32(const char *v, uint32_t *out) {
    if (!v || !*v || std::isspace((unsigned char)*v) || *v == '-' || *v == '+') return false;
    char *e = nullptr; errno = 0;
    const unsigned long long x = std::strtoull(v, &e, 10);
    if (*e || errno || x > 0xFFFFFFFFull) return false;
    *out = (uint32_t)x; return true;
}
static bool parse_u32_list(const char *v, std::vector<uint32_t> *out) {
    std::vector<uint32_t> r;
    std::string s = v ? v : "";
    size_t at = 0;
    for (;;) {
        const size_t end = s.find(',', at);
        uint32_t x = 0;
        if (!parse_u32(s.substr(at, end == std::string::npos ? std::string::npos : end - at).c_str(), &x)) return false;
        if (!r.empty() && x <= r.back()) return false;
        r.push_back(x);
        if (end == std::string::npos) break;
        at = end + 1;
    }
    *out = r; return true;
}
static bool parse_float(const char *v, float *out) {
    if (!v || !*v || std::isspace((unsigned char)*v)) return false;
    char *e = nullptr;
    const float x = std::strtof(v, &e);
    if (*e || !std::isfinite(x)) return false;
    *out = x; return true;
}
static bool parse3(const char *v, float out[3]) {
    std::string s = v ? v : "";
    size_t at = 0;
    for (int i = 0; i < 3; i++) {
        const size_t end = i < 2 ? s.find(',', at) : s.size();
        if (end == std::string::npos) return false;
        if (!parse_float(s.substr(at, end - at).c_str(), &out[i])) return false;
        at = end + 1;
    }
    return true;
}
static bool parse_fake_probe(const char *v, float dir[3], float rgb[3]) {
    std::string s = v ? v : "";
    float f[5];
    size_t at = 0;
    for (int i = 0; i < 5; i++) {
        const size_t end = i < 4 ? s.find(',', at) : s.size();
        if (end == std::string::npos || !parse_float(s.substr(at, end - at).c_str(), &f[i])) return false;
        at = end + 1;
    }
    if (f[0] < 0.f || f[0] > 360.f || f[1] < -90.f || f[1] > 90.f) return false;
    for (int k = 2; k < 5; k++) if (f[k] < 0.f || f[k] > 100.f) return false;
    const double az = f[0] * 3.14159265358979323846 / 180.0, el = f[1] * 3.14159265358979323846 / 180.0;
    dir[0] = (float)(std::cos(el) * std::sin(az)); dir[1] = (float)std::sin(el); dir[2] = (float)(std::cos(el) * std::cos(az));
    for (int k = 0; k < 3; k++) rgb[k] = f[2 + k];
    return true;
}
static bool parse(int argc, char **argv, Options &o, std::string &err) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) { err = "missing value for " + a; return nullptr; } return argv[++i]; };
        const char *v = nullptr;
        if (a == "--offscreen") o.offscreen = true;
        else if (a == "--stereo") o.stereo = true;
        else if (a == "--no-overlay") o.overlay = false;
        else if (a == "--no-silhouette") o.silhouette = false;
        else if (a == "--flat-hud") o.flat_hud = true;
        else if (a == "--depth-stats") o.depth_stats = true;
        else if (a == "--vis-stats") { o.vis_stats = true; }
        else if (a == "--depth-only") o.depth_only = true;
        else if (a == "--depth-holes-png") o.depth_holes_png = true;
        else if (a == "--pip-png") o.pip_png = true;
        else if (a == "--title-eyes") { o.title_eyes = true; }
        else if (a == "--pip-sky") {
           
            if (!(v = val())) return false;
            if (std::strcmp(v, "on") != 0 && std::strcmp(v, "off") != 0) { err = std::string("bad --pip-sky '") + v + "' (on or off)"; return false; }
            o.pip_sky = std::strcmp(v, "on") == 0;
        }
        else if (a == "--log-hash") o.log_hash = true;
        else if (a == "--log-deep-hash") { o.log_deep_hash = true; }
        else if (a == "--log-frame-hash") { o.log_frame_hash = true; }
        else if (a == "--eye") { if (!(v = val())) return false; if (!parse3(v, o.eye)) { err = "bad --eye (use X,Y,Z)"; return false; } o.has_eye = true; }
        else if (a == "--move-eye") {
            if (!(v = val())) return false;
            const std::string str = v; const size_t colon = str.find(':');
            if (colon == std::string::npos || !parse_u32(str.substr(0, colon).c_str(), &o.move_eye_frame) ||
                !parse3(str.substr(colon + 1).c_str(), o.move_eye)) { err = "bad --move-eye (use N:X,Y,Z)"; return false; }
            o.has_move_eye = true;
        }
        else if (a == "--target") { if (!(v = val())) return false; if (!parse3(v, o.target)) { err = "bad --target (use X,Y,Z)"; return false; } o.has_target = true; }
        else if (a == "--orbit-center") { if (!(v = val())) return false; if (!parse3(v, o.orbit_center)) { err = "bad --orbit-center (use X,Y,Z)"; return false; } }
        else if (a == "--orbit-radius") { if (!(v = val())) return false; if (!parse_float(v, &o.orbit_radius)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--orbit-height") { if (!(v = val())) return false; if (!parse_float(v, &o.orbit_height)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--orbit-period") { if (!(v = val())) return false; if (!parse_float(v, &o.orbit_period)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--level") {
           
            if (!(v = val())) return false;
            o.level = v;   // one setup value: no comma (it would add setup keys), never empty
            if (o.level.empty() || o.level.find(',') != std::string::npos) { err = std::string("bad --level '") + v + "' (one level key)"; return false; }
        }
        else if (a == "--start") {
            if (!(v = val())) return false;
            if (std::strcmp(v, "story") != 0 && std::strcmp(v, "level") != 0) { err = std::string("bad --start '") + v + "' (story or level)"; return false; }
            o.start = v;
        }
        else if (a == "--story-events") { o.story_events = true; }
        else if (a == "--at") {
            if (!(v = val())) return false;
            const std::string str = v;
            const size_t colon = str.find(':'), eq = str.find('=');
            Options::AtAction act{};
            bool ok = colon != std::string::npos && eq != std::string::npos && eq > colon + 1 &&
                      parse_u32(str.substr(0, colon).c_str(), &act.tick);
            if (ok) {
                act.kind = str.substr(colon + 1, eq - colon - 1);
                const std::string arg = str.substr(eq + 1);
                uint32_t n = 0;
                if (act.kind == "tap") {
                    const size_t comma = arg.find(',');
                    ok = comma != std::string::npos && parse_float(arg.substr(0, comma).c_str(), &act.x) &&
                         parse_float(arg.substr(comma + 1).c_str(), &act.y) && act.x >= -1.f && act.x <= 1.f &&
                         act.y >= -1.f && act.y <= 1.f;
                } else {
                    ok = (act.kind == "door" || act.kind == "painting" || act.kind == "gameover" || act.kind == "credits" ||
                          act.kind == "event_music" || act.kind == "exit" || act.kind == "star") &&
                         parse_u32(arg.c_str(), &n) &&
                         n <= (act.kind == "door" || act.kind == "event_music" ? 255u : act.kind == "painting" ? 44u :
                               act.kind == "star" ? 1u : 0u);
                }
                act.n = (int)n;
            }
            if (!ok) { err = "bad --at '" + str + "' (TICK:door=N, TICK:painting=N, TICK:gameover=0, TICK:credits=0, TICK:event_music=N, TICK:exit=0, TICK:star=0|1 or TICK:tap=X,Y)"; return false; }
            o.at.push_back(act);
        }
        else if (a == "--draw-all-rooms") {
           
            if (!(v = val())) return false;
            const std::string d = v;
            if (d != "on" && d != "off") { err = std::string("bad --draw-all-rooms '") + v + "' (on or off)"; return false; }
            o.draw_all_rooms = d == "on" ? 1 : 0;
        }
        else if (a == "--other-rooms") {
           
            if (!(v = val())) return false;
            o.other_rooms = ar_adapter_sm64_other_rooms_parse(v);
            if (o.other_rooms < 0) { err = std::string("bad --other-rooms '") + v + "' (hidden, frozen or live)"; return false; }
        }
        else if (a == "--hide-outside") {
           
            if (!(v = val())) return false;
            const std::string d = v;
            if (d != "on" && d != "off") { err = std::string("bad --hide-outside '") + v + "' (on or off)"; return false; }
            o.hide_outside = d == "on" ? 1 : 0;
        }
        else if (a == "--mario-cm") {
           
            if (!(v = val())) return false;
            if (!parse_float(v, &o.mario_cm) || !(o.mario_cm >= SM64_MARIO_CM_MIN && o.mario_cm <= SM64_MARIO_CM_MAX)) {
                err = std::string("bad --mario-cm '") + v + "' (0.5..10)"; return false;
            }
        }
        else if (a == "--place-mario") {
           
            if (!(v = val())) return false;
            if (!parse3(v, o.mario_at)) { err = "bad --place-mario (use X,Y,Z)"; return false; }
            o.place_mario = true;
        }
        else if (a == "--billboards") {
           
            if (!(v = val())) return false;
            o.billboards = ar_adapter_sm64_billboard_style_parse(v);
            if (o.billboards < 0) { err = std::string("bad --billboards '") + v + "' (upright or full)"; return false; }
        }
        else if (a == "--tree-style") {
           
            if (!(v = val())) return false;
            o.tree_style = ar_adapter_sm64_tree_style_parse(v);
            if (o.tree_style < 0) { err = std::string("bad --tree-style '") + v + "' (card or crossed)"; return false; }
        }
        else if (a == "--textures" || a == "--models") {
           
            if (!(v = val())) return false;
            const int kind = a == "--textures" ? SM64_PACK_TEXTURES : SM64_PACK_MODELS;
            char name[128];
            const int on = sm64_packs_parse_choice(kind, v, name, sizeof name);
            if (on < 0) { err = "bad " + a + " '" + v + "' (" + (kind == SM64_PACK_TEXTURES ? "original, hd or hd:NAME" : "original, 3d or 3d:NAME") + ")"; return false; }
            (kind == SM64_PACK_TEXTURES ? o.textures_on : o.models_on) = on;
            (kind == SM64_PACK_TEXTURES ? o.textures_pack : o.models_pack) = name;
        }
        else if (a == "--generate-tex") {
           
            if (!(v = val())) return false; o.generate_tex_in = v;
            if (!(v = val())) return false; o.generate_tex_out = v;
        }
        else if (a == "--generate-actors") { if (!(v = val())) return false; o.generate_actors = v; }
        else if (a == "--pack-stats") { o.pack_stats = true; }
        else if (a == "--mem-stats") { o.mem_stats = true; }
        else if (a == "--actor-counts") { if (!(v = val())) return false; o.actor_counts = v; }
        else if (a == "--probe-layer-clamp") { o.probe_layer_clamp = true; }
        else if (a == "--probe-moving-coin") {
            if (!(v = val())) return false;
            uint32_t t = 0;
            if (!parse_u32(v, &t)) { err = "bad " + a + " value '" + v + "'"; return false; }
            o.probe_moving_coin = (int)t;
        }
        else if (a == "--probe-metal-cap") {
            if (!(v = val())) return false;
            uint32_t t = 0;
            if (!parse_u32(v, &t)) { err = "bad " + a + " value '" + v + "'"; return false; }
            o.probe_metal_cap = (int)t;
        }
        else if (a == "--look") {
            if (!(v = val())) return false;
            const std::string s = v;
            const size_t eq = s.find('=');
            std::string why;
            if (eq == std::string::npos || eq == 0 || !ar::look_set(&o.look, s.substr(0, eq), s.substr(eq + 1), &why)) {
                err = "bad --look '" + s + "'" + (why.empty() ? std::string(" (use KEY=VALUE)") : ": " + why);
                return false;
            }
        }
        else if (a == "--audio-set") {
            if (!(v = val())) return false;
            const std::string s = v;
            const size_t eq = s.find('=');
            std::string why;
            std::vector<std::pair<std::string, std::string>> kv;
            if (eq == std::string::npos || eq == 0 || !ar::audio_expand_option(s.substr(0, eq), s.substr(eq + 1), &kv, &why)) {
                err = "bad --audio-set '" + s + "'" + (why.empty() ? std::string(" (use KEY=VALUE)") : ": " + why);
                return false;
            }
            for (const auto &p : kv) ar::audio_set(&o.audio, p.first, p.second, &why);
        }
        else if (a == "--fake-probe") {
            if (!(v = val())) return false;
            if (!parse_fake_probe(v, o.probe_dir, o.probe_rgb)) { err = std::string("bad --fake-probe '") + v + "' (AZ,EL,R,G,B)"; return false; }
            o.fake_probe = true;
        }
        else if (a == "--preview-test") {
            if (!(v = val())) return false;
            o.preview_test = v;
            if (o.preview_test != "seeking" && o.preview_test != "dragging") { err = std::string("bad --preview-test '") + v + "' (seeking or dragging)"; return false; }
        }
        else if (a == "--rom") { if (!(v = val())) return false; o.rom = v; o.rom_given = true; }
        else if (a == "--save") { if (!(v = val())) return false; o.save = v; }
        else if (a == "--sarec") { if (!(v = val())) return false; o.sarec = v; }
        else if (a == "--record") { if (!(v = val())) return false; o.record = v; }
        else if (a == "--game-arg") {
            if (!(v = val())) return false;
            if (!valid_game_arg(v)) { err = std::string("bad --game-arg '") + v + "' (use KEY=VALUE)"; return false; }
            o.game_args.push_back(v);
        }
        else if (a == "--game") { if (!(v = val())) return false; o.game = v; }
        else if (a == "--game-set") {
            if (!(v = val())) return false;
            const std::string kv = v;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= kv.size()) { err = "bad --game-set '" + kv + "' (use KEY=VALUE)"; return false; }
            o.game_set.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        else if (a == "--audio-out") { if (!(v = val())) return false; o.audio_out = v; }
        else if (a == "--audio-mode") {
            if (!(v = val())) return false;
            o.audio_mode = v;
            if (o.audio_mode != "stereo" && o.audio_mode != "spatial") { err = std::string("bad --audio-mode '") + v + "' (stereo or spatial)"; return false; }
        }
        else if (a == "--msaa") {
            if (!(v = val())) return false;
            std::string why;
            if (!ar::look_set(&o.look, "msaa", v, &why)) { err = std::string("bad --msaa '") + v + "' (1, 2 or 4)"; return false; }
        }
        else if (a == "--tex-filter") {
            if (!(v = val())) return false;
            std::string why;
            if (!ar::look_set(&o.look, "tex_filter", v, &why)) { err = std::string("bad --tex-filter '") + v + "' (smooth or vanilla)"; return false; }
        }
        else if (a == "--spatial-out") { if (!(v = val())) return false; o.spatial_out = v; }
        else if (a == "--out") { if (!(v = val())) return false; o.out = v; }
        else if (a == "--camera") { if (!(v = val())) return false; o.camera = v; }
        else if (a == "--frames") { if (!(v = val())) return false; if (!parse_u32(v, &o.frames)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--fps") { if (!(v = val())) return false; if (!parse_u32(v, &o.fps)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--warmup-ticks") { if (!(v = val())) return false; if (!parse_u32(v, &o.warmup)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--png-every") { if (!(v = val())) return false; if (!parse_u32(v, &o.png_every)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--png-at") { if (!(v = val())) return false; if (!parse_u32_list(v, &o.png_at)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--quit-after") { if (!(v = val())) return false; if (!parse_u32(v, &o.quit_after)) { err = "bad " + a + " value '" + v + "'"; return false; } }
        else if (a == "--size") {
            if (!(v = val())) return false;
            const std::string sz = v;
            const size_t x = sz.find('x');
            if (x == std::string::npos || !parse_u32(sz.substr(0, x).c_str(), &o.w) || !parse_u32(sz.substr(x + 1).c_str(), &o.h) || !o.w || !o.h) { err = "bad --size (use WxH)"; return false; }
        } else if (a == "--screenshot-after") {
            if (!(v = val())) return false;
            if (!parse_u32(v, &o.screenshot_after)) { err = "bad " + a + " value '" + v + "'"; return false; }
            if (!(v = val())) return false;
            o.screenshot = v;
        } else if (a == "--test-close-after") {
            if (!(v = val())) return false;
            if (!parse_u32(v, &o.test_close_after)) { err = "bad " + a + " value '" + v + "'"; return false; }
            if (!(v = val())) return false;
            o.test_close = v;
            if (o.test_close != "window" && o.test_close != "cmdq") { err = "--test-close-after N needs window or cmdq"; return false; }
        } else { err = "unknown option " + a; return false; }
    }
    if (o.game != "sm64") { err = "unknown --game " + o.game + " (sm64)"; return false; }
    for (const auto &kv : o.game_set) {
        char why[96];
        if (!ar_adapter_sm64_set_setting(kv.first.c_str(), kv.second.c_str(), why, sizeof why)) { err = std::string("bad --game-set: ") + why; return false; }
    }
    if (o.rom.empty()) { const char *e = std::getenv("SM64_ROM"); o.rom = e ? e : "local/baserom.us.z64"; }
    if (o.save.empty()) o.save = "local/harness-save";
    if (!o.level.empty()) {
        for (const std::string &g : o.game_args)
            if (g.rfind("level=", 0) == 0) { err = "--level and --game-arg level=... go not together"; return false; }
        o.game_args.push_back("level=" + o.level);
    }
    if (o.start == "story" && !o.level.empty()) { err = "--start story and --level go not together"; return false; }
    if (!o.start.empty() && !o.sarec.empty()) { err = "--start is taken from the recording (--sarec)"; return false; }
    if (!o.start.empty()) o.game_args.push_back("start=" + o.start);
    if ((!o.generate_tex_in.empty() || !o.generate_actors.empty()) && !o.rom_given) { err = std::string(o.generate_tex_in.empty() ? "--generate-actors" : "--generate-tex") + " needs --rom"; return false; }
    if (o.offscreen && o.out.empty()) { err = "--offscreen needs --out DIR"; return false; }
    if (!o.at.empty() && !o.record.empty()) { err = "--at replays a recording: not with --record"; return false; }
    if (!o.at.empty() && !o.offscreen) { err = "--at needs --offscreen"; return false; }
    if (o.story_events && !o.offscreen) { err = "--story-events needs --offscreen"; return false; }
    if (!o.fps) { err = "--fps must be > 0"; return false; }
    if (!o.spatial_out.empty() && o.audio_mode != "spatial") { err = "--spatial-out needs --audio-mode spatial"; return false; }
    if (!o.spatial_out.empty() && !o.offscreen) { err = "--spatial-out needs --offscreen"; return false; }
    if (o.audio.show_sources && o.audio_mode != "spatial") { err = "--audio-set show_sources=1 needs --audio-mode spatial"; return false; }
    if (o.audio.show_sources && !o.offscreen) { err = "--audio-set show_sources=1 needs --offscreen"; return false; }
    if (o.depth_only && !o.depth_stats) { err = "--depth-only needs --depth-stats"; return false; }
    if (o.depth_holes_png && !o.depth_stats) { err = "--depth-holes-png needs --depth-stats"; return false; }
    if (o.pip_png && !o.offscreen) { err = "--pip-png needs --offscreen"; return false; }
    if (o.place_mario && !o.offscreen) { err = "--place-mario needs --offscreen"; return false; }
    if (o.place_mario && !o.warmup) { err = "--place-mario needs --warmup-ticks > 0"; return false; }
    if (o.has_eye != o.has_target) { err = "--eye and --target go together"; return false; }
    if (o.has_move_eye && !o.has_eye) { err = "--move-eye needs --eye"; return false; }
    if (!(o.orbit_period > 0.f)) { err = "--orbit-period must be > 0"; return false; }
    CameraPreset p;
    if (o.camera != "orbit" && o.camera != "game" && !parse_preset(o.camera.c_str(), &p)) { err = "unknown --camera " + o.camera; return false; }
    if (o.camera == "game" && !o.offscreen) { err = "--camera game needs --offscreen"; return false; }
    if (o.camera == "game" && o.has_eye) { err = "--camera game and --eye cannot be used together"; return false; }
    return true;
}

static const ar_adapter *game_adapter(const Options &o) {
    (void)o;
    return ar_adapter_sm64();
}
static bool load_replay(const Options &o, sarec *rec) {
    if (o.sarec.empty()) return true;
    char why[200];
    if (!sarec_load_err(rec, o.sarec.c_str(), why, sizeof why)) {
        std::fprintf(stderr, "error: cannot read %s: %s\n", o.sarec.c_str(), why);
        return false;
    }
    return true;
}
// Call after load_replay and before init: the replay index is the runtime's tick count.
static bool apply_setup(const Options &o, const sarec &rec, std::string *setup) {
    std::string serr;
    if (!resolve_setup(join_game_args(o.game_args), o.sarec.empty() ? "" : rec.setup, setup, &serr, !o.sarec.empty())) {
        std::fprintf(stderr, "error: %s\n", serr.c_str());
        return false;
    }
    if (!o.sarec.empty() && rec.game[0] && o.game != rec.game) {
        std::fprintf(stderr, "error: the recording is for %s, not %s\n", rec.game, o.game.c_str());
        return false;
    }
    // A replay takes the recording's game-changing settings (its setup items, absent = the defaults) whatever the
    // options say; otherwise the options, then --game-arg on top. A recording gets an item for each one not at its
    // default.
    const bool replay = !o.sarec.empty();
    ar_adapter_sm64_reset_game_settings();
    ar_adapter_sm64_set_pip_sky(o.pip_sky ? 1 : 0);
    if (!replay) {
        if (o.mario_cm > 0.f) ar_adapter_sm64_set_mario_cm(o.mario_cm);
        if (o.draw_all_rooms >= 0) ar_adapter_sm64_set_draw_all_rooms(o.draw_all_rooms);
        if (o.other_rooms >= 0) ar_adapter_sm64_set_other_room_objects(o.other_rooms);
    }
    ar_adapter_sm64_set_title_eyes_active(o.title_eyes && !replay ? 1 : 0);
    if (o.title_eyes) {
        ar_adapter_sm64_set_eyes_log([](const char *line, uint32_t tick) { std::printf("AR_EYES %s tick=%u\n", line, tick); });
        std::printf("AR_SETTING title_eyes_active=%d%s\n", replay ? 0 : 1, replay ? " (--title-eyes ignored in a replay)" : "");
    }
    char why[160];
    if (!ar_adapter_sm64_setup(setup->c_str(), why, sizeof why)) { std::fprintf(stderr, "error: setup: %s\n", why); return false; }
    ar_adapter_sm64_set_screen_log([](int screen, int story, uint32_t tick) {
        const char *name = sm64_screen_name(screen);
        std::printf("AR_SCREEN name=%s story=%d tick=%u\n", name ? name : "?", story, tick);
    });
    ar_adapter_sm64_set_dropped_log([](const char *what, const char *why, uint32_t tick) {
        std::printf("AR_LEVEL dropped %s: %s tick=%u\n", what, why, tick);
    });
    char items[96];
    if (!o.record.empty()) {
        if (sm64_game_setting_items(ar_adapter_sm64_mario_cm(), ar_adapter_sm64_cannon_visible(), ar_adapter_sm64_draw_all_rooms(),
                                    ar_adapter_sm64_other_room_objects(), items, sizeof items) < 0) {
            std::fprintf(stderr, "error: the game settings' setup items do not fit\n"); return false;
        }
        *setup = ar::setup_with_items(*setup, items);
        if (setup->size() > SAREC_SETUP_MAX) { std::fprintf(stderr, "error: %s\n", ar::setup_too_long(items).c_str()); return false; }
    }
    std::printf("AR_SETUP %s\n", setup->c_str());
    const bool story = sm64_setup_start(setup->c_str()) == 1;
    std::printf("AR_STORY start=%s source=%s\n", story ? "story" : "level", replay ? "recording" : "launch");
    if (story && (replay || !o.record.empty())) {
        std::error_code ec;
        const bool empty = !std::filesystem::exists(o.save, ec) || std::filesystem::is_empty(o.save, ec);
        if (!empty) { std::fprintf(stderr, "error: a story recording or replay needs an empty --save folder (%s)\n", o.save.c_str()); return false; }
    }
    const char *from = replay ? " from=recording" : "";
    std::printf("AR_SETTING draw_all_rooms=%d%s", ar_adapter_sm64_draw_all_rooms(), from);
    if (replay && o.draw_all_rooms >= 0) std::printf(" (--draw-all-rooms %s ignored)", o.draw_all_rooms ? "on" : "off");
    std::printf("\n");
    std::printf("AR_SETTING other_rooms=%s%s", ar_adapter_sm64_other_rooms_name(ar_adapter_sm64_other_room_objects()), from);
    if (replay && o.other_rooms >= 0) std::printf(" (--other-rooms %s ignored)", ar_adapter_sm64_other_rooms_name(o.other_rooms));
    std::printf("\n");
    if (o.tree_style >= 0) ar_adapter_sm64_set_tree_style(o.tree_style);
    std::printf("AR_SETTING tree_style=%s\n", ar_adapter_sm64_tree_style_name(ar_adapter_sm64_tree_style()));
    std::printf("AR_SETTING cannon_visible=%d%s\n", ar_adapter_sm64_cannon_visible(), from);
    if (o.billboards >= 0) ar_adapter_sm64_set_billboard_style(o.billboards);
    std::printf("AR_SETTING billboards=%s\n", ar_adapter_sm64_billboard_style_name(ar_adapter_sm64_billboard_style()));
    if (o.hide_outside >= 0) ar_adapter_sm64_set_hide_outside(o.hide_outside);
    std::printf("AR_SETTING hide_outside=%d\n", ar_adapter_sm64_hide_outside());
    const float cm = ar_adapter_sm64_mario_cm();
    std::printf("AR_SETTING mario_cm=%g upm=%g", cm, sm64_units_per_meter_for_mario_cm(cm));
    if (replay) std::printf(" from=recording");
    if (replay && o.mario_cm > 0.f) std::printf(" (--mario-cm %g ignored)", o.mario_cm);
    std::printf("\n");
    ar_adapter_sm64_set_player_focus(o.audio.player_focus);
    ar_adapter_sm64_set_object_focus(o.audio.object_focus);
    ar_adapter_sm64_set_music_ring(o.audio.music_ring, o.audio.ring_radius, o.audio.ring_rotate);
    ar_adapter_sm64_set_ring_listener(o.audio.ring_at_head, o.audio.ring_focus);
    for (const std::string &k : ar::audio_keys()) std::printf("AR_SETTING audio.%s=%s\n", k.c_str(), ar::audio_get(o.audio, k).c_str());
    return true;
}
static bool apply_game_set(const Options &o, HarnessSession &s) {
    (void)o; (void)s;
    for (int i = 0; i < ar_adapter_sm64_setting_count(); i++) {
        const char *k = ar_adapter_sm64_setting_key(i);
        char v[32] = "";
        if (k && ar_adapter_sm64_get_setting(k, v, sizeof v)) std::printf("AR_SETTING sm64.%s=%s\n", k, v);
    }
    return true;
}
static void apply_fake_probe(const Options &o, HarnessPlatform &plat, HarnessSession &s) {
    if (!o.fake_probe) return;
    plat.set_fake_probe(s.mtl_device(), o.probe_dir, o.probe_rgb);
    std::printf("AR_SETTING fake_probe dir=%.3f,%.3f,%.3f rgb=%g,%g,%g\n", o.probe_dir[0], o.probe_dir[1], o.probe_dir[2],
                o.probe_rgb[0], o.probe_rgb[1], o.probe_rgb[2]);
}
static void apply_look(const Options &o, HarnessSession &s) {
    s.set_key_light_callback([](void *, int on, const float d[3]) { ar_adapter_sm64_set_key_light(on, d); }, nullptr);
    s.set_look(o.look);
    ar_adapter_sm64_set_normal_flags(ar::look_normal_flags(o.look));
    for (const std::string &k : ar::look_keys()) std::printf("AR_SETTING look.%s=%s\n", k.c_str(), ar::look_get(o.look, k).c_str());
}
static void attach_replay_and_recorder(const Options &o, ar_runtime *rt, const sarec *rec, sarec *out, const std::string &setup) {
    if (!o.sarec.empty()) {
        ar_runtime_set_replay(rt, rec);
        std::printf("AR_INPUT source=sarec ticks=%u version=%u hashes=%d input_frame=%d setup=%s\n", rec->count, rec->version,
                    (rec->flags & SAREC_FLAG_HASHES) ? 1 : 0, (rec->flags & SAREC_FLAG_INPUT_FRAME) ? 1 : 0, rec->setup);
    }
    if (!o.record.empty()) { sarec_init(out, game_adapter(o)->name, setup.c_str()); ar_runtime_set_recorder(rt, out); }
}
static bool report_replay(const Options &o, ar_runtime *rt, uint32_t rec_count) {
    if (o.sarec.empty()) return false;
    const ar_replay_status st = ar_runtime_replay_status(rt);
    std::printf("AR_REPLAY played=%llu checked=%llu diverged=%d", (unsigned long long)st.played, (unsigned long long)st.checked, st.diverged);
    if (!st.done) std::printf(" ended_early=1 done=%llu/%u", (unsigned long long)st.played, rec_count);
    if (st.diverged) std::printf(" tick=%llu expected=0x%016llx got=0x%016llx", (unsigned long long)st.first_bad_tick,
                                 (unsigned long long)st.expected, (unsigned long long)st.got);
    std::printf("\n");
    std::fflush(stdout);
    return st.diverged != 0;
}
static bool save_record(const Options &o, const sarec *out) {
    if (o.record.empty()) return true;
    if (!sarec_save(out, o.record.c_str())) { std::fprintf(stderr, "error: cannot write %s\n", o.record.c_str()); return false; }
    std::printf("AR_RECORD saved=1 path=%s ticks=%u dropped=%u\n", o.record.c_str(), out->count, out->dropped);
    if (out->dropped) { std::fprintf(stderr, "error: the recording dropped %u tick(s): allocation failed\n", out->dropped); return false; }
    return true;
}

// DynOS's own generators (0007-DynOS-packs.patch) after a boot into an empty temporary save. Nothing is ticked or
// drawn.
static int run_generate(const Options &o) {
    char tmpl[] = "/tmp/sm64ar-gen-XXXXXX";
    if (!mkdtemp(tmpl)) { std::fprintf(stderr, "error: cannot create a temporary save\n"); return 1; }
    if (!ar_adapter_sm64()->init(o.rom.c_str(), tmpl)) {
        std::fprintf(stderr, "error: %s\n", ar_adapter_sm64()->last_error());
        std::filesystem::remove_all(tmpl);
        return 1;
    }
    if (!o.generate_tex_in.empty()) sm64_host_generate_tex(o.generate_tex_in.c_str(), o.generate_tex_out.c_str());
    if (!o.generate_actors.empty()) sm64_host_generate_actors(o.generate_actors.c_str());
    std::printf("AR_GENERATED tex=%s actors=%s\n", o.generate_tex_out.c_str(), o.generate_actors.c_str());
    std::filesystem::remove_all(tmpl);
    return 0;
}
static void print_mem_stats(const Options &o) {
    if (!o.mem_stats || o.game != "sm64") return;
    uint64_t up = 0, peak = 0;
    ar_adapter_sm64_texfifo_stats(&up, &peak);
    const ar_adapter *ad = ar_adapter_sm64();
    const fc_frame *f = ad->acquire_snapshot();
    const unsigned verts = f ? f->vert_count : 0;
    if (f) ad->release_snapshot(f);
    std::printf("AR_MEM phys_mb=%.1f tex_up_mb=%.1f tex_peak_mb=%.1f pack_load_ms=%.1f frame_verts=%u\n", ar_phys_footprint_mb(),
                up / 1048576.0, peak / 1048576.0, ar_adapter_sm64_pack_load_ms(), verts);
    std::fflush(stdout);
}
static void print_actor_counts(const Options &o) {
    if (o.actor_counts.empty() || o.game != "sm64") return;
    std::string line = "AR_ACTORS", s = o.actor_counts;
    for (size_t b = 0; b <= s.size();) {
        size_t e = s.find(',', b);
        if (e == std::string::npos) e = s.size();
        const std::string name = s.substr(b, e - b);
        if (!name.empty()) line += " " + name + "=" + std::to_string(ar_adapter_sm64_actor_instances(name.c_str()));
        b = e + 1;
    }
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}
static void apply_packs(const Options &o) {
    const int n = ar_adapter_sm64_pack_count();
    std::printf("AR_PACKS n=%d\n", n);
    for (int i = 0; i < n; i++) {
        char name[128]; int kind = 0, tex = 0, bins = 0; double mb = 0;
        if (!ar_adapter_sm64_pack_info(i, name, sizeof name, &kind, &tex, &bins, &mb)) continue;
        std::printf("AR_PACK i=%d kind=%s name=%s tex=%d bins=%d mb=%.1f\n", i, sm64_packs_kind_name(kind) ? sm64_packs_kind_name(kind) : "none",
                    name, tex, bins, mb);
    }
    const bool roomlit = o.look.roomlit;
    for (int kind : {SM64_PACK_TEXTURES, SM64_PACK_MODELS}) {
        const int on = kind == SM64_PACK_TEXTURES ? o.textures_on : o.models_on;
        const std::string &want = kind == SM64_PACK_TEXTURES ? o.textures_pack : o.models_pack;
        const int idx = ar_adapter_sm64_set_packs(kind, on && roomlit, want.c_str());
        char used[128] = "", choice[160];
        if (on && roomlit && idx >= 0) ar_adapter_sm64_pack_info(idx, used, sizeof used, nullptr, nullptr, nullptr, nullptr);
        sm64_packs_choice_string(kind, on, on && roomlit ? used : want.c_str(), choice, sizeof choice);
        std::printf("AR_SETTING %s=%s%s\n", sm64_packs_kind_name(kind), choice,
                    !on ? "" : !roomlit ? " (off: Look is Original N64)" : idx < 0 ? " (none installed)" : "");
    }
}

static int run_offscreen(const Options &o) {
    HarnessPlatform plat;
    plat.set_size(o.w, o.h);
    plat.set_stereo(o.stereo);
    plat.set_preview_test(o.preview_test);
    std::string err;
    OwnedSarec rec, out;   // must outlive the session: freed after it
    if (!load_replay(o, &rec.r)) return 1;
    if (!o.at.empty() && (rec.r.flags & SAREC_FLAG_HASHES)) {
        std::fprintf(stderr, "error: --at changes a hashed replay (%s): its state hashes would diverge\n", o.sarec.c_str());
        return 1;
    }
    std::string setup;
    if (!apply_setup(o, rec.r, &setup)) return 1;
    HarnessSession s(game_adapter(o), plat);
    if (!apply_game_set(o, s)) return 2;
    if (o.probe_moving_coin >= 0 && o.game == "sm64") ar_adapter_sm64_test_queue_moving_coin(o.probe_moving_coin);
    if (o.probe_metal_cap >= 0 && o.game == "sm64") ar_adapter_sm64_test_queue_metal_cap(o.probe_metal_cap);
    s.set_audio_mode(o.audio_mode == "spatial" ? AR_AUDIO_MODE_SOURCES : AR_AUDIO_MODE_STEREO);
    s.set_overlay_panel(o.overlay);
    s.set_silhouette(o.silhouette);
    s.set_flat_hud(o.flat_hud);
    apply_look(o, s);
    if (!s.init(o.rom, o.save, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    apply_packs(o);
    apply_fake_probe(o, plat, s);
    attach_replay_and_recorder(o, s.runtime(), &rec.r, &out.r, setup);
    if (o.log_deep_hash && o.game == "sm64") {
        for (uint32_t i = 0; i < o.warmup; i++) {
            s.warmup(1);
            std::printf("AR_DEEP n=%u hash=0x%016llx\n", i + 1, (unsigned long long)ar_adapter_sm64_deep_state_hash());
        }
    } else {
        s.warmup(o.warmup);
    }
    if (o.pack_stats && o.game == "sm64") std::printf("AR_PACKSTAT force3d=%d trees_3d=%d tree_cards=%d load_ms=%.1f packdraws=%u tilts=%u\n",
                                  ar_adapter_sm64_objects_force_3d(), ar_adapter_sm64_trees_3d(), ar_adapter_sm64_tree_cards(),
                                  ar_adapter_sm64_pack_load_ms(), ar_adapter_sm64_pack_draws(), ar_adapter_sm64_pack_tilts());
    if (o.pack_stats && o.game == "sm64" && o.probe_moving_coin >= 0) {
        float m[16], n[3];
        int yaw = 0;
        if (ar_adapter_sm64_pack_tilt_sample(m, n, &yaw)) {
            std::printf("AR_PACKTILT floor=%.6f,%.6f,%.6f yaw=%d m=", n[0], n[1], n[2], yaw);
            for (int i = 0; i < 16; i++) std::printf(i ? ",%.6f" : "%.6f", m[i]);
            std::printf("\n");
        } else {
            std::printf("AR_PACKTILT none\n");
        }
    }
    print_actor_counts(o);
    print_mem_stats(o);
    if (o.place_mario) {
        if (!ar_adapter_sm64_test_place_mario(o.mario_at[0], o.mario_at[1], o.mario_at[2])) {
            std::fprintf(stderr, "error: --place-mario: no floor under %g,%g,%g\n", o.mario_at[0], o.mario_at[1], o.mario_at[2]);
            return 1;
        }
        std::printf("AR_PLACE_MARIO at=%g,%g,%g\n", o.mario_at[0], o.mario_at[1], o.mario_at[2]);
    }
    if (o.log_hash)
        std::printf("AR_TICK n=%llu hash=0x%016llx\n", (unsigned long long)s.ticks_done(), (unsigned long long)game_adapter(o)->state_hash());
    if (o.vis_stats) {
        const ar_level_info lv = ar_adapter_sm64()->level_info();
        sm64_level_state ls{};
        ar_adapter_sm64_level_state(&ls);
        std::printf("AR_LEVELINFO level=%d upm=%.1f ground_y=%.1f min=%.0f,%.0f,%.0f max=%.0f,%.0f,%.0f area=%d\n", ls.level_num,
                    lv.units_per_meter, lv.ground_y, lv.bounds_min[0], lv.bounds_min[1], lv.bounds_min[2], lv.bounds_max[0],
                    lv.bounds_max[1], lv.bounds_max[2], ls.area);
    }
    const bool scene = !o.spatial_out.empty();
    std::vector<int16_t> audio, lanes, chunk(2 * 4096), lane_chunk((size_t)AR_AUDIO_MAX_SOURCES * 4096);
    std::vector<ar_audio_tick_meta> metas(16);
    std::string track;
    uint64_t origin = 0, track_ticks = 0, dropped_sources = 0;
    uint32_t max_active = 0;
    uint64_t next_frame = 0, next_meta_frame = 0, frame_gaps = 0, meta_gaps = 0;
    bool have_origin = false, track_ok = true;
    ar_audio_tick_meta last_meta{};
    bool have_meta = false;
    auto pull_all = [&](bool keep) {
        for (;;) {
            ar_audio_pull p{chunk.data(), scene ? lane_chunk.data() : nullptr, 4096, 0, metas.data(), (uint32_t)metas.size(), 0};
            const uint32_t n = s.pull_audio_scene(&p);
            if (p.meta_count) { last_meta = metas[p.meta_count - 1]; have_meta = true; }
            if (keep) audio.insert(audio.end(), chunk.begin(), chunk.begin() + 2 * n);
            if (keep && scene) {
                lanes.insert(lanes.end(), lane_chunk.begin(), lane_chunk.begin() + (size_t)AR_AUDIO_MAX_SOURCES * n);
                if (!have_origin && n) { origin = p.first_frame; have_origin = true; next_frame = next_meta_frame = p.first_frame; }
                if (n && p.first_frame != next_frame) frame_gaps++;
                if (n) next_frame = p.first_frame + n;
                for (uint32_t i = 0; i < p.meta_count; i++) {
                    if (metas[i].first_frame != next_meta_frame) meta_gaps++;
                    next_meta_frame = metas[i].first_frame + metas[i].frames;
                    char line[4096];
                    if (!ar_scene_track_line(&metas[i], track_ticks++, origin, line, sizeof line)) track_ok = false;
                    track += line;
                    track += '\n';
                    dropped_sources += metas[i].dropped;
                    max_active = std::max(max_active, metas[i].active);
                }
            }
            if (n == 0) return;
        }
    };
    pull_all(false);
    const ar_audio_tuning tuning = ar::audio_tuning_from_settings(o.audio);
    auto source_points = [&](size_t count[3]) {
        std::vector<ar::DebugPoint> pts;
        count[0] = count[1] = count[2] = 0;
        if (!have_meta) return pts;
        const ar_level_info lv = game_adapter(o)->level_info();
        const float size = ar::layer_size_factor(lv.units_per_meter, lv.reference_units_per_meter);
        for (const ar_audio_source &lane : last_meta.lane) {
            float pos[3];
            const float level = ar_audio_lane_gain(&tuning, &last_meta.listener, &lane, pos);
            ar::DebugPoint d;
            if (!ar::sound_source_point(lane.flags, pos, level, &d, size)) continue;
            pts.push_back(d);
            count[(lane.flags & AR_SRC_MUSIC) ? 0 : (lane.flags & AR_SRC_FOCUS) ? 1 : 2]++;
        }
        return pts;
    };
    std::filesystem::create_directories(o.out);
    CameraPreset preset = CameraPreset::Near;
    bool orbit = o.camera == "orbit";
    const bool game_cam = o.camera == "game";
    if (!orbit && !game_cam) parse_preset(o.camera.c_str(), &preset);
    uint32_t pngs = 0;
    int last_level = -1, last_intro = 0;
    uint64_t last_pip = 0;
    for (uint32_t f = 0; f < o.frames; f++) {
        double t = (double)f / o.fps;
        plat.set_time_ns(offscreen_frame_time_ns(f, o.fps));   // exactly one tick per frame at 30 fps
        ar_pose head = orbit ? orbit_pose(t, o.orbit_radius, o.orbit_height, o.orbit_period, o.orbit_center)
                             : o.has_eye ? ar_pose_look_at(o.has_move_eye && f >= o.move_eye_frame ? o.move_eye : o.eye, o.target)
                             : preset_pose(preset);
        float fov_up = 0.5f, near_m = 0.f;
        if (game_cam) {
            ar_pose eye_table; float tan_half = 0.f, n = 0.f;
            if (ar_adapter_sm64_game_camera(&eye_table, &tan_half, &n)) {
                ar_table tb{};
                plat.api()->get_table(plat.api()->user, &tb);
                head = ar_pose_mul(tb.tracking_from_table, eye_table);
                fov_up = tan_half; near_m = n;
            }
        }
        plat.set_fov_up(fov_up);
        plat.set_near(near_m);
        plat.set_head(head);
        const bool select = o.png_at.empty() ? (o.png_every != 0 && f % o.png_every == 0)
                                             : std::binary_search(o.png_at.begin(), o.png_at.end(), f);
        if (o.audio.show_sources) {
            size_t n[3];
            s.set_debug_points(source_points(n));
            if (select) std::printf("AR_SOURCES frame=%u music=%zu focus=%zu sfx=%zu\n", f, n[0], n[1], n[2]);
        }
        if (!s.frame(true, nullptr, nullptr)) return 1;
        s.wait_idle();
        if (o.log_frame_hash && o.game == "sm64") {
            const unsigned long long n = (unsigned long long)s.ticks_done();
            std::printf("AR_TICK n=%llu hash=0x%016llx\n", n, (unsigned long long)game_adapter(o)->state_hash());
            std::printf("AR_DEEP n=%llu hash=0x%016llx\n", n, (unsigned long long)ar_adapter_sm64_deep_state_hash());
        }
        if (o.game == "sm64") {   // game thread: the offscreen frame ran its tick on this thread
            for (const Options::AtAction &act : o.at) {
                if (act.tick != s.ticks_done()) continue;
                int ok = 1;
                if (act.kind == "door") sm64_host_test_warp_node(act.n);
                else if (act.kind == "painting") ok = sm64_host_test_enter_painting(act.n);
                else if (act.kind == "gameover") { sm64_host_set_lives(0); sm64_host_test_kill_mario(); }
                else if (act.kind == "credits") ok = sm64_host_test_grand_star_ending();
                else if (act.kind == "tap") ok = ar_adapter_sm64_menu_tap(act.x, act.y);
                else if (act.kind == "event_music") ok = sm64_host_test_play_secondary_music(act.n);
                else if (act.kind == "exit") sm64_host_test_exit_course();
                else if (act.kind == "star") ok = act.n ? sm64_host_test_spawn_star_noexit(0) : sm64_host_test_spawn_star(0);
                if (act.kind == "tap") std::printf("AR_AT tick=%u tap=%.3f,%.3f ok=%d\n", act.tick, (double)act.x, (double)act.y, ok);
                else std::printf("AR_AT tick=%u %s=%d ok=%d\n", act.tick, act.kind.c_str(), act.n, ok);
            }
            if (o.story_events || !o.at.empty()) {
                const int lv = sm64_host_level(), intro = sm64_host_mario_in_intro();
                if (lv != last_level) { std::printf("AR_LEVEL now=%d tick=%llu\n", lv, (unsigned long long)s.ticks_done()); last_level = lv; }
                if (last_intro && !intro) std::printf("AR_INTRO end tick=%llu\n", (unsigned long long)s.ticks_done());
                static int last_menu = -2;
                const int menu = sm64_host_menu_mode();
                if (menu != last_menu) { std::printf("AR_MENU mode=%d tick=%llu\n", menu, (unsigned long long)s.ticks_done()); last_menu = menu; }
                static int last_dialog = -2;
                const int dlg = sm64_host_dialog_id();
                if (dlg != last_dialog) { std::printf("AR_DIALOG id=%d tick=%llu\n", dlg, (unsigned long long)s.ticks_done()); last_dialog = dlg; }
                static int last_shot = -2;
                const int shot = ar_adapter_sm64_intro_shot();
                if (shot != last_shot) { std::printf("AR_SHOT intro=%d tick=%llu\n", shot, (unsigned long long)s.ticks_done()); last_shot = shot; }
                static float last_pip_alpha = 0.f;
                const float pip = ar_adapter_sm64_pip_alpha();
                if ((pip > 0.f) != (last_pip_alpha > 0.f) || (pip >= 1.f) != (last_pip_alpha >= 1.f))
                    std::printf("AR_PIP alpha=%.3f tick=%llu\n", (double)pip, (unsigned long long)s.ticks_done());
                last_pip_alpha = pip;
                last_intro = intro;
            }
        }
        if (!o.audio_out.empty() || scene) pull_all(true);
        else if (o.audio.show_sources) pull_all(false);
        if (select && o.depth_stats) {
            std::vector<uint8_t> px; std::vector<float> dp; uint32_t w = 0, h = 0, dw = 0, dh = 0;
            if (!s.read_ar(px, w, h) || !s.read_ar_depth(dp, dw, dh) || w != dw || h != dh) {
                std::fprintf(stderr, "error: cannot read the AR layer's colour and depth at frame %u\n", f);
                return 1;
            }
            const DepthStats st = depth_holes(px.data(), dp.data(), (size_t)w * h);
            std::printf("AR_DEPTH frame=%u covered=%llu holes=%llu fraction=%.5f\n", f, (unsigned long long)st.covered,
                        (unsigned long long)st.holes, st.fraction());
            if (o.depth_holes_png) {
                for (size_t i = 0; i < (size_t)w * h; i++) {
                    if (px[4 * i + 3] < 26 || dp[i] != 0.f) continue;   // what depth_holes counts (alpha_min 26)
                    px[4 * i] = 255; px[4 * i + 1] = 0; px[4 * i + 2] = 255; px[4 * i + 3] = 255;
                }
                char name[64]; std::snprintf(name, sizeof name, "/holes_%04u.png", f);
                if (!png_write(o.out + name, px.data(), w, h)) { std::fprintf(stderr, "cannot write %s%s\n", o.out.c_str(), name); return 1; }
            }
        }
        if (select && o.vis_stats) {
            ar_table tb{};   // the metric takes the head in table space; --eye and the presets are tracking space
            plat.api()->get_table(plat.api()->user, &tb);
            const ar_pose eye = ar_pose_mul(ar_pose_inverse(tb.tracking_from_table), head);
            int mario = 0; float floors = 0.f;
            const int n = ar_adapter_sm64_visibility(eye.pos, &mario, &floors);
            std::printf("AR_VIS frame=%u eye=%.3f,%.3f,%.3f mario=%d floors=%.3f samples=%d\n", f, eye.pos[0], eye.pos[1],
                        eye.pos[2], mario, floors, n);
            fc_look lk{};
            if (ar_adapter_sm64_look(&lk))
                std::printf("AR_LOOK frame=%u flags=0x%x floor=%.1f box=%.1f,%.1f,%.1f,%.1f liquid=%d water=%u cut=%.0f,%.0f,%.0f,%.0f"
                            " cover=%.3f kind=%u\n",
                            f, lk.flags, (double)lk.clip_floor_y, (double)lk.clip_lo[0], (double)lk.clip_lo[1], (double)lk.clip_hi[0],
                            (double)lk.clip_hi[1], (lk.flags & FC_LOOK_LIQUID) ? 1 : 0, lk.water_count, (double)lk.cut_target[0],
                            (double)lk.cut_target[1], (double)lk.cut_target[2], (double)lk.cut_radius,
                            (double)lk.trans_cover, (unsigned)lk.trans_kind);
        }
        if (select && !o.depth_only) {
            std::vector<uint8_t> px; uint32_t w = 0, h = 0;
            if (!s.read_final(px, w, h)) return 1;
            char name[64]; std::snprintf(name, sizeof name, "/frame_%04u.png", f);
            if (!png_write(o.out + name, px.data(), w, h)) { std::fprintf(stderr, "cannot write %s%s\n", o.out.c_str(), name); return 1; }
            pngs++;
        }
        const uint64_t pip_now = s.game_view_renders();
        if (select && o.pip_png && pip_now != last_pip) {
            std::vector<uint8_t> px; uint32_t w = 0, h = 0;
            if (!s.read_game_view(px, w, h)) { std::fprintf(stderr, "error: cannot read the game view at frame %u\n", f); return 1; }
            char name[64]; std::snprintf(name, sizeof name, "/pip_%04u.png", f);
            if (!png_write(o.out + name, px.data(), w, h)) { std::fprintf(stderr, "cannot write %s%s\n", o.out.c_str(), name); return 1; }
            std::printf("AR_PIP_PNG frame=%u size=%ux%u\n", f, w, h);
        }
        last_pip = pip_now;
    }
    std::printf("AR_LIGHT %s\n", ar::room_light_line(s.room_light_status()).c_str());
    uint64_t ticks = s.ticks_done() - o.warmup, want = offscreen_ticks_expected(o.frames, o.fps);
    if (ticks != want) {
        std::fprintf(stderr, "error: %u frame(s) at %u fps ran %llu game tick(s), expected %llu\n", o.frames, o.fps,
                     (unsigned long long)ticks, (unsigned long long)want);
        return 1;
    }
    const bool diverged = report_replay(o, s.runtime(), rec.r.count);
    if (!save_record(o, &out.r)) return 1;   // saved even when the replay diverged: a --sarec + --record run keeps its recording
    if (scene) {
        std::error_code ec;
        std::filesystem::create_directories(o.spatial_out, ec);
        if (ec) { std::fprintf(stderr, "error: cannot create %s: %s\n", o.spatial_out.c_str(), ec.message().c_str()); return 1; }
        const bool wavs = wav_write_s16_stereo(o.spatial_out + "/bed.wav", audio.data(), audio.size() / 2, AR_AUDIO_RATE_HZ) &&
                          wav_write_s16(o.spatial_out + "/lanes.wav", lanes.data(), lanes.size() / AR_AUDIO_MAX_SOURCES,
                                        AR_AUDIO_MAX_SOURCES, AR_AUDIO_RATE_HZ);
        std::ofstream t(o.spatial_out + "/track.jsonl", std::ios::binary);
        t << track;
        t.close();
        std::ofstream aj(o.spatial_out + "/audio.json", std::ios::binary);
        aj << ar::audio_settings_json(o.audio);
        aj.close();
        if (!wavs || !t || !aj || !track_ok) { std::fprintf(stderr, "error: cannot write the spatial dump to %s\n", o.spatial_out.c_str()); return 1; }
        std::printf("AR_SPATIAL ticks=%llu frames=%zu lanes=%u max_active=%u dropped=%llu out=%s\n", (unsigned long long)track_ticks,
                    audio.size() / 2, AR_AUDIO_MAX_SOURCES, max_active, (unsigned long long)dropped_sources, o.spatial_out.c_str());
        std::printf("AR_SPATIAL_RING frame_gaps=%llu meta_gaps=%llu\n", (unsigned long long)frame_gaps, (unsigned long long)meta_gaps);
        if (frame_gaps || meta_gaps) {
            std::fprintf(stderr, "error: the scene ring lost audio (frame_gaps=%llu meta_gaps=%llu); the spatial dump is incomplete\n",
                         (unsigned long long)frame_gaps, (unsigned long long)meta_gaps);
            return 1;
        }
    }
    if (diverged) return 3;
    if (!o.audio_out.empty()) {
        if (!wav_write_s16_stereo(o.audio_out, audio.data(), audio.size() / 2, AR_AUDIO_RATE_HZ)) {
            std::fprintf(stderr, "error: cannot write %s\n", o.audio_out.c_str());
            return 1;
        }
        std::printf("wrote %zu audio frame(s) to %s\n", audio.size() / 2, o.audio_out.c_str());
    }
    if (o.probe_layer_clamp && o.game == "sm64") std::printf("AR_PROBE layer_clamp=%d\n", ar_adapter_sm64_probe_layer_clamp());
    if (o.pack_stats && o.game == "sm64") std::printf("AR_PACKSTAT clamped=%u\n", ar_adapter_sm64_gfx_clamped());
    print_mem_stats(o);
    std::printf("rendered %u frame(s), wrote %u PNG(s) to %s\n%llu game tick(s) after warm-up\n", o.frames, pngs, o.out.c_str(),
                (unsigned long long)ticks);
    return 0;
}

@interface HarnessView : MTKView <MTKViewDelegate>
- (instancetype)initWithFrame:(NSRect)r platform:(HarnessPlatform *)p session:(HarnessSession *)s options:(const Options &)o;
@end
@implementation HarnessView {
    HarnessPlatform *_plat;
    HarnessSession *_session;
    FreeFly _fly;
    Options _opt;
    bool _keys[256];
    bool _orbit, _overlay;
    bool _silhouette;
    int64_t _t0, _last;
    uint64_t _frame;
    bool _stopping;
}
- (instancetype)initWithFrame:(NSRect)r platform:(HarnessPlatform *)p session:(HarnessSession *)s options:(const Options &)o {
    self = [super initWithFrame:r device:(__bridge id<MTLDevice>)s->mtl_device()];
    if (!self) return nil;
    _plat = p; _session = s; _opt = o; _orbit = false; _overlay = o.overlay; _silhouette = o.silhouette; _frame = 0; _stopping = false;
    std::memset(_keys, 0, sizeof _keys);
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.framebufferOnly = NO;
    self.preferredFramesPerSecond = 60;
    self.delegate = self;
    _t0 = _last = now_ns();
    return self;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)e {
    if (e.keyCode < 256) _keys[e.keyCode] = true;
    NSString *c = e.charactersIgnoringModifiers.lowercaseString;
    if ([c isEqualToString:@"o"]) _orbit = !_orbit;
    if ([c isEqualToString:@"v"]) _plat->set_stereo(!_plat->stereo());
    if ([c isEqualToString:@"h"]) { _overlay = !_overlay; _session->set_overlay_panel(_overlay); }
    if ([c isEqualToString:@"x"]) { _silhouette = !_silhouette; _session->set_silhouette(_silhouette); }
    if ([c isEqualToString:@"p"]) [self saveScreenshot:"local/screenshots/screenshot.png"];
}
- (void)keyUp:(NSEvent *)e { if (e.keyCode < 256) _keys[e.keyCode] = false; }
- (void)mouseDragged:(NSEvent *)e { _fly.look(-(float)e.deltaX * 0.005f, -(float)e.deltaY * 0.005f); }
- (void)mtkView:(MTKView *)v drawableSizeWillChange:(CGSize)size { (void)v; (void)size; }
- (void)drawInMTKView:(MTKView *)v {
    if (_stopping) return;
    int64_t now = now_ns();
    float dt = (float)(now - _last) / 1e9f;
    _last = now;
    // macOS virtual key codes: W 13, A 0, S 1, D 2, Q 12, E 14
    float f = (_keys[13] ? 1.f : 0.f) - (_keys[1] ? 1.f : 0.f);
    float r = (_keys[2] ? 1.f : 0.f) - (_keys[0] ? 1.f : 0.f);
    float u = (_keys[14] ? 1.f : 0.f) - (_keys[12] ? 1.f : 0.f);
    _fly.move(f, r, u, dt);
    _plat->poll_gamepad();
    CGSize ds = v.drawableSize;
    _plat->set_size((uint32_t)ds.width, (uint32_t)ds.height);
    _plat->set_time_ns(now - _t0);
    _plat->set_head(_orbit ? orbit_pose((double)(now - _t0) / 1e9, _opt.orbit_radius, _opt.orbit_height, _opt.orbit_period, _opt.orbit_center) : _fly.pose());
    id<CAMetalDrawable> d = v.currentDrawable;
    if (!d) return;
    _session->frame(false, (__bridge void *)d.texture, (__bridge void *)d);
    _frame++;
    if (_opt.screenshot_after && _frame == _opt.screenshot_after) [self saveScreenshot:_opt.screenshot.c_str()];
    if (_opt.test_close_after && _frame == _opt.test_close_after) {
        if (_opt.test_close == "window") {
            dispatch_async(dispatch_get_main_queue(), ^{ [self.window performClose:nil]; });
        } else {
            NSEvent *q = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
                                         timestamp:0 windowNumber:self.window.windowNumber context:nil characters:@"q"
                       charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
            [NSApp postEvent:q atStart:NO];
        }
    }
    if (_opt.quit_after && _frame >= _opt.quit_after) {
        // Leave the draw callback first; termination then runs the delegate's orderly teardown.
        [self stopDrawing];
        dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
    }
}
- (void)stopDrawing {
    _stopping = true;
    self.paused = YES;
    self.enableSetNeedsDisplay = NO;
}
- (void)saveScreenshot:(const char *)path {
    _session->wait_idle();
    std::vector<uint8_t> px; uint32_t w = 0, h = 0;
    if (_session->read_final(px, w, h) && png_write(path, px.data(), w, h)) std::printf("screenshot %s (%ux%u)\n", path, w, h);
    else std::fprintf(stderr, "screenshot failed: %s\n", path);
    std::fflush(stdout);
}
@end

// Orderly shutdown for every exit path (last window closed, Cmd-Q, --quit-after): stop drawing, then delete the
// session (waits for the GPU, stops the game thread, destroys the runtime) and only then the platform it uses.
@interface HarnessAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) HarnessView *view;
@property(nonatomic) HarnessSession *session;
@property(nonatomic) HarnessPlatform *platform;
@property(nonatomic) OwnedSarec *replay;     // with recording: must outlive the session, freed after it
@property(nonatomic) OwnedSarec *recording;
@property(nonatomic) const Options *options;
@end
@implementation HarnessAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { (void)app; return YES; }
- (void)applicationWillTerminate:(NSNotification *)n {
    (void)n;
    [self.view stopDrawing];
    bool diverged = false;
    if (self.session && self.options) diverged = report_replay(*self.options, self.session->runtime(), self.replay ? self.replay->r.count : 0);
    delete self.session; self.session = nullptr;
    if (self.options && self.recording) save_record(*self.options, &self.recording->r);
    delete self.replay; self.replay = nullptr;
    delete self.recording; self.recording = nullptr;
    delete self.platform; self.platform = nullptr;
    std::printf("shutdown complete\n");
    std::fflush(stdout);
    if (diverged) std::exit(3);
}
@end

static void install_menu() {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *app_item = [NSMenuItem new];
    [bar addItem:app_item];
    NSMenu *app_menu = [NSMenu new];
    [app_menu addItemWithTitle:@"Quit Diorama64 harness" action:@selector(terminate:) keyEquivalent:@"q"];
    app_item.submenu = app_menu;
    NSApp.mainMenu = bar;
}

static int run_window(const Options &o) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        auto *plat = new HarnessPlatform();          // deleted by HarnessAppDelegate on termination
        plat->set_stereo(o.stereo);
        plat->set_preview_test(o.preview_test);
        std::string err;
        auto *rec = new OwnedSarec(), *out = new OwnedSarec();   // deleted by HarnessAppDelegate after the session
        if (!load_replay(o, &rec->r)) { delete rec; delete out; delete plat; return 1; }
        std::string setup;
        if (!apply_setup(o, rec->r, &setup)) { delete rec; delete out; delete plat; return 1; }
        auto *s = new HarnessSession(game_adapter(o), *plat);
        if (!apply_game_set(o, *s)) { delete s; delete rec; delete out; delete plat; return 2; }
        s->set_audio_mode(o.audio_mode == "spatial" ? AR_AUDIO_MODE_SOURCES : AR_AUDIO_MODE_STEREO);
        s->set_overlay_panel(o.overlay);
        s->set_silhouette(o.silhouette);
        s->set_flat_hud(o.flat_hud);
        apply_look(o, *s);
        if (!s->init(o.rom, o.save, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            delete s; delete rec; delete out; delete plat;
            return 1;
        }
        apply_packs(o);
        apply_fake_probe(o, *plat, *s);
        attach_replay_and_recorder(o, s->runtime(), &rec->r, &out->r, setup);
        s->warmup(o.warmup);
        if (o.pack_stats && o.game == "sm64") std::printf("AR_PACKSTAT force3d=%d trees_3d=%d tree_cards=%d load_ms=%.1f\n", ar_adapter_sm64_objects_force_3d(),
                                      ar_adapter_sm64_trees_3d(), ar_adapter_sm64_tree_cards(), ar_adapter_sm64_pack_load_ms());
        print_actor_counts(o);
        print_mem_stats(o);
        s->start_game_thread();
        NSRect r = NSMakeRect(0, 0, o.w / 2.0, o.h / 2.0);   // points; 2x backing gives o.w x o.h pixels
        NSWindow *win = [[NSWindow alloc] initWithContentRect:r
                                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                                      backing:NSBackingStoreBuffered defer:NO];
        win.title = @"Diorama64 harness";
        HarnessView *v = [[HarnessView alloc] initWithFrame:r platform:plat session:s options:o];
        HarnessAppDelegate *app = [HarnessAppDelegate new];   // NSApp.delegate is weak; this local outlives [NSApp run]
        app.view = v; app.session = s; app.platform = plat;
        app.replay = rec; app.recording = out; app.options = &o;
        NSApp.delegate = app;
        install_menu();
        win.contentView = v;
        [win makeFirstResponder:v];
        [win center];
        [win makeKeyAndOrderFront:nil];
        std::printf("window %ld\n", (long)win.windowNumber);
        std::fflush(stdout);
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

int main(int argc, char **argv) {
    Options o;
    std::string err;
    if (!parse(argc, argv, o, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    if (o.game == "sm64" && (!o.generate_tex_in.empty() || !o.generate_actors.empty())) return run_generate(o);
    std::filesystem::create_directories(o.save);
    if (!o.offscreen) return run_window(o);
    return run_offscreen(o);
}

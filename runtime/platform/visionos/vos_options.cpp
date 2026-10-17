#include "vos_options.h"
#include "ar_game_args.h"
#include "look.h"
#include "ar_audio_settings.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>
namespace ar {
namespace {
bool parse_floats(const std::string &s, float *out, int n) {
    const char *p = s.c_str();
    for (int i = 0; i < n; i++) {
        char *end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p || !std::isfinite(v)) return false;
        out[i] = v;
        p = end;
        if (i + 1 < n) { if (*p != ',') return false; p++; }
    }
    return *p == 0;
}
bool parse_u64(const std::string &s, uint64_t *out) {
    if (s.empty() || s[0] == '-' || s[0] == '+') return false;
    char *end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (*end != 0 || errno != 0) return false;
    *out = v;
    return true;
}
}
bool parse_vos_options(const std::vector<std::string> &args, const std::vector<GameSpec> &games, VosOptions *out, std::string *err) {
    if (games.empty()) { *err = "no games supplied"; return false; }
    VosOptions o;
    o.game = games[0].name;
    for (size_t i = 1; i < args.size(); i++) {
        const std::string &a = args[i];
        std::string v;
        auto value = [&]() { if (i + 1 >= args.size()) { *err = a + " needs a value"; return false; } v = args[++i]; return true; };
        if (a == "--autostart") o.autostart = true;
        else if (a == "--reset-placement") o.reset_placement = true;
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--hide-status") o.hide_status = true;
        else if (a == "--fake-table" || a == "--no-fake-table") {
            const FakeTable m = a == "--fake-table" ? FakeTable::on : FakeTable::off;
            if (o.fake_table != FakeTable::automatic && o.fake_table != m) { *err = "--fake-table and --no-fake-table go not together"; return false; }
            o.fake_table = m;
        }
        else if (a == "--sim-plane-seek") o.sim_plane_seek = true;
        else if (a == "--reset-safe-mode") o.reset_safe_mode = true;
        else if (a == "--game") {
            if (!value()) return false;
            bool known = false;
            std::string names;
            for (const GameSpec &g : games) { known = known || g.name == v; names += (names.empty() ? "" : " or ") + g.name; }
            if (!known) { *err = "unknown --game " + v + " (" + names + ")"; return false; }
            o.game = v;
        } else if (a == "--sarec") { if (!value()) return false; o.sarec = v; }
        else if (a == "--game-arg") {
            if (!value()) return false;
            if (!valid_game_arg(v)) { *err = "bad --game-arg '" + v + "' (use KEY=VALUE)"; return false; }
            o.game_args.push_back(v);
        } else if (a == "--game-set") {
            if (!value()) return false;
            const size_t eq = v.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= v.size()) { *err = "bad --game-set '" + v + "' (use KEY=VALUE)"; return false; }
            o.game_set.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        } else if (a == "--record") { if (!value()) return false; o.record = v; }
        else if (a == "--place") {
            float f[3];
            if (!value()) return false;
            if (!parse_floats(v, f, 3) || !(f[0] >= 0.3f && f[0] <= 5.f) || !(f[1] >= -1.f && f[1] <= 2.f) || std::fabs(f[2]) > 360.f) {
                *err = "bad --place " + v + " (FORWARD_M 0.3..5, BELOW_M -1..2, YAW_DEG)"; return false;
            }
            o.place = PlaceParams{f[0], f[1], f[2], false}; o.place_given = true;
        } else if (a == "--hold-ticks") {
            if (!value()) return false;
            size_t s = 0;
            while (s <= v.size()) {
                const size_t e = v.find(',', s);
                uint64_t t = 0;
                if (!parse_u64(v.substr(s, e == std::string::npos ? std::string::npos : e - s), &t) || t == 0 ||
                    (!o.hold_ticks.empty() && t <= o.hold_ticks.back())) {
                    *err = "bad --hold-ticks " + v + " (increasing ticks > 0)"; return false;
                }
                o.hold_ticks.push_back(t);
                if (e == std::string::npos) break;
                s = e + 1;
            }
        } else if (a == "--lifecycle-test") {
            float f[2];
            if (!value()) return false;
            if (!parse_floats(v, f, 2) || !(f[0] > 0.f) || !(f[1] > 0.f)) { *err = "bad --lifecycle-test " + v + " (CLOSE_S,REOPEN_S)"; return false; }
            o.lifecycle_close_s = f[0]; o.lifecycle_reopen_s = f[1];
        } else if (a == "--replace-test") {
            float f;
            if (!value()) return false;
            if (!parse_floats(v, &f, 1) || !(f > 0.f)) { *err = "bad --replace-test " + v + " (seconds > 0)"; return false; }
            o.replace_after_s = f;
        } else if (a == "--select-test") {
            if (!value()) return false;
            const size_t c1 = v.find(','), c2 = c1 == std::string::npos ? std::string::npos : v.find(',', c1 + 1);
            float f = 0.f;
            const std::string key = c1 == std::string::npos ? "" : v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
            uint64_t n = 0;
            const bool key_ok = !key.empty() && key.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            const bool n_ok = c2 == std::string::npos || (parse_u64(v.substr(c2 + 1), &n) && n >= 1 && n <= 99);
            if (c1 == std::string::npos || !parse_floats(v.substr(0, c1), &f, 1) || !(f > 0.f) || !key_ok || !n_ok) {
                *err = "bad --select-test " + v + " (SECONDS>0,KEY[,N 1..99])"; return false;
            }
            o.select_test_s = f; o.select_test_key = key; o.select_test_n = (int)n;
        } else if (a == "--sim-pinch") {
            float f[3];
            if (!value()) return false;
            if (!parse_floats(v, f, 3) || !(f[0] > 0.f) || std::fabs(f[1]) > 2.f || std::fabs(f[2]) > 2.f) {
                *err = "bad --sim-pinch " + v + " (AT_S>0,DX_M,DZ_M)"; return false;
            }
            o.sim_pinch.push_back({f[0], f[1], f[2]});
        } else if (a == "--audio") {
            if (!value()) return false;
            if (v != "phase" && v != "remoteio") { *err = "bad --audio " + v + " (phase or remoteio)"; return false; }
            o.audio_backend = v;
        } else if (a == "--tex-filter") {
            if (!value()) return false;
            if (v != "smooth" && v != "vanilla") { *err = "bad --tex-filter " + v + " (smooth or vanilla)"; return false; }
            o.look.emplace_back("tex_filter", v);
        } else if (a == "--phase-mode") {
            if (!value()) return false;
            if (v != "ladder" && v != "client" && v != "local") { *err = "bad --phase-mode " + v + " (ladder, client or local)"; return false; }
            o.phase_mode = v;
        } else if (a == "--phase-head") {
            if (!value()) return false;
            if (v != "auto" && v != "manual") { *err = "bad --phase-head " + v + " (auto or manual)"; return false; }
            o.phase_head = v;
        } else if (a == "--phase-fail-test") {
            o.phase_fail_test = true;
        } else if (a == "--audio-tap") {
            float f;
            if (!value()) return false;
            if (!parse_floats(v, &f, 1) || !(f > 0.f && f <= 600.f)) { *err = "bad --audio-tap " + v + " (seconds, 0..600]"; return false; }
            o.audio_tap_s = f;
        } else if (a == "--draw-all-rooms") {
            if (!value()) return false;
            if (v != "on" && v != "off") { *err = "bad --draw-all-rooms " + v + " (on or off)"; return false; }
            o.draw_all_rooms = v == "on" ? 1 : 0;
        } else if (a == "--hide-outside") {
            if (!value()) return false;
            if (v != "on" && v != "off") { *err = "bad --hide-outside " + v + " (on or off)"; return false; }
            o.hide_outside = v == "on" ? 1 : 0;
        } else if (a == "--other-rooms") {
            if (!value()) return false;
            if (v != "hidden" && v != "frozen" && v != "live") { *err = "bad --other-rooms " + v + " (hidden, frozen or live)"; return false; }
            o.other_rooms = v == "hidden" ? 0 : v == "frozen" ? 1 : 2;
        } else if (a == "--tree-style") {
            if (!value()) return false;
            if (v != "card" && v != "crossed") { *err = "bad --tree-style " + v + " (card or crossed)"; return false; }
            o.tree_style = v == "card" ? 0 : 1;
        } else if (a == "--textures" || a == "--models") {
            if (!value()) return false;
            const std::string word = a == "--textures" ? "hd" : "3d";
            const std::string name = v.size() > word.size() + 1 ? v.substr(word.size() + 1) : "";
            const bool ok = v == "original" || v == word ||
                            (v.rfind(word + ":", 0) == 0 && !name.empty() && name.find('/') == std::string::npos && name != "." && name != "..");
            if (!ok) { *err = "bad " + a + " " + v + " (original, " + word + " or " + word + ":NAME)"; return false; }
            (a == "--textures" ? o.textures : o.models) = v;
        } else if (a == "--billboards") {
            if (!value()) return false;
            if (v != "full" && v != "upright") { *err = "bad --billboards " + v + " (upright or full)"; return false; }
            o.billboards = v == "full" ? 0 : 1;
        } else if (a == "--ui-tab") {
            if (!value()) return false;
        } else if (a == "--ui-publish-stats") {
        } else if (a == "--look") {
            if (!value()) return false;
            const size_t eq = v.find('=');
            LookSettings scratch;
            std::string why;
            if (eq == std::string::npos || eq == 0 || !look_set(&scratch, v.substr(0, eq), v.substr(eq + 1), &why)) {
                *err = "bad --look " + v + (why.empty() ? " (use KEY=VALUE)" : ": " + why);
                return false;
            }
            o.look.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        } else if (a == "--look-test") {
            if (!value()) return false;
            const size_t c = v.find(','), eq = v.find('=', c == std::string::npos ? 0 : c);
            float f = 0.f;
            LookSettings scratch;
            std::string why;
            if (c == std::string::npos || eq == std::string::npos || eq == c + 1 || !parse_floats(v.substr(0, c), &f, 1) || !(f > 0.f) ||
                !look_set(&scratch, v.substr(c + 1, eq - c - 1), v.substr(eq + 1), &why)) {
                *err = "bad --look-test " + v + (why.empty() ? " (use SECONDS,KEY=VALUE)" : ": " + why);
                return false;
            }
            o.look_test.push_back(VosOptions::LookTest{f, v.substr(c + 1, eq - c - 1), v.substr(eq + 1)});
        } else if (a == "--audio-set") {
            if (!value()) return false;
            const size_t eq = v.find('=');
            std::vector<std::pair<std::string, std::string>> kv;
            std::string why;
            if (eq == std::string::npos || eq == 0 || !audio_expand_option(v.substr(0, eq), v.substr(eq + 1), &kv, &why)) {
                *err = "bad --audio-set " + v + (why.empty() ? " (use KEY=VALUE)" : ": " + why);
                return false;
            }
            o.audio_set.insert(o.audio_set.end(), kv.begin(), kv.end());
        } else if (a == "--msaa") {
            if (!value()) return false;
            if (v != "1" && v != "2" && v != "4") { *err = "bad --msaa " + v + " (1, 2 or 4)"; return false; }
            o.look.emplace_back("msaa", v);
        } else if (a == "--foveation") {
            if (!value()) return false;
            if (v != "on" && v != "off") { *err = "bad --foveation " + v + " (on or off)"; return false; }
            o.foveation = v == "on";
            o.look.emplace_back("foveation", o.foveation ? "1" : "0");
        } else if (a == "--render-quality") {
            float q = 0.f;
            if (!value()) return false;
            LookSettings scratch;
            if (!parse_floats(v, &q, 1) || !(q > 0.f && q <= 1.f) || !look_set(&scratch, "render_quality", v, nullptr)) {
                *err = "bad --render-quality " + v + " (0 < Q <= 1)";
                return false;
            }
            o.render_quality = q;
            o.look.emplace_back("render_quality", look_get(scratch, "render_quality"));
        } else if (a == "--linearize") {
            if (!value()) return false;
            if (v != "0" && v != "1") { *err = "bad --linearize " + v + " (0 or 1)"; return false; }
            o.look.emplace_back("colour", v == "1" ? "linear" : "raw");
        } else if (a == "--log-hash-every") {
            uint64_t n = 0;
            if (!value()) return false;
            if (!parse_u64(v, &n) || n == 0) { *err = "bad --log-hash-every " + v; return false; }
            o.log_hash_every = n;
        } else if (a.rfind("--", 0) == 0) {
            *err = "unknown option " + a; return false;
        } else if (a.size() > 1 && a[0] == '-') {            // Apple launch defaults: -Key Value
            if (i + 1 < args.size() && args[i + 1].rfind("-", 0) != 0) i++;
        } else {
            *err = "unexpected argument " + a; return false;
        }
    }
    if (!o.place_given)
        for (const GameSpec &g : games) if (g.name == o.game) o.place = g.place;
    *out = o;
    return true;
}
bool fake_table_enabled(FakeTable mode, bool simulator) {
    return mode == FakeTable::on || (mode == FakeTable::automatic && simulator);
}
std::string resolve_documents_path(const std::string &docs, const std::string &p) {
    return !p.empty() && p[0] == '/' ? p : docs + "/" + p;
}
std::string bundled_data(const std::string &bundle, const std::string &rel) {
    return (bundle.empty() ? std::string("Bundled") : bundle + "/Bundled") + "/" + rel;
}
bool game_setting_change_allowed(const VosOptions &o, std::string *why) {
    if (!o.sarec.empty()) { *why = "a recording replays (--sarec)"; return false; }
    if (!o.record.empty()) { *why = "recording (--record)"; return false; }
    return true;
}
bool level_switch_allowed(const VosOptions &o, std::string *why) { return game_setting_change_allowed(o, why); }
bool is_rom_error(const std::string &err) {
    return err.rfind("ROM not found", 0) == 0 || err.rfind("not an N64 ROM", 0) == 0 || err.rfind("ROM rejected", 0) == 0;
}
std::string rom_error_hint(const std::string &err) {
    if (err.rfind("ROM not found", 0) == 0)
        return "this build has no ROM: build it with ./build.py, which puts your Super Mario 64 (US) ROM inside the app";
    return "the ROM inside this build is not usable (" + err.substr(0, err.find(':')) + "): build it again with ./build.py "
           "--rom PATH and your Super Mario 64 (US) ROM";
}
bool valid_save_folder_name(const std::string &n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return true;
}
SaveFolderChoice save_folder_for(const std::string &game, bool story_start, bool replay, bool record) {
    SaveFolderChoice c;
    if (game == "sm64" && story_start && (replay || record)) { c.name = "story-scratch-save"; c.fresh = true; }
    return c;
}
bool prepare_save_folder(const std::string &docs, const std::string &name, bool fresh, std::string *folder, bool *deleted,
                         std::string *err) {
    *folder = valid_save_folder_name(name) ? name : std::string("save");
    *deleted = false;
    if (!fresh || *folder == "save") return true;
    std::error_code ec;
    std::filesystem::remove_all(docs + "/" + *folder, ec);
    // a stale scratch save would make the story replay diverge; fail the start instead of going on
    if (ec) { *err = "cannot delete the old Documents/" + *folder + " for a fresh start: " + ec.message(); return false; }
    *deleted = true;
    return true;
}
}

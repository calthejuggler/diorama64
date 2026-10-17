#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "vos_placement.h"
namespace ar {
struct GameSpec { std::string name; PlaceParams place; };
enum class FakeTable { automatic, on, off };
struct VosOptions {
    std::string game;
    std::string rom = "rom.z64";
    std::string sarec;
    std::string record;
    std::vector<std::string> game_args;
    std::vector<std::pair<std::string, std::string>> game_set;
    bool autostart = false;
    bool reset_placement = false;
    PlaceParams place;
    bool place_given = false;
    std::vector<uint64_t> hold_ticks;
    double lifecycle_close_s = 0, lifecycle_reopen_s = 0;
    bool sim_plane_seek = false;
    std::vector<std::array<float, 3>> sim_pinch;
    double replace_after_s = 0;
    double select_test_s = 0;
    std::string select_test_key;
    int select_test_n = 0;
    bool audio = true;
    std::string audio_backend = "phase";
    // Default local: the client rung is unconfirmed on a device.
    std::string phase_mode = "local";
    std::string phase_head = "manual";
    bool phase_fail_test = false;
    float audio_tap_s = 0.f;
    bool hide_status = false;
    bool reset_safe_mode = false;
    bool foveation = true;
    float render_quality = -1.f;
    uint64_t log_hash_every = 0;
    FakeTable fake_table = FakeTable::automatic;
    int draw_all_rooms = -1;
    int other_rooms = -1;
    int tree_style = -1;
    int billboards = -1;
    int hide_outside = -1;
    std::string textures;
    std::string models;
    std::vector<std::pair<std::string, std::string>> audio_set;
    std::vector<std::pair<std::string, std::string>> look;
    struct LookTest { double at_s; std::string key, value; };
    std::vector<LookTest> look_test;
};
bool parse_vos_options(const std::vector<std::string> &args, const std::vector<GameSpec> &games, VosOptions *out, std::string *err);
bool fake_table_enabled(FakeTable mode, bool simulator);
std::string resolve_documents_path(const std::string &docs, const std::string &p);
// a save folder name under Documents: 1..64 of [A-Za-z0-9_-] (no path, no dots), so a fresh scratch save can be
// deleted without ever reaching outside Documents.
bool valid_save_folder_name(const std::string &name);
struct SaveFolderChoice { std::string name = "save"; bool fresh = false; };
SaveFolderChoice save_folder_for(const std::string &game, bool story_start, bool replay, bool record);
// fresh: delete it first, never "save" (the player's own); a stale scratch save would make a replay diverge.
bool prepare_save_folder(const std::string &docs, const std::string &name, bool fresh, std::string *folder, bool *deleted,
                         std::string *err);
std::string bundled_data(const std::string &bundle, const std::string &rel);
bool is_rom_error(const std::string &err);
std::string rom_error_hint(const std::string &err);
// A live change of a setting that changes the game is outside input a .sarec cannot hold: refused while a recording
// replays or records.
bool game_setting_change_allowed(const VosOptions &o, std::string *why);
bool level_switch_allowed(const VosOptions &o, std::string *why);
// The lock on the game's settings that change the game: set (with the reason) once a
// recording starts or replays; from then on change() refuses every live change. The check and the change run together
// under the lock's mutex, so no change lands between the session reading a value for its recording and the lock. Any thread.
class GameSettingLock {
public:
    void lock(const std::string &why) { std::lock_guard<std::mutex> g(m_); why_ = why; }
    std::string why() const { std::lock_guard<std::mutex> g(m_); return why_; }
    template <class F> bool change(F &&apply, std::string *why = nullptr) {
        std::lock_guard<std::mutex> g(m_);
        if (!why_.empty()) { if (why) *why = why_; return false; }
        apply();
        return true;
    }
private:
    mutable std::mutex m_;
    std::string why_;
};
}

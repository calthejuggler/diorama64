# Quest 3S Port, Plan 3 of 3: Finish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Quest app is complete: stereo and spatial audio, the floating settings panel with the Level, Look, Audio and World tabs, settings that persist, the ROM and packs bundled inside the APK by `./build.py` on Linux, a performance pass at the display rate, and the one Mac check of the visionOS build.

**Architecture:** Audio follows `vos_audio.mm`'s shape on AAudio: a callback pulls 32 kHz game audio through the portable latency bound and resampler; spatial mode reuses `SceneFeed` and `ScenePositions` with a new pure stereo panner. The app's C bridge (`apps/visionos/vos_api.h`) is implemented for Quest by a port of `vos_app.mm` with a JSON preferences store in place of `UserDefaults`, and the panel is a pure model (controls built from that bridge) rasterised on the CPU into a texture the existing renderer draws as a textured quad, pointed at with the right controller. Bundling stages the same `Bundled/` tree as visionOS into APK assets, copied to the app's files directory on first run. `build.py` learns a Linux path that ends in `quest_build.py` and adb.

**Tech Stack:** as plans 1 and 2, plus AAudio (NDK), `AAssetManager`, `stb_truetype.h` (vendored single header, public domain) with the device's `/system/fonts/Roboto-Regular.ttf`.

**Spec:** `docs/superpowers/specs/2026-10-22-quest3s-port-design.md`. Plans 1 and 2 in `docs/superpowers/plans/`.

## Global Constraints

All of plans 1 and 2, plus:

- Audio: the game produces 32 kHz int16 stereo (`AR_AUDIO_RATE_HZ`), output is 48 kHz float stereo; latency constants `kAudioStandingFrames 800`, `kAudioWindowFrames 2400`, `kAudioQueueFrames 16384` (`vos_latency.h`); the resampler choice follows the sm64 `resampler` setting through `feed_choice` (`vos_scene_audio.h`). Spatial mode is the adapter's `AR_AUDIO_MODE_SOURCES` (up to 32 lanes), chosen before `init` with `audio_set_mode`.
- Panning is constant-power from the listener-relative azimuth (`listener_direction` in `vos_scene_pos.h`); per-lane gain is `ar_audio_lane_gain` with the tuning from `audio_tuning_from_settings`. No HRTF, no Meta audio SDK.
- Settings keys, defaults, ranges and persistence names are exactly the visionOS window's (`apps/visionos/SM64ARApp.swift`, `LookLoad.swift`); the inventory is in this plan's Task 4. Persisted keys use the same names (`look.<key>`, `sm64.<key>`, `audio.<key>`, `marioCm`, `drawAllRooms`, `hideOutside`, `otherRoomObjects`, `billboards`, `cannonVisible`, `treeStyle`, `startIn`, `packTexturesOn`, `packTextures`, `packModelsOn`, `packModels`, `ui.tab`, `look.defaults_rev`, `sm64.defaults_rev`, `audio.defaults_version`).
- The panel is drawn only by the app (never by the game), after the level, with premultiplied alpha, 0.6 m wide, anchored by `ar_float_panel_anchor` with `above_top_m 0.05`, `max_above_ground_m 0.45`, `edge_gap_m 0.1`, facing the head; toggled by the left controller's menu button; pointed at with the right controller's aim ray; the trigger activates; the right thumbstick's X adjusts the hovered slider.
- Bundled data inside the APK: `assets/Bundled/rom.z64`, `assets/Bundled/packs/<name>/...`, `assets/Bundled/bundled.json` (the expected tree hashes), `assets/res/{lang,dynos,palettes,mods}`; copied to `internalDataPath` when `bundled.json` differs from the copy there. Nothing is read from external storage any more; the plan-2 adb push path is removed.
- `build.py` on Linux never asks for a team, bundle id or Apple device; it asks for the ROM and the packs, checks the Nix tools, and installs over adb.

## Review Focus

1. Audio device changes or the session pauses mid-stream (AAudio `XRUN`/disconnect): the stream is rebuilt, no crash, and the game audio ring is drained so it does not burst on resume. (Task 1: `QxAudio` restart path, on-device doff test.)
2. A persisted setting is invalid or from an older defaults revision: it is dropped exactly as `LookLoad.swift` does, the default applies, and the app does not fail to start. (Task 3 `qx_prefs` tests.)
3. The panel is opened while the level is very tall or the head is beside the table: it is anchored beside the level toward the head, never inside geometry, and never off-screen. (Task 5 anchor test over `ar_float_panel_anchor` cases.)
4. The bundled data in the APK changes (new packs chosen): the app detects the changed `bundled.json` and re-copies; an interrupted copy (killed mid-way) is redone on the next launch. (Task 6 copy-manifest tests and an on-device kill test.)
5. The spatial mix is given a lane with a NaN position or a listener pose that is not finite: the lane is muted for that cycle, nothing else is affected. (Task 2 panner test.)

---

## File structure

Created:

- `runtime/platform/quest/qx_audio.h/.cpp` (AAudio stream, stereo and scene feeds), `qx_panner.h/.cpp` (pure spatial mixer), `qx_prefs.h/.cpp` (JSON key-value store and the load rules), `qx_app.h`.
- `apps/quest/quest_app.cpp` (implements `vos_api.h` for Quest), `apps/quest/panel/panel_model.h/.cpp`, `panel_raster.h/.cpp`, `panel_place.h/.cpp`, `apps/quest/third_party/stb_truetype.h`, `apps/quest/bundled_copy.h/.cpp`.
- `runtime/tests/test_qx_panner.cpp`, `test_qx_prefs.cpp`, `test_panel_model.cpp`, `test_panel_place.cpp`, `test_bundled_copy.cpp`, `scripts/tests/test_guided_quest.py`.
- `scripts/quest_bundle.py` (stages assets, writes `bundled.json`).

Modified: `qx_session.cpp/.h`, `qx_platform.cpp/.h`, `apps/quest/main.cpp`, `apps/quest/CMakeLists.txt`, `scripts/quest_build.py`, `scripts/guided_build.py`, `scripts/bundle_local.py` (manifest output), `README.md`, `runtime/CMakeLists.txt`, `runtime/tests/CMakeLists.txt`.

---

### Task 1: Stereo audio on AAudio

**Files:**
- Create: `runtime/platform/quest/qx_audio.h/.cpp`
- Modify: `runtime/platform/quest/qx_session.cpp/.h`, `runtime/CMakeLists.txt` (`ar_quest` links `aaudio`)

**Interfaces:**
- Produces (namespace `ar`):
```cpp
class QxAudio {
public:
    explicit QxAudio(ar_runtime *rt);
    bool start(bool spatial, std::string *err);   // AAudio: PERFORMANCE_MODE_LOW_LATENCY, SHARING_MODE_SHARED, float, 2 channels, 48000 Hz, callback
    void pause(); void resume(); void stop();      // session VISIBLE/FOCUSED transitions
    bool running() const; bool spatial() const;
    uint64_t frames_out() const; uint64_t missing_in() const; uint64_t trimmed_in() const;
    void set_tuning(const ar_audio_tuning &t); void set_poly_mode(PolyMode m);   // spatial only (Task 2)
    void listener(const ar_pose &tracking_from_head, const ar_pose &tracking_from_table);   // render thread, each frame
};
```
Stereo path: the callback renders through `AudioLatencyBound` (`vos_latency.h`) over `ar_runtime_pull_audio`, then `StereoResampler` 32→48 kHz, exactly as `vos_audio.mm` (read it). On `AAUDIO_ERROR_DISCONNECTED` the error callback schedules a restart on the session thread (flag polled in `QxSession::frame`), which recreates the stream and resets the latency bound. `QxSession::start` creates `QxAudio` after `init_game`; `frame()` calls `pause`/`resume` on session state changes, and `status()` reports `audio=stereo|spatial|off frames=… missing=… trimmed=…`.

- [ ] **Step 1: Implement** stereo only (`start(false, …)`).
- [ ] **Step 2: On the headset**: game music and SFX play in both ears without crackle; `adb shell dumpsys media.audio_flinger | grep -A3 Diorama` shows one 48 kHz stream; doff for 10 s and don: audio resumes without a burst (no `AR_AUDIO trimmed` jump over 2400 frames in the log); the sm64 `audio` setting off silences it.
- [ ] **Step 3: Commit** — `git commit -m "quest: stereo audio on AAudio"`.

---

### Task 2: Spatial audio with a pure panner

**Files:**
- Create: `runtime/platform/quest/qx_panner.h/.cpp`, `runtime/tests/test_qx_panner.cpp`
- Modify: `runtime/platform/quest/qx_audio.cpp`, `runtime/CMakeLists.txt` (`ar_qx_panner` host and Android)

**Interfaces:**
- Produces (namespace `ar`):
```cpp
struct PannerLane { bool active; float gain; float azimuth_deg; };   // azimuth from listener_direction after ar_widen_azimuth_deg
// Pure. From the lanes' sources (positions in table metres) and the listener (table space): per-lane gain via ar_audio_lane_gain
// (nullptr tuning = defaults clamped) and azimuth; a lane with a non-finite position or an inactive source is {false, 0, 0}.
void qx_panner_plan(const ar_audio_tuning &t, const ar_pose &listener, const ar_audio_source src[AR_AUDIO_MAX_SOURCES], PannerLane out[AR_AUDIO_MAX_SOURCES]);
// Pure. Constant-power stereo: left = cos(theta), right = sin(theta), theta = (azimuth clamped to [-90, 90] + 90) * pi / 360.
void qx_pan_gains(float azimuth_deg, float *left, float *right);
// Pure. Mixes one SceneFeed cycle: in = kSceneChannels interleaved floats (bed L, bed R, then 32 lanes), out = stereo interleaved.
// The lane gains ramp linearly from prev to plan over the block (no zipper noise).
void qx_panner_mix(const float *in, uint32_t frames, const PannerLane prev[AR_AUDIO_MAX_SOURCES], const PannerLane plan[AR_AUDIO_MAX_SOURCES], float bed_gain, float *out);
```
`QxAudio::start(true, …)`: `SceneFeed` (`vos_scene_audio.h`) with `in_hz 32000, out_hz 48000, polyphase per feed_choice`, source `ar_runtime_pull_audio_scene`; `ScenePositions` fed from `pop_meta`; each callback: `render(key++, frames, kSceneChannels, scratch)`, `at(heard_frame, sources, &listener)` (the listener from the adapter's meta is in table space; the head from `listener()` is used when the meta has none), `qx_panner_plan`, `qx_panner_mix`. `QxSession` applies the `AudioStore` generation changes as `VosSession::frame` does (`set_tuning`, `set_poly_mode`), and sets the adapter's mode to SOURCES before `init_game` when the sm64 `sound` setting is `spatial`.

- [ ] **Step 1: Write `test_qx_panner.cpp`**

```cpp
float l, r;
qx_pan_gains(0.f, &l, &r);   CHECK(fabsf(l - r) < 1e-6f && fabsf(l * l + r * r - 1.f) < 1e-5f);
qx_pan_gains(90.f, &l, &r);  CHECK(r > 0.99f && l < 0.01f);
qx_pan_gains(-90.f, &l, &r); CHECK(l > 0.99f && r < 0.01f);
qx_pan_gains(170.f, &l, &r); CHECK(r > 0.99f);                                  // behind: clamped to the side
ar_audio_tuning t{}; ar_audio_tuning_clamp(&t);
ar_pose lis = ar_pose_identity(); lis.pos[2] = 1.f;                                // listener 1 m in front, looking -Z
ar_audio_source src[AR_AUDIO_MAX_SOURCES]{};
src[0] = {1, AR_SRC_ACTIVE, {0.5f, 0.f, 0.f}, 1.f};                               // right of centre, ahead
src[1] = {2, AR_SRC_ACTIVE, {NAN, 0.f, 0.f}, 1.f};
src[2] = {3, AR_SRC_ACTIVE, {0.f, 0.f, 0.f}, 1.f};
src[3] = {4, AR_SRC_ACTIVE | AR_SRC_NO_DISTANCE_LOSS, {0.f, 0.f, -5.f}, 1.f};
PannerLane plan[AR_AUDIO_MAX_SOURCES]; qx_panner_plan(t, lis, src, plan);
CHECK(plan[0].active && plan[0].azimuth_deg > 5.f && !plan[1].active && plan[1].gain == 0.f);
CHECK(plan[2].active && fabsf(plan[2].azimuth_deg) < 1e-3f && plan[2].gain >= plan[3].gain);   // at the listener's feet: at least as loud as 6 m away
CHECK(!plan[4].active);
// mix: a lane fully right lands in the right channel only, the bed passes through
std::vector<float> in(kSceneChannels * 4, 0.f), out(8, 0.f);
for (uint32_t f = 0; f < 4; f++) { in[f * kSceneChannels + 0] = 0.25f; in[f * kSceneChannels + 1] = -0.25f; in[f * kSceneChannels + 2] = 1.f; }
PannerLane z[AR_AUDIO_MAX_SOURCES]{}; PannerLane p1[AR_AUDIO_MAX_SOURCES]{}; p1[0] = {true, 1.f, 90.f};
qx_panner_mix(in.data(), 4, p1, p1, 1.f, out.data());
CHECK(fabsf(out[6] - 0.25f) < 1e-5f && fabsf(out[7] - (-0.25f + 1.f)) < 1e-5f);     // last frame: L = bed, R = bed + lane
qx_panner_mix(in.data(), 4, z, p1, 1.f, out.data());
CHECK(out[1] < out[7]);                                                           // ramping in: the first frame has less lane than the last
// a non-finite listener mutes every lane
lis.pos[0] = NAN; qx_panner_plan(t, lis, src, plan); for (auto &L : plan) CHECK(!L.active);
```
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** `qx_panner.cpp` and the spatial path in `qx_audio.cpp`.
- [ ] **Step 4: Run** host tests; on the headset with `sound=spatial`: a coin collected to your left is louder in the left ear; turning the head swings it; music stays as the ring/bed setting says; switching `sound` live between stereo and spatial takes effect without a restart (the session restarts `QxAudio`).
- [ ] **Step 5: Commit** — `git commit -m "quest: spatial audio with constant-power panning over the scene feed"`.

---

### Task 3: Preferences store and the settings load rules

**Files:**
- Create: `runtime/platform/quest/qx_prefs.h/.cpp`, `runtime/tests/test_qx_prefs.cpp`

**Interfaces:**
- Produces (namespace `ar`):
```cpp
class QxPrefs {   // a flat JSON object of string values at path; atomic save (write temp, rename); any thread
public:
    explicit QxPrefs(const std::string &path); bool load(std::string *err); bool save(std::string *err);
    bool has(const std::string &key) const; std::string get(const std::string &key, const std::string &fallback = "") const;
    void set(const std::string &key, const std::string &value); void remove(const std::string &key);
    int get_int(const std::string &key, int fallback) const; void set_int(const std::string &key, int v);
};
struct PrefLoadResult { std::string key, value; bool from_launch; };
// Pure port of LookLoad.swift:15-40: for each key, the stored revision is max(prefix+"defaults_rev", prefix+"defaults_version");
// if it is older than new_rev and drop_stored(key, stored, stored_rev, has_launch) says so, the stored value is removed; the value
// used is launch, else stored, else default; a launch value is written; afterwards prefix+"defaults_rev" = new_rev.
std::vector<PrefLoadResult> qx_load_values(QxPrefs &p, const std::string &prefix, const std::vector<std::string> &keys,
    const std::function<bool(const std::string &, std::string *)> &default_of, int new_rev,
    const std::function<bool(const std::string &, const std::string &, int, bool)> &drop_stored,
    const std::function<bool(const std::string &, std::string *)> &launch_of);
// Pure port of SM64ARApp.swift:200-212 for audio: removes stored values equal to old defaults {1: ring_focus "1", 2: music "box"}
// below audio.defaults_version, then sets it to 2.
void qx_migrate_audio_defaults(QxPrefs &p);
```

- [ ] **Step 1: Write `test_qx_prefs.cpp`**: round trip (set, save, new instance load, get); `load` on a missing file succeeds with no keys; a corrupt file fails `load` with a message and leaves the store empty; `qx_load_values` with stored `look.shadows = "objects"`, `look.defaults_rev = 0`, new rev 2 and a `drop_stored` that returns true for that pair → the result value for `shadows` is the default and `has("look.shadows")` is false, `get_int("look.defaults_rev")` is 2; with a launch value for `msaa` → result `from_launch`, stored `look.msaa` written; `qx_migrate_audio_defaults` removes `audio.music = "box"` when `audio.defaults_version` is 1 and keeps `audio.music = "ring"`.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** (hand-written JSON for a flat string map; reuse `vos_placement.cpp`'s JSON helpers if they are reusable, else 60 lines of your own).
- [ ] **Step 4: Run** tests.
- [ ] **Step 5: Commit** — `git commit -m "quest: JSON preferences store with the visionOS load rules"`.

---

### Task 4: The app bridge: `vos_api.h` on Quest

**Files:**
- Create: `apps/quest/quest_app.cpp`, `runtime/platform/quest/qx_app.h`
- Modify: `apps/quest/main.cpp`, `apps/quest/CMakeLists.txt` (include `apps/visionos` for the header only), `runtime/platform/quest/qx_session.cpp/.h`

**Interfaces:**
- Consumes: `apps/visionos/vos_api.h` (unchanged), `apps/visionos/vos_app.mm` as the reference.
- Produces: every `arvos_*` function in `vos_api.h` implemented for Quest in `quest_app.cpp`, with these differences: `arvos_configure(argc, argv, bundle_dir, documents_dir)` takes `bundle_dir = internalDataPath + "/bundled"` and `documents_dir = internalDataPath`, argv from the launch intent extra `args` (space-separated; default empty) so `--game`, `--look`, `--audio-set`, `--game-set`, `--start` keep working; the pads timer is gone (`QxInput` syncs in `wait_frame`); `arvos_layer_run(nullptr)` runs the frame loop on the calling thread until the platform stops; `arvos_lifecycle_test`, `arvos_replace_test`, `arvos_render_probe_*`, `arvos_push_spatial_event`, `arvos_foveation`, `arvos_render_quality` are no-ops returning their neutral values; `arvos_replace_table` calls `QxPlatform::replace_table()`; `arvos_set_background` maps to session pause; persisted values go through `QxPrefs` at `internalDataPath + "/prefs.json"` with the key names from Global Constraints; `arvos_status` text is what `vos_app.mm:184-193` composes plus the placer hint. The startup sequence of `SM64ARApp.init` (`configure`, `loadGame`, world settings, look values through `qx_load_values`, packs, audio) is `qx_app_startup()` in `quest_app.cpp`, called by `main.cpp` after `QxPlatform::start`. Quest-only: `arvos_quest_hint()` returns the placer hint for the panel; `arvos_quest_seed_look(const char *key, const char *value)` writes a value only when the key is not stored (Task 8).

- [ ] **Step 1: Port `vos_app.mm`** function by function (it is 93 % portable; the `NSUserDefaults`, dispatch timer, `TARGET_OS_SIMULATOR`, `@autoreleasepool` and plane-provider lines are the only Apple parts). Keep every `AR_*` log line.
- [ ] **Step 2: Host test**: none beyond compiling `quest_app.cpp` for the host behind `SM64AR_HOST_TESTS` with `QxPlatform` stubbed? No: `quest_app.cpp` depends on OpenXR. Verification is on the device: `adb shell am start -n org.pagetable.diorama64/android.app.NativeActivity --es args "--game sm64 --look msaa=2 --start story"`; logcat shows `AR_ARGS`, `AR_LOOK msaa=2`, the title screen; `prefs.json` in the files dir contains `"look.msaa": "2"` afterwards (`adb shell run-as` is unavailable for a non-debuggable APK: set `android:debuggable="true"` in the manifest for this plan's debug builds and remove it in Task 9).
- [ ] **Step 3: Commit** — `git commit -m "quest: the vos_api bridge on Quest with JSON preferences"`.

---

### Task 5: Settings panel: model, placement, raster, interaction

**Files:**
- Create: `apps/quest/panel/panel_model.h/.cpp`, `panel_place.h/.cpp`, `panel_raster.h/.cpp`, `apps/quest/third_party/stb_truetype.h`, `runtime/tests/test_panel_model.cpp`, `test_panel_place.cpp`
- Modify: `runtime/platform/quest/qx_session.cpp/.h`, `apps/quest/main.cpp`, `apps/quest/CMakeLists.txt`, `runtime/tests/CMakeLists.txt` (the panel model and placement are compiled into a host library `quest_panel` from `apps/quest/panel`, without `quest_app.cpp`)

**Interfaces:**
- Produces (namespace `ar::panel`):
```cpp
enum class Kind { segmented, toggle, slider, button, picker, stepper, text };
struct Control { Kind kind; std::string id, label; std::vector<std::string> options; float min = 0, max = 1, step = 0.05f; bool enabled = true; std::string note; };
struct Tab { std::string name; std::vector<Control> controls; };
struct Bridge {   // the arvos_* calls behind the model, as function pointers so tests can fake them
    std::function<std::string(const std::string &id)> get;             // current value as text ("1"/"0", option name, number)
    std::function<bool(const std::string &id, const std::string &v, bool final)> set;
    std::function<bool()> has_levels; std::function<bool()> locked;     // sm64 present; game lock (always false on Quest)
    std::function<std::vector<std::string>(const std::string &list)> names;   // "levels", "packs_tex", "packs_models", "audio_presets", "look_keys", "audio_keys", "sm64_keys"
    std::function<std::string()> status; std::function<std::string()> hint; std::function<std::string()> now_line;
};
// Pure: builds the four tabs (Level, Look, Audio, World; World only when has_levels) with exactly the controls of SM64ARApp.swift in
// its order, ids = the setting keys with their prefixes ("look.shadows", "sm64.menu_size", "audio.widen", "world.mario_cm",
// "level.start_in", "level.level", "level.act", "level.go", "level.play_from_start", "look.packs_tex_on", "look.packs_tex", ...,
// "table.replace", "app.quit"), with the enable rules (e.g. look.edge_aa disabled when msaa == 1; the shadow group when shadows == off;
// packs and light controls when mode != roomlit; ring_* when music != ring; hide_outside when draw_all_rooms off).
std::vector<Tab> build_tabs(const Bridge &b);
struct Layout { uint32_t width = 1024, height = 768; float row_h = 36.f, margin = 24.f; };
struct Hit { int tab = -1; int control = -1; float t = 0.f; bool tab_strip = false; };   // t: 0..1 along a slider / segmented
Hit hit_test(const std::vector<Tab> &tabs, int tab, const Layout &L, float u, float v, int scroll_rows);   // u, v in 0..1 panel space
struct Action { enum { none, select_tab, set, press, scroll } kind; std::string id, value; int tab; };
Action act_on(const std::vector<Tab> &tabs, int tab, const Hit &h, bool trigger_edge, float stick_x, float dt_s);   // slider: stick_x * (max-min) * 0.5 per second; segmented: next/previous; toggle: flip on trigger
// Pure placement: a quad of width_m facing the head at the anchor from ar_float_panel_anchor; tracking_from_panel pose and the quad corners.
struct PanelPose { ar_pose tracking_from_panel; float corners[4][3]; float height_m; };
PanelPose place_panel(const ar_render_frame &rf, float width_m, float aspect);
bool ray_hit_panel(const PanelPose &p, const float o[3], const float d[3], float *u, float *v);   // u right, v down in 0..1
```
Raster (`panel_raster.h`): `class Raster { Raster(uint32_t w, uint32_t h); bool load_font(const char *ttf_path); void draw(const std::vector<Tab>&, int tab, const Hit &hover, const Layout&, int scroll_rows, const std::string &status, const std::string &hint); const uint8_t *rgba() const; }` with `stb_truetype` (bake a 24 px and a 16 px atlas once); without a font, text is drawn as 5×7 bitmap glyphs from a built-in table so the panel never goes blank. The session: `QxSession` owns the panel state (open flag toggled by the menu edge from `QxInput`, current tab persisted under `ui.tab`, hover from `ray_hit_panel` with the right aim ray while open, `act_on` each frame), re-rasterises when anything changed (`upload_texture(kPanelTexture = 0xFFFFFE01, …)`), and draws it through `Renderer`/`encode_pass` as two triangles in a WORLD-space pass with `ViewConstants.clip_from_model = clip_from_tracking · tracking_from_panel`, `DrawPacket{tex[0] = kPanelTexture, cc = TEXEL0 (rgb and alpha), blend 1, depth_test 1, depth_write 0}` after the AR layer, and the controller ray as a `PreviewLine` through `Renderer::draw_lines` from the controller to the hit point (0.004 m wide, white at 60 %). While the panel is open the game pointer is not fed (the panel owns the trigger).

- [ ] **Step 1: Write `test_panel_model.cpp`**: a fake `Bridge` (a map of values; `has_levels` true; names lists); `build_tabs` returns 4 tabs named Level, Look, Audio, World in that order; with `has_levels` false, 3 tabs and no `sm64.*` controls; the Look tab contains controls with ids `look.mode`, `look.msaa`, `look.edge_aa`, … every key of `look_keys` as an id (assert each of the 32 keys appears) plus `look.packs_tex_on` and the 32 sm64 keys as `sm64.*`; with `look.msaa = "1"`, `look.edge_aa` is disabled; with `look.mode = "original"`, `look.source` is disabled; with `audio.music = "box"`, `audio.ring_radius` is disabled; slider `audio.spotlight` has `min -30, max 0, step 1`; `look.key_az` has `0, 360, 5`; `world.mario_cm` is a slider `0.5..10`; `hit_test` at the first row's centre gives control 0; `act_on` with `trigger_edge` on a toggle returns `set` with the flipped value; on a segmented control returns the next option; `stick_x = 1, dt 1` on `audio.widen` (1..2) returns `set` with `"1.5"` rounded to the step; `act_on` on `level.go` returns `press`.
- [ ] **Step 2: Write `test_panel_place.cpp`**: `place_panel` with a level 0.3 m tall at the table origin and the head 1 m in front → the panel's centre is above the level (`y > level top`), its normal points at the head (dot of panel −Z and the head direction > 0.9); with a level 2 m tall → beside it toward the head; `ray_hit_panel` from the head through the panel centre gives `u ≈ 0.5, v ≈ 0.5`; a ray past the edge misses.
- [ ] **Step 3: Run to verify they fail.**
- [ ] **Step 4: Implement** the model (from the inventory below), placement, raster and session wiring.

Inventory to reproduce (from `SM64ARApp.swift`; the visionOS tab order is Level, Look, Audio, World):
- **Level**: `start_in` segmented Story/Level (`arvos_set_start_in`); note text; `play_from_start` button (`arvos_play_from_start`); `level` picker from `arvos_level_count/name/section/label` (section 1 under "Secret & Bowser"); `act` stepper 0..6 when `arvos_level_has_acts`; `go` button (`arvos_select_level(index, act)`); header lines: `now_line` from `arvos_level_view_get`, `status` from `arvos_status`; buttons `table.replace` (`arvos_replace_table`) and `app.quit` (`arvos_prepare_quit(3)` then `arvos_quit_now`).
- **Look**: `mode` original/roomlit; packs group (textures: Original/HD + name picker; models: Original/3D + name picker; `arvos_pack_count/info/option`, `arvos_set_packs`); `source` room/fixed; `light_response` steady/responsive; room light readout (`arvos_room_light_get`); sliders `match_brightness`, `match_colour` 0..1/0.05; buttons "Set as neutral"/"Reset neutral" (`neutral_lum`); `contact` + `contact_strength`; `shadows` off/objects/all, `blob` keep/hide, `softness`, `table_cast`; `relight`+`relight_strength`, `ao`+`ao_strength`, `toy`+`gloss`+`rim`, `reflections`; `key_az` 0..360/5, `key_el` 10..90/5, `key_power` 0.3..1.5/0.05, `warmth` −1..1/0.05, `directional` 0..1/0.05; `msaa` 1/2/4, `edge_aa`, `foveation` (shown, no effect on Quest: note "no effect on this headset"), `render_quality` 0/0.6/0.8/0.9/1, `tex_filter` smooth/vanilla, `decal_fix`, `colour` auto/linear/raw; sm64 "Story and 3D models": `pack_tilt`, `menu_size`, `menu_backdrop`, `menu_backdrop_white`, `letter_front`, `score_float`, `score_seated`, `intro_pip`, `pip_follow`, `title_eye_level`, `title_touch`, `title_eyes`, `room_reveal`, `gameover_text`, `splash_lift`, `title_light`; sm64 "Diorama": `below_table`, `trim_level`, `liquids`, `water_trim`, `behind_walls`, `transitions`, `trans_fade`, `fine_textures`, `dialog_plate`, `dialog_size`, `act_star_solid`.
- **Audio**: sm64 `sound`, `audio`, `reverb`, `event_music`, `resampler`; presets off/gentle/strong (`arvos_apply_audio_preset`); sliders `head_focus`, `player_focus`, `object_focus` 0..1/0.05, `spotlight` −30..0/1, `spotlight_cone` 5..90/1, `widen` 1..2/0.05, `sfx_db` −12..6/0.5, `music_db` −24..6/0.5; `music` box/ring, `ring_radius` 0.5..2/0.05, `ring_rotate` 0..30/1, `ring_height` head/table, `ring_focus` 0..1/0.05; `spatializer` binaural/automatic/channel (note: "binaural: panning on this headset"); `show_sources`.
- **World** (sm64): `mario_cm` log slider 0.5..10 with `arvos_scale_readout`; `draw_all_rooms`; `hide_outside`; `other_rooms` Hidden/Frozen/Live; `billboards` Upright/Face the head; `cannon_visible`; `tree_style` Original card/Crossed cards (disabled when `arvos_trees_3d`).

- [ ] **Step 5: Run** host tests; on the headset: the menu button opens the panel above the level facing you; the ray and hover highlight follow the right controller; every control changes its setting live (shadows off darkens nothing, msaa 1 shows aliasing, a level pick warps, Re-place table moves the level, Quit exits); settings survive a restart; the panel closes with the menu button and the game pointer works again.
- [ ] **Step 6: Commit** — `git commit -m "quest: floating settings panel with the four tabs"`.

---

### Task 6: Bundled data inside the APK

**Files:**
- Create: `scripts/quest_bundle.py`, `apps/quest/bundled_copy.h/.cpp`, `runtime/tests/test_bundled_copy.cpp`
- Modify: `scripts/bundle_local.py` (write `bundled.json` into the staged dir: `{"files": {"rom.z64": "<sha256>", "packs/<name>/<file>": "<sha256>", ...}}` from the `tree_hashes` it already computes), `scripts/quest_build.py` (`--bundled DIR --res DIR` → assets dir), `scripts/quest_device.py` (remove `push_dev_data`), `apps/quest/main.cpp`, `scripts/tests/test_quest_scripts.py`

**Interfaces:**
- Produces: `quest_bundle.stage(out_assets: Path, packs: list[str] | None) -> dict` calling `bundle_local.stage(out_assets / "Bundled", names=packs)` and copying `build/_src/sm64/build/us_pc/{lang,dynos,palettes,mods}` to `out_assets / "res"`, writing `out_assets / "res" / "res.json"` listing every file (assets cannot be listed recursively on Android); `quest_build.py` passes `out_assets` as the APK's assets dir. In C++: `bool bundled_copy_needed(const std::string &asset_manifest_json, const std::string &installed_manifest_path)` (pure: true when the installed file is missing or differs byte for byte), `bool bundled_copy(AAssetManager *, const std::string &dest_dir, std::string *err)` (copies every file named in `Bundled/bundled.json` and `res/res.json` to `dest_dir`, writing into `dest_dir + ".tmp"` first and renaming into place last, so an interrupted copy is redone), `std::vector<std::string> bundled_manifest_files(const std::string &json)` (pure).

- [ ] **Step 1: Write tests**: Python: `test_quest_bundle_assets_layout` (stage into a temp dir with a fake `bundle_local.stage` and fake res dir → `Bundled/bundled.json` and `res/res.json` exist and list the files). C++: `bundled_manifest_files` parses a manifest with 3 files in order; `bundled_copy_needed` is true for a missing installed manifest and for a differing one, false for an identical one.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**; in `main.cpp` the copy runs before `arvos_configure`, logging `AR_BUNDLED copied=N ms=…` or `AR_BUNDLED up_to_date`; `bundle_dir` for the bridge is `internalDataPath + "/bundled"`.
- [ ] **Step 4: Run**: `python3 scripts/quest_build.py --packs render96-textures --launch`; the first launch logs `AR_BUNDLED copied=…` and the HD textures show; a second launch logs `up_to_date`; kill the app during the first copy (`adb shell am force-stop` within 2 s of launch) and relaunch: it copies again and the game boots. Remove `android:debuggable` from the manifest once Task 4's check is done.
- [ ] **Step 5: Commit** — `git commit -m "quest: ROM, packs and resources bundled as APK assets"`.

---

### Task 7: `./build.py` on Linux

**Files:**
- Modify: `scripts/guided_build.py`, `scripts/tests/test_guided_quest.py` (create), `README.md`

**Interfaces:**
- Produces: `guided_build.main` on Linux: target `quest` (the only one; `--target simulator|headset` are refused with "on Linux ./build.py builds for Meta Quest"); checks `common.tool_problems()` instead of Homebrew/Xcode; the ROM question and `import_rom` unchanged; the packs question unchanged, `prepare_packs` using the pack tool (plan 1); then `step("quest_build.py", "--config", "Release", "--packs", names, "--no-install" if a.no_install)` and `quest_device.install`/`launch`; the device question is replaced by `quest_device.find_device`, refusing with "connect one Quest with USB debugging allowed" when none or several; `local/build.json` keeps `{"target": "quest", "packs": …}`. `resolve()` takes a `platform` argument (default `sys.platform`) so it is testable.

- [ ] **Step 1: Write `test_guided_quest.py`**: `resolve(args(), {}, FakeAsker(answers=["all"]), platform="linux")` returns `target == "quest"`, `packs == {all known: OFFICIAL}` and never asks for a team; `resolve` with `--target headset` on linux raises `Stop`; `main` on linux with `tool_problems` returning `["magick"]` raises `Stop` mentioning `magick`.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**, keeping the macOS path byte-for-byte except for the `platform` parameter threading.
- [ ] **Step 4: Run** the Python tests; then from a clean `build/` directory: `direnv allow && ./build.py` asks for the ROM and the packs, builds everything, installs, and the headset shows the game with the chosen packs.
- [ ] **Step 5: README**: add a "Meta Quest 3S" section: requirements (Linux with Nix and direnv, Quest 3S in developer mode with USB debugging, the ROM), `direnv allow` then `./build.py`, the controller mapping table (left stick Mario, A jump, B/X punch, left trigger crouch/Z, Y start, grips drag the level, right stick turns it while gripping, menu button settings), Space Setup note, where settings and the log live (`adb logcat -s Diorama64`).
- [ ] **Step 6: Commit** — `git commit -m "build.py: Linux path builds and installs the Quest app"`.

---

### Task 8: Performance pass and Quest Look defaults

**Files:**
- Modify: `apps/quest/quest_app.cpp` (`qx_app_startup` seeding), `runtime/platform/quest/qx_session.cpp` (perf log), `README.md`

**Interfaces:**
- Produces: `AR_PERF` lines every 5 s with `gpu_ms` (`last_gpu_ms`), `cpu_ms`, `hz` (from `display_hz`), `late` (`late_passes`), `deferred` (`deferred_draws`); a documented set of Quest first-run Look values in `quest_app.cpp` (`kQuestLookSeed`), applied through `arvos_quest_seed_look` only for keys the player has never stored.

- [ ] **Step 1: Measure** with the defaults on Bob-omb Battlefield, Castle grounds and Jolly Roger Bay (water): record `gpu_ms` p50 and p95 over 60 s each, with the headset at 90 Hz. Budget: 11.1 ms at 90 Hz, 13.9 ms at 72 Hz.
- [ ] **Step 2: Decide**: if p95 fits 90 Hz, `kQuestLookSeed` is empty. Else try, in this order, each alone and measure again: `msaa 2`, `ao 0`, `shadows objects`, `reflections 0` (already off: no probe), `render_quality 0.8` (if the platform honours it: it does not in this port, skip). Choose the smallest set that fits 90 Hz at p95; if none does, keep `msaa 2` + `ao 0` and request 72 Hz (`Config.refresh_hz = 72`) and say so in the README.
- [ ] **Step 3: Implement** the seed and the perf line; write the measurements (table: level, settings, p50, p95, refresh) into `docs/superpowers/plans/2026-10-22-quest-perf.md`.
- [ ] **Step 4: Commit** — `git commit -m "quest: perf readout and first-run Look defaults from measurements"`.

---

### Task 9: The Mac check

**Files:**
- None new. On the Mac: `git log --oneline | grep '^.* shared:'` lists the commits to review.

- [ ] **Step 1**: On the Mac with Xcode 26: `./build.py --no-install` (headset target) and, if a Vision Pro is available, a run; also `python3 scripts/visionos_build.py` for the simulator and the macOS harness build (`cmake -S . -B build/macos -G Ninja && cmake --build build/macos`).
- [ ] **Step 2**: Fix anything the `shared:` commits broke, in commits prefixed `shared-fix:`; re-run the Linux ctest suite afterwards.
- [ ] **Step 3**: Note the result in `docs/superpowers/plans/2026-10-22-quest-perf.md` under "visionOS check" (commit hash checked, what was run, outcome).
- [ ] **Step 4: Commit.**

---

## Done when

- Headset: `./build.py` from a clean clone on the Linux machine ends with the game on a scanned table, played with the Touch controllers, stereo and spatial audio switchable live, the panel operating every setting of the four tabs with persistence, HD packs showing, a logged frame rate at the requested refresh with the seeded defaults, and no crash across menu, doff/don, permission denial, scene re-scan and app restart.
- Host: every ctest target and every Python test passes.
- The Mac check is recorded and the `shared:` commits build on macOS and visionOS.

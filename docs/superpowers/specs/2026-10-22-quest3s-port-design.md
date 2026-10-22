# Diorama64 on Meta Quest 3S: design

Date: 2026-10-22. Status: draft for review.

## Intent

Diorama64 plays Super Mario 64 as an augmented-reality diorama: the level stands on the real table as a small model
you walk around while you play with a controller. Today it runs only on Apple Vision Pro, built with Xcode on a Mac.
This port brings the same experience to the Meta Quest 3S, built from a Linux machine with Nix.

The shared runtime was designed with this in mind. Its platform interface is modelled on OpenXR (`wait_frame`,
`begin_frame`, `end_frame`, views as tracking-from-eye poses with field-of-view tangents, poses in metres, +Y up,
-Z forward, quaternion xyzw), and the renderer talks to an abstract `ar::IRenderDevice`. The port fills in Quest
backends behind those two interfaces and adds an Android app shell. It does not restructure the shared code.

## Goals

1. The same experience as the visionOS app: the level on the real table in passthrough, walk around it, play it.
2. Play with the Quest Touch controllers.
3. The table comes from the headset's room scan; the player can nudge and rotate the level on it, and the adjustment
   is remembered.
4. A floating settings panel in the scene with the same four tabs as the visionOS window: Level, World, Look, Audio.
5. Stereo audio and spatialised per-source audio.
6. HD texture packs and 3D model packs, prepared on Linux.
7. Everything builds on Linux (NixOS) from a `flake.nix` and a `.envrc` for direnv, and installs on the headset over
   adb from the same build script flow the Mac uses.
8. The visionOS build keeps working. Changes to shared code stay compatible with it. This is a mid-priority goal:
   the Quest result comes first, and the Apple check is done once on a Mac before merge, not after every step.

## Non-goals

- Hand tracking or pinch input. Controllers only.
- Bluetooth gamepad support on Quest. Can be added later through the same `ar_pad` path.
- Room-matched lighting and reflections from an environment probe. Quest has no equivalent of ARKit's environment
  probe; `get_environment` stays null and the renderer's probe-dependent features stay off.
- A Gradle or Android Studio project. Packaging is done with the SDK command-line tools.
- Multiplayer, mods or Lua scripts (the game boots with mods disabled, as today).
- Store or App Lab release packaging, signing with a release key, or Meta's VRC requirements.
- Any other Android headset. Quest 3 will very likely work as a side effect, but is not tested or required.

## Anti-goals (what would make this a failure even if it technically works)

- The visionOS build breaks, or shared code grows `#ifdef` forests that make it harder to maintain.
- The level floats above or sinks into the table because placement guesses the height instead of using the scan.
- The build needs tools outside the flake (a manual SDK install, Android Studio, a Mac in the loop) for anything the
  Mac flow does today, including packs.
- Frame pacing stutters: the game must keep its 30 Hz tick with render-thread interpolation, as today.

## Constraints

- Target hardware: Meta Quest 3S (Snapdragon XR2 Gen 2, 1832x1920 per eye, 72/90/120 Hz). Horizon OS v69 or newer.
- Development host: NixOS, Nix 2.34, direnv. No adb, Java or Android SDK outside the flake. A Mac with Xcode is
  available for the one visionOS check before merge; it is not part of the day-to-day loop.
- No test suite ships in this tree (the goldens are private to the upstream author), so nothing existing can be run
  to prove shared code still behaves; the fake platform and cube adapter exist but have no tests around them here.
- The ROM is the user's own; it and the packs go inside the app, never into the repo.
- The headset must be in developer mode with USB debugging accepted (user does this once in the Meta Horizon app).
- License: AGPL-3.0-only, as the rest of the project. New dependencies must be compatible (OpenXR loader, glslang,
  SPIRV-Tools, android_native_app_glue are all Apache-2.0 or BSD).

## Current state (what exists, from the code survey)

Shared, portable, reused as-is (about 17.5k lines):

- `runtime/include`, `runtime/core`, `runtime/capture`: the runtime, game thread, snapshot ring, texture FIFO, audio
  ring and settings. Two spots need a non-Apple fallback: `mem_stats.cpp` (mach `task_info`) and `fc_builder.cpp`
  (`dispatch_apply_f` for parallel normals).
- `runtime/render/common`: the portable renderer (`Renderer`, `ar_layer`, combiner decode, lighting, shadows, AO
  scheduling, pipeline warm-up, `look.cpp` settings).
- `runtime/platform/visionos` portable `.cpp` files, built as the `ar_vos_logic` library: placement record (JSON),
  plane picking and table candidate rules, place gesture and reticle, input queue, options, state, resampler and
  polyphase filter, scene audio, latency, perf, foveation planning, safe mode, logging. Despite the directory name
  these are plain C++ and the Quest platform links them unchanged.
- `games/sm64`: the sm64coopdx submodule, 14 patches, the adapter (portable C), SDL stub, Lua shim.

Apple-specific, needs a Quest counterpart (about 8.3k lines):

- `runtime/render/metal/metal_device.mm` (2.0k) implementing `IRenderDevice`, and `msl_emitter.cpp` (0.5k)
  generating one Metal vertex+fragment program per N64 combiner key at runtime, plus six fixed shaders embedded as
  strings: composite, gamma-correct mip generation (compute), environment cube reduce (compute), colour+depth resolve
  (2D and array variants), shadow/underside depth with cutout, AO fill (compute into a 3D texture).
- `runtime/platform/visionos/*.mm`: ARKit sessions (world tracking, plane detection, hand tracking, environment
  light), CompositorServices frame loop and drawables, world anchors (`vos_placer.mm`), AVAudioSession + RemoteIO
  output (`vos_audio.mm`), PHASE spatial audio (`vos_phase.mm`).
- `runtime/platform/apple/gc_pad.mm`: GameController framework → `ar_pad`.
- `apps/visionos`: SwiftUI app with the four settings tabs, `vos_api.h` C bridge (about 90 functions), `vos_app.mm`.
- `apps/macos_harness`: desktop harness. `packs.py` uses it headlessly (`--generate-tex`, `--generate-actors`) to
  convert packs; the conversion itself is DynOS's generator inside the patched sm64 library and draws nothing.
- Build: `cmake/Toolchain.cmake` requires Xcode 26; `scripts/*.py` assume Homebrew paths, `sips`, `bsdtar`,
  `xcrun devicectl`, `nm`, `otool`.

## Architecture of the port

### Layout (decided)

```
apps/quest/                      Android app shell: NativeActivity entry, manifest, packaging, panel UI
runtime/platform/quest/          OpenXR platform (ar_platform), Touch controllers, AAudio output, spatial audio
runtime/render/vulkan/           vulkan_device.{h,cpp} implementing IRenderDevice; glsl_emitter.cpp
apps/pack_tool/                  headless Linux CLI that runs DynOS's pack generators (replaces the harness for packs)
flake.nix, .envrc                toolchain
scripts/quest_build.py, quest_device.py, quest_run.py   mirrors of the visionos_* scripts
```

The portable visionOS logic stays where it is and keeps its `vos_` names. Renaming it to a common directory is a
pure move that would touch the visionOS build for no functional gain; it can happen later in its own change.

### Build and toolchain (decided)

- `flake.nix` provides a dev shell with: the Android SDK and NDK from nixpkgs `androidenv` (platform-tools for adb,
  build-tools for aapt2/zipalign/apksigner, one platform API level at 32 or higher, an NDK r26 or newer), a JDK
  (needed only by apksigner), CMake, Ninja, GNU make, clang, SDL2 and GLEW (for the oracle build), Python 3,
  ImageMagick (replaces `sips`), bsdtar (libarchive), and the host-side tools the scripts call. Android SDK
  licence acceptance is configured in the flake. `.envrc` is `use flake`.
- The Android build is CMake + Ninja with the NDK's `android.toolchain.cmake`, `ANDROID_ABI=arm64-v8a`,
  `ANDROID_PLATFORM` matching the chosen API level, C++17 as the rest of the project. A new top-level option
  `SM64AR_QUEST` selects it, parallel to `SM64AR_VISIONOS`. `cmake/Toolchain.cmake` must no longer fail on
  non-Apple hosts; the Xcode checks move under the Apple branch.
- Packaging is scripted, not Gradle: compile nothing in Java (the app is a `NativeActivity` with `hasCode="false"`),
  `aapt2` links the manifest and assets, the `.so` goes under `lib/arm64-v8a/`, `zipalign`, then `apksigner` with a
  debug key generated into `build/quest/` on first use. Install with `adb install -r`, launch with `adb shell am
  start`, logs via `adb logcat` filtered to the app tag.
- `build.py` / `guided_build.py` gain a Quest path: on Linux it asks for the ROM and packs as today, skips team and
  headset questions, checks that one Quest is visible to adb, builds, bundles and installs. Reconfigure flags work
  the same.
- `sm64_oracle.py` runs upstream's Makefile on Linux with gmake and the Nix-provided SDL2/GLEW (it already runs on
  macOS this way). `sm64_prepare.py`, `fetch_lua.py`, `bundle_local.py` are host-agnostic and stay.
- The sm64 library target for Quest mirrors the visionOS variant: drops `audio_sdl`, `controller_sdl`,
  `update_checker`, `crash_handler` (same link stubs), keeps `MA_NO_*` defines so miniaudio stays out, links the SDL
  stub and zlib from the NDK. `OSX_BUILD` is not defined. If upstream code needs a `TARGET_ANDROID`-style define,
  it is added in a patch, not with sed.
- Dependencies that are source, not SDK: the Khronos OpenXR loader (OpenXR-SDK), glslang and SPIRV-Tools, and
  android_native_app_glue (from the NDK). They are pinned as flake inputs and built inside the app's CMake with the
  NDK toolchain, so nothing is fetched at build time. The Khronos loader is the only loader (my call: Meta's own
  loader is proprietary and bundling it into an AGPL app is a licence problem). The builder verifies in the first
  milestone that it finds the Meta runtime on the 3S (`XR_KHR_loader_init_android` with the activity's JavaVM and
  context); if it does not, that is a blocker to raise, not a reason to swap loaders.

### OpenXR platform layer (decided)

`runtime/platform/quest/qx_platform.cpp` implements `ar_platform` (ABI v4):

- Instance with extensions: `XR_KHR_vulkan_enable2`, `XR_FB_passthrough`, `XR_FB_display_refresh_rate`,
  `XR_FB_scene`, `XR_FB_spatial_entity`, `XR_FB_spatial_entity_query`, `XR_FB_spatial_entity_storage`,
  `XR_FB_scene_capture`, `XR_FB_composition_layer_alpha_blend` (if present), `XR_KHR_composition_layer_depth`
  (if present), `XR_KHR_android_create_instance`, `XR_KHR_loader_init_android`.
- Session on the Vulkan device created through `xrCreateVulkanDeviceKHR` so the runtime picks queues and device
  extensions. Reference space: `STAGE` if available, else `LOCAL`; this is the runtime's "tracking" space.
- `wait_frame`: `xrWaitFrame` + `xrLocateViews` fill `ar_frame_info` (two views, tracking-from-eye, fov tangents,
  swapchain size, display time, refresh rate from `XR_FB_display_refresh_rate`). Request 90 Hz at session start;
  stay at 72 if refused.
- `begin_frame`/`end_frame`: `xrBeginFrame`, acquire/wait swapchain images, and after encoding `xrEndFrame` with two
  layers: the passthrough layer underneath, the projection layer on top with source-alpha blending (unpremultiplied
  or premultiplied, whichever the renderer's composite output is; the builder checks the composite shader).
  Depth layer info is attached when `XR_KHR_composition_layer_depth` is present, for better reprojection.
- `set_passthrough`: starts/pauses the passthrough feature and layer.
- `session_state` maps `XrSessionState` onto `ar_session_state` (SYNCHRONIZED/VISIBLE → VISIBLE, FOCUSED → FOCUSED,
  STOPPING/LOSS_PENDING/EXITING → STOPPING/LOST).
- `get_environment` and `share_anchor` are null. `persist_anchor` is null too (see placement: the table anchor is
  the scan's own persistent anchor, so nothing of ours needs persisting).
- Swapchains: one colour swapchain with `arraySize=2` (layered stereo, matching the Metal backend's convention)
  and, when depth submission is used, one depth swapchain. Format `VK_FORMAT_R8G8B8A8_SRGB` or the runtime's
  preferred sRGB format, picked from `xrEnumerateSwapchainFormats`.
- Android lifecycle (`android_native_app_glue` commands) drives session begin/end; `arvos_prepare_quit`-style
  shutdown waits for the game thread, as `vos_app.mm` does.

### Table placement (decided)

- On start, after the `com.oculus.permission.USE_SCENE` runtime permission is granted, query spatial entities with
  the semantic label `TABLE` through `XR_FB_scene` + `XR_FB_spatial_entity_query`. For each: locate the anchor,
  derive `ar_table{tracking_from_table, half_extent}`. `half_extent` is reported as 0 (unknown), as the
  visionOS platform does for real tables (my call: the struct is symmetric about the level origin, which after a
  nudge is not the table centre; the 2D bounding box is still read, for the candidate rules and the drag limits). The table's +Y is up and its
  origin is the plane centre (the Scene API gives a plane whose normal is the anchor's -Z or +Z; the platform
  rotates it into the runtime's +Y-up convention). Candidate rules reuse `vos_plane_is_table_candidate` (at least
  0.4 m above the floor; the floor anchor comes from the same query with label `FLOOR`).
- Choose the table closest to the head on first run and remember its anchor UUID. Save a `PlacementRecord` (the
  existing JSON type) holding that UUID plus the player's offset and yaw on the surface, in the app's files
  directory. On later runs, re-locate that UUID; if it is gone (space re-scanned), fall back to the nearest table
  and reset the offset.
- Adjusting: hold the grip on either controller to drag the level along the table plane (offset follows the
  controller's aim ray intersection with the plane), the right thumbstick's X rotates it about the table's up axis
  while gripping. Release saves. This is the Quest form of the visionOS place gesture; it drives `get_preview` so
  the existing reticle/outline preview draws during the drag. The builder may reuse `PlaceGesture` if it fits the
  ray-on-plane model, or write a small `qx_place.cpp` with the same outputs.
- "Re-place table" from the panel re-runs the nearest-table choice (so a room with several scanned tables can
  switch by walking up to another one and pressing it, matching what the visionOS button does) and resets the
  offset to that table's centre. If the query returns no table, the app shows
  a message on the panel ("No table in your room scan. Run Space Setup, then come back.") and offers a button that
  calls `xrRequestSceneCaptureFB`, which opens the system's scan flow. The app never invents a table height.
- No manual (ray-only) placement mode.

### Input (decided)

- Interaction profile `/interaction_profiles/oculus/touch_controller`, actions bound for: left thumbstick (Mario
  stick), right thumbstick (camera C-buttons: left/right/up/down from the stick's quadrant, with the same deadzone
  and masking the adapter already applies to the pad's right stick), A → A, B → B, X → Z-trigger alternative,
  left trigger → Z, right trigger → B (punch) alternative, left grip → placement drag, right grip → placement drag,
  Y → start, menu button → toggle the settings panel, right thumbstick click → recentre camera (if the adapter
  exposes it; otherwise unbound). The builder confirms the exact `ar_pad` button bit assignments from
  `ar_types.h`/`pad_map.c` and documents the mapping in a table in the panel's help tab or the README.
- `get_pad` is called from the game thread at 30 Hz; the platform syncs actions once per `wait_frame` on the render
  thread and hands the game thread the last synced state under a mutex, matching how `VosPads` caches the gamepad.
- Haptics: none in v1.

### Renderer (decided)

`runtime/render/vulkan/vulkan_device.cpp` implements every `IRenderDevice` method, keeping the Metal backend's
conventions so the shared renderer sees no difference: reverse-Z with an infinite far plane, layered stereo targets
(array textures; the shared renderer calls `encode_pass` once per eye with `ViewConstants.layer`, and the vertex
shader writes `gl_Layer` exactly as the Metal emitter writes `render_target_array_index`; no `VK_KHR_multiview` in
v1, because true multiview would change the renderer contract), MSAA with sample counts reported
by `supported_samples`, lazily allocated (transient) MSAA attachments standing in for Metal's memoryless textures.

- Shaders: `glsl_emitter.cpp` replaces `msl_emitter.cpp`'s output with GLSL 4.50 for Vulkan, sharing the
  `CombinerIR` input and the same prelude features (combiner, fog, texture edge, noise, room lighting, shadow/AO
  sampling, cutaway, clip, silhouettes, render interpolation). The six fixed shaders are rewritten in GLSL (compute
  stays compute). Compilation happens on the headset at runtime through glslang + SPIRV-Tools built into the app,
  with a `VkPipelineCache` persisted to the files directory so second launches skip compile time. The existing
  `warm_pipelines*` path drives compilation during the loading screen. The emitter must leave the Metal emitter
  untouched; shared code between the two emitters (prelude text that is identical) may be hoisted into a common
  file only if it stays byte-identical for Metal.
- Resolve: `encode_resolve(src_target, ExternalView)` resolves into the OpenXR swapchain image (colour) and, when
  depth submission is on, the depth swapchain image. `ExternalView` carries `VkImage` handles as its opaque
  pointers.
- Foveation (`set_rate_map`): a no-op in v1. Fixed foveated rendering via `XR_FB_foveation` can follow once the
  target layout is settled, because it needs `VK_EXT_fragment_density_map` on the app's own targets.
- `read_target`/`read_target_depth`: implemented (used by tests and probes), through a host-visible staging buffer.
- `memoryless_msaa` returns true when the device supports lazily allocated memory, which Adreno does.
- Environment reduce and `environment_stats` are implemented as no-ops that report nothing (no probe on Quest).
- Target frame rate: 90 Hz. The builder measures on device with the default Look settings and reports the GPU time
  per frame; if 90 Hz cannot be held with MSAA 4x, shadows and AO on, the Quest app shell seeds reduced Look values into the
  settings file on first run (my call: this keeps `look.cpp` and its defaults-revision logic unchanged; the player
  can still change and persist them afterwards) rather than dropping to 72 Hz by default.

### Audio (decided)

- Output: AAudio, low-latency performance mode, 48 kHz, stereo float or int16, callback-driven. The callback pulls
  32 kHz int16 from `ar_runtime_pull_audio` (stereo mode) and resamples with the existing `vos_resample` /
  `vos_polyphase`, exactly as `vos_audio.mm` does. Audio pauses with the session.
- Spatial mode: the runtime's "sources" mode (up to 32 mono lanes with per-tick positions) feeds a new
  `qx_spatial.cpp` that places each lane relative to the head pose using the portable `vos_scene_audio` /
  `vos_scene_pos` logic for source positions and smoothing, then mixes with per-source stereo panning (constant-power
  pan from the source's azimuth) and distance attenuation, into the same AAudio stream. No HRTF and no Meta XR
  Audio SDK in v1; the mixer is behind a small interface so an HRTF or the Meta SDK can replace it later. The Audio
  tab's existing keys (stereo vs sources mode, reverb, resampler, presets) apply as today; keys that only PHASE
  honoured are shown but documented as having no effect on Quest.

### Settings panel (decided)

- A floating quad in the scene, anchored with the existing `ar_float_panel_anchor` logic (above the level, or
  beside it toward the head when the level is tall), toggled with the menu button, and facing the head.
- Content: the same four tabs with the same controls as `SM64ARApp.swift`, driven through the same C bridge
  (`vos_api.h` functions, which `apps/quest` links from a renamed-neutral copy of `vos_app.mm`'s logic or directly
  from the C++ classes; the builder picks whichever keeps `vos_app.mm` untouched). Plus one Quest-only control:
  "Re-place table" (reset offset), and the "No table" message described above. Values persist in a JSON file in
  the app's files directory (replacing `UserDefaults`), through the same key names so settings have the same
  meaning across platforms.
- Drawn by the app, not the game: a CPU-rasterised RGBA texture (text via a small embedded bitmap font or
  `stb_truetype` with a bundled open-licence font) uploaded through `upload_texture` and drawn as two triangles with
  a dedicated pipeline in the Vulkan device, after the level, with alpha. The pointer is the right controller's aim
  ray; the ray-quad intersection gives the hovered control; the trigger activates it; the thumbstick adjusts
  sliders. A thin ray line is drawn from the controller to the panel while the panel is open.
- The HUD (lives, stars, coins) continues to come from the game through the existing HUD path.

### ROM and packs (decided)

- Bundled inside the APK as assets, mirroring "your ROM and the packs go inside the app" on visionOS:
  `bundle_local.py`'s staging folder becomes the APK's `assets/Bundled/`. On launch, the app copies assets to
  its files directory when they are missing or their size/hash differs (the sm64 library and DynOS read files by
  path), then boots from there. Nothing is read from external storage, no storage permission is needed.
- `apps/pack_tool`: a headless Linux executable that links `sm64_adapter` + `sm64_game` + `ar_core`, takes
  `--rom`, `--generate-tex IN OUT` and `--generate-actors DIR`, and calls `sm64_host_generate_tex` /
  `sm64_host_generate_actors` after `ar_adapter_sm64()->init` into a temporary save, like `run_generate` in the
  harness. No renderer, no window. `packs.py` chooses the harness on macOS and the pack tool on Linux; `sips` calls
  are replaced by an `_resize_png` helper that uses ImageMagick on Linux and `sips` on macOS, with identical
  output constraints (format png, max dimension). `bsdtar` is found on PATH rather than at `/usr/bin/bsdtar`.
- Expected-file checks and verification stay as they are.

### App shell (decided)

- `apps/quest/main.cpp`: `android_main` with `android_native_app_glue`; initialises the OpenXR loader for Android,
  requests the scene permission through JNI (`Activity.requestPermissions`), copies assets, configures the runtime
  the way `arvos_configure` does, runs the render loop the way `arvos_layer_run` does, handles pause/resume/destroy.
- `AndroidManifest.xml`: `NativeActivity`, `android:hasCode="false"`, `com.oculus.intent.category.VR` category,
  `uses-feature` for passthrough (`com.oculus.feature.PASSTHROUGH`) and `android.hardware.vr.headtracking`,
  `uses-permission` `com.oculus.permission.USE_SCENE` and `com.oculus.permission.USE_ANCHOR_API`,
  `com.oculus.supportedDevices` listing quest3 and quest3s (the builder checks Meta's current manifest reference for
  the exact string), `minSdkVersion`/`targetSdkVersion` 32 or whatever Meta currently requires.
- Logging goes to logcat with tag `Diorama64`, through the existing `vos_log` by giving it an Android sink.
- App id `org.pagetable.diorama64` (the builder may change it if it collides with anything; it is only a local
  debug install).

## What's left to the builder

- Exact Vulkan target/attachment layout, descriptor strategy, and whether the AO 3D texture and mip generation use
  compute or graphics passes, as long as the `IRenderDevice` contract holds.
- Whether to reuse `PlaceGesture` or write `qx_place.cpp`.
- How much of `vos_app.mm` / `vos_session.cpp` can be reused versus copied; `vos_session.cpp` calls
  `create_metal_device` directly, so it needs a small factory seam. That seam must keep the visionOS build
  behaviour identical.
- The bitmap font and panel layout details.
- Which nixpkgs revision and NDK version to pin, and how the flake handles the Android SDK licence.
- Test strategy details. Nothing exists to reuse, so the builder decides how much to build: at minimum, unit tests
  for new portable code (GLSL emitter output, placement record, spatial mixer, controller-to-pad mapping) and an
  on-device smoke path using `read_target`; a host-side lavapipe build of the Vulkan device driven by the fake
  platform and cube adapter if it comes cheaply.
- Shared-file discipline (follows from goal 8 being mid-priority and unverifiable day to day): changes to files the
  visionOS build compiles are kept additive and minimal, land in their own commits with a note of what the Mac check
  must cover, and are done together where possible so the Mac check happens once.
- The order of work. A suggested first milestone: the cube adapter rendering on the headset in passthrough with
  head tracking, before any sm64 code is linked.

## Verification

The port is done when, from a clean clone on the Linux host:

1. `direnv allow` then `./build.py` asks for ROM and packs, builds, and installs on a connected Quest 3S.
2. On the headset, the level appears on a scanned table, in passthrough, at the right height, and the Touch
   controllers play the game. Restarting the app keeps the nudged placement.
3. The settings panel shows the four tabs and every setting applies live and persists.
4. Stereo and spatial audio both work and switching between them works live.
5. An HD texture pack and a model pack prepared on Linux load and show.
6. The visionOS build still builds and runs from the same commit, checked once on the Mac before merge. If that
   check fails, fixing it is in scope, but it does not block Quest work before that point.
7. Frame pacing at the requested refresh rate holds with the Quest default Look settings, measured with the existing
   perf readout.

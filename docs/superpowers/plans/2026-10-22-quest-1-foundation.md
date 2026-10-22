# Quest 3S Port, Plan 1 of 3: Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** From a Nix dev shell on Linux, build and install a native Android app that shows the cube test adapter standing on a fixed table in passthrough on a Meta Quest 3S with head tracking, plus a Linux pack tool that converts HD packs without a Mac.

**Architecture:** The shared runtime (`ar_runtime`, `Renderer`, `draw_ar_layer`) stays untouched. New code fills the two existing seams: an OpenXR implementation of `ar_platform` (`runtime/platform/quest`) and a Vulkan implementation of `ar::IRenderDevice` (`runtime/render/vulkan`) whose shaders come from a new GLSL emitter compiled on the headset with glslang. A `NativeActivity` shell (`apps/quest`) wires them together, and Python scripts package the APK without Gradle. The shared CMake grows a Linux host configuration with a small ctest suite, so the Vulkan device and emitter are tested on the host under lavapipe before they run on the headset.

**Tech Stack:** C++17, CMake 3.28+ with Ninja, Android NDK r29 (`android.toolchain.cmake`), Khronos OpenXR-SDK loader, Vulkan 1.1, glslang, android_native_app_glue, Python 3 (stdlib only), Nix flakes + direnv, ImageMagick, libarchive's bsdtar.

**Spec:** `docs/superpowers/specs/2026-10-22-quest3s-port-design.md` (read it first; this plan covers its build/toolchain, OpenXR platform (without Scene placement and input), renderer (cube subset), app shell and pack tool sections. Plan 2 adds the sm64 game, the full renderer, Touch controllers and table placement; plan 3 adds audio, the settings panel, bundling and the guided build.)

## Global Constraints

- Shared files the visionOS build compiles are changed additively and minimally; each such change lands in its own commit whose message starts with `shared:` so the one Mac check before merge can find them. Never edit `apps/visionos`, `runtime/platform/visionos/*.mm`, `runtime/render/metal`.
- Languages: C11 and C++17 (the project's `CMAKE_C_STANDARD 11`, `CMAKE_CXX_STANDARD 17`). No exceptions-based control flow in new C++ (the project does not use them).
- ABI: `AR_PLATFORM_ABI_VERSION 4`, `AR_ADAPTER_ABI_VERSION 5`, `FC_ABI_VERSION 5`; do not bump.
- Pose convention: metres, right-handed, +Y up, -Z forward, quaternion xyzw (`ar_types.h`), identical to OpenXR's `XrPosef`.
- Projection: reverse-Z with an infinite far plane, `near_m` from `ar_view` (0 means `AR_NEAR_M` = 0.05). Depth clear value 0, compare GREATER_OR_EQUAL.
- Colour: the renderer writes premultiplied alpha; `ExternalView::linearize` true means the destination is an sRGB format and the resolve writes linear values.
- Dependencies may only be: nixpkgs packages, the Khronos OpenXR-SDK, glslang, android_native_app_glue (NDK), Vulkan headers/loader (NDK on Android, nixpkgs on host). No Gradle, no Meta SDKs, no proprietary loader.
- Android: package `org.pagetable.diorama64`, `minSdkVersion` 32, `targetSdkVersion` 32, ABI `arm64-v8a` only, `NativeActivity` with `android:hasCode="false"`, logcat tag `Diorama64`.
- No ROM, pack or anything derived from them is ever written under the repo except `local/` and `build/` (both gitignored).
- Tests: C++ tests are plain executables registered with `add_test` (exit 0 pass, 1 fail, 77 skip); Python tests use `unittest` under `scripts/tests/`. No test frameworks added.
- Licence: all new files are AGPL-3.0-only like the rest of the tree (no header needed; LICENSE covers the tree).

## Review Focus

Inputs the spec implies but which only these added tests cover; each is pinned to a task below.

1. The player opens the Quest system menu or takes the headset off: the session goes VISIBLE/IDLE, the game pauses, and play resumes afterwards without a restart. (Task 5: `qx_session_state` mapping test; Task 7: on-device doff/don check.)
2. Head tracking is lost (view state flags without `POSITION_VALID`): `head_tracked` is 0 and the last good pose is kept, never a zero pose. (Task 5: `test_qx_convert` tracking-lost case.)
3. The runtime offers no `VK_FORMAT_R8G8B8A8_SRGB` swapchain format: the next sRGB format is taken, else a UNORM one with `linearize` off, never a crash or a black view. (Task 5: `qx_pick_swapchain_format` test.)
4. A generated shader fails to compile on the device: `compile_combiner` returns false with glslang's message, `take_pipeline_error` reports it, the draw is skipped, the app keeps running. (Task 6: emitter and device tests.)
5. The pack tool is given a missing or wrong ROM, or a PNG directory with no matching textures: exit code 1 with an `error:` line on stderr and no partial output left in the destination. (Task 8: pack tool test.)

---

## File structure

Created:

- `flake.nix`, `flake.lock`, `.envrc`: toolchain.
- `runtime/tests/check.h`: `CHECK(cond)` and `SKIP()` macros for the test executables.
- `runtime/tests/CMakeLists.txt`, `runtime/tests/test_runtime_cube.cpp`, `test_glsl_emitter.cpp`, `test_vulkan_cube.cpp`, `test_qx_convert.cpp`, `vk_host_context.{h,cpp}`.
- `runtime/render/vulkan/vulkan_device.h`, `vulkan_device.cpp`, `vk_layout.h`, `glsl_emitter.h`, `glsl_emitter.cpp`, `glsl_compiler.h`, `glsl_compiler.cpp`.
- `runtime/platform/quest/qx_convert.h`, `qx_convert.cpp` (pure helpers), `qx_platform.h`, `qx_platform.cpp` (OpenXR session, `ar_platform`), `qx_session.h`, `qx_session.cpp` (frame loop).
- `apps/quest/CMakeLists.txt`, `apps/quest/main.cpp`, `apps/quest/AndroidManifest.xml`.
- `apps/pack_tool/CMakeLists.txt`, `apps/pack_tool/main.cpp`.
- `scripts/quest_build.py`, `scripts/quest_package.py`, `scripts/quest_device.py`, `scripts/tests/test_quest_scripts.py`, `scripts/tests/test_packs.py`.

Modified (shared, additive):

- `CMakeLists.txt` (languages and app selection per platform), `cmake/Toolchain.cmake` (Apple checks under `if(APPLE)`), `runtime/CMakeLists.txt` (Apple-only targets under `if(APPLE)`, new targets), `games/sm64/CMakeLists.txt` (a non-Apple embedded branch), `runtime/platform/visionos/vos_log.cpp` (logcat sink under `__ANDROID__`), `.gitignore`, `scripts/sm64_oracle.py`, `scripts/sm64ar_common.py`, `scripts/packs.py`.

---

### Task 1: Nix dev shell and direnv

**Files:**
- Create: `flake.nix`, `.envrc`
- Modify: `.gitignore`

**Interfaces:**
- Produces: a dev shell exporting `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT`, `ANDROID_BUILD_TOOLS` (directory holding `aapt2`, `zipalign`, `apksigner`), `ANDROID_JAR` (the platform's `android.jar`), `JAVA_HOME`, `SM64AR_OPENXR_DIR` (OpenXR-SDK source tree), `SM64AR_GLSLANG_DIR` (glslang source tree), `VK_DRIVER_FILES` (lavapipe's ICD JSON). Later tasks read exactly these names.

- [ ] **Step 1: Write `flake.nix`**

Inputs: `nixpkgs` (`github:NixOS/nixpkgs/nixos-unstable`), `openxr-sdk` (`github:KhronosGroup/OpenXR-SDK`, `flake = false`, pinned to the newest `release-1.1.*` tag), `glslang` (`github:KhronosGroup/glslang`, `flake = false`, pinned to the newest `15.*` tag). One output `devShells.x86_64-linux.default` built with `pkgs.mkShell.override { stdenv = pkgs.clangStdenv; }`. Import nixpkgs with `config.allowUnfree = true; config.android_sdk.accept_license = true;`. Android packages from `pkgs.androidenv.composeAndroidPackages { includeNDK = true; platformVersions = [ "34" ]; buildToolsVersions = [ "34.0.0" ]; includeEmulator = false; includeSystemImages = false; }`. Shell packages: `cmake ninja gnumake pkg-config python3 jdk17 imagemagick libarchive SDL2 glew zlib vulkan-headers vulkan-loader vulkan-validation-layers mesa git` plus the Android composition's `androidsdk`. `shellHook` exports the variables listed in Interfaces; `ANDROID_NDK_ROOT` is the NDK directory inside the SDK (`$ANDROID_SDK_ROOT/ndk-bundle` or `$ANDROID_SDK_ROOT/ndk/<version>`, whichever exists); `VK_DRIVER_FILES` is the `lvp_icd.x86_64.json` under the `mesa` package's `share/vulkan/icd.d`.

- [ ] **Step 2: Write `.envrc`**

```
watch_file flake.nix flake.lock
use flake
```

- [ ] **Step 3: Add `.direnv/` and `/result` to `.gitignore`** (under the "Build output" group).

- [ ] **Step 4: Verify the shell**

Run: `nix develop -c sh -c 'cmake --version | head -1; ninja --version; adb version | head -1; "$ANDROID_BUILD_TOOLS/aapt2" version; ls "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" "$ANDROID_JAR" "$SM64AR_OPENXR_DIR/src/loader" "$SM64AR_GLSLANG_DIR/glslang" "$VK_DRIVER_FILES"; magick -version | head -1; bsdtar --version; java -version 2>&1 | head -1; vulkaninfo --summary 2>/dev/null | grep -m1 -i "deviceName"'`
Expected: every command prints a version or path, no "No such file"; `vulkaninfo` names `llvmpipe`.

- [ ] **Step 5: Commit**

```bash
git add flake.nix flake.lock .envrc .gitignore
git commit -m "quest: nix dev shell with Android NDK, OpenXR-SDK, glslang and lavapipe"
```

---

### Task 2: Shared CMake builds on a Linux host, with a ctest suite

**Files:**
- Modify: `CMakeLists.txt`, `cmake/Toolchain.cmake`, `runtime/CMakeLists.txt`
- Create: `runtime/tests/CMakeLists.txt`, `runtime/tests/check.h`, `runtime/tests/test_runtime_cube.cpp`

**Interfaces:**
- Produces: CMake options `SM64AR_SM64` (default ON; OFF skips `games/sm64`), `SM64AR_HOST_TESTS` (default ON when `NOT APPLE AND NOT ANDROID`), `SM64AR_QUEST` (set ON automatically when `ANDROID`). Targets `ar_core`, `ar_render_common`, `ar_vos_logic`, `ar_platform_fake`, `ar_cube_adapter` build on Linux unchanged. `runtime/tests/check.h` provides `CHECK(cond)` (prints `FAIL file:line cond` and `exit(1)`) and `SKIP(msg)` (prints and `exit(77)`), and `int main` is written by each test.

- [ ] **Step 1: Make the top-level CMake platform-neutral**

In `CMakeLists.txt`: `project(sm64_ar C CXX)`; after it, `if(APPLE) enable_language(OBJC OBJCXX) set(CMAKE_OBJCXX_STANDARD 17) endif()`. Add the options from Interfaces. `if(ANDROID) set(SM64AR_QUEST ON) endif()`. Replace the app selection with: visionOS → `apps/visionos` (with `enable_language(Swift)` as today); `SM64AR_QUEST` → `apps/quest`; `APPLE` → `apps/macos_harness`; otherwise nothing. `add_subdirectory(games/sm64)` only `if(SM64AR_SM64)`. `add_subdirectory(runtime/tests)` `if(SM64AR_HOST_TESTS)` after the others. `enable_testing()` at top level.

- [ ] **Step 2: Guard `cmake/Toolchain.cmake`**

Wrap the whole file in `if(APPLE) ... else() set(SM64AR_VISIONOS OFF) endif()`. No other change.

- [ ] **Step 3: Guard the Apple-only targets in `runtime/CMakeLists.txt`**

Under `if(APPLE)`: `ar_render_metal`, `ar_png`, `ar_gc_pad`, `ar_harness`, and the existing `ar_vos` block stays under `SM64AR_VISIONOS`. Everything else (`ar_capture`, `ar_core`, `ar_render_common`, `ar_platform_fake`, `ar_cube_adapter`, `ar_vos_logic`) stays as it is. On non-Apple, `ar_core` links `Threads::Threads` (`find_package(Threads REQUIRED)`).

- [ ] **Step 4: Write the first test**

`runtime/tests/test_runtime_cube.cpp`: `fake_platform_init(&fp, 90.0)`, `fp.stop_after = 10`; `ar_runtime_create(cube_adapter(), &fp.api)`; `ar_runtime_init_game(rt, "", "")`; loop `ar_runtime_begin_frame` / `ar_runtime_step_game` (until `ar_runtime_ticks_due` is 0) / `ar_runtime_submit_frame` / `ar_runtime_end_frame` until begin returns 0. Assertions: `CHECK(fp.submitted == 10)`, `CHECK(cube_ticks() >= 3)` (10 frames at 90 Hz span 3 ticks at 30 Hz), the last `ar_render_frame` had `snapshot != nullptr`, `view_count == 1`, and `rf.views[0].clip_from_level` is finite in every element.

`runtime/tests/CMakeLists.txt`: a function `sm64ar_test(name src...)` doing `add_executable`, link `ar_core ar_platform_fake ar_cube_adapter ar_render_common ar_vos_logic`, `add_test(NAME name COMMAND name)`, `set_tests_properties(name PROPERTIES SKIP_RETURN_CODE 77)`.

- [ ] **Step 5: Configure and run on the host**

Run: `cmake -S . -B build/host -G Ninja -DSM64AR_SM64=OFF && cmake --build build/host && ctest --test-dir build/host --output-on-failure`
Expected: configure succeeds with no Xcode message, build has no errors, `1/1 Test #1: test_runtime_cube ... Passed`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt cmake/Toolchain.cmake runtime/CMakeLists.txt runtime/tests
git commit -m "shared: build the portable runtime on Linux with a ctest suite"
```

---

### Task 3: Android app skeleton, APK packaging and device scripts

**Files:**
- Create: `apps/quest/CMakeLists.txt`, `apps/quest/main.cpp`, `apps/quest/AndroidManifest.xml`, `scripts/quest_package.py`, `scripts/quest_device.py`, `scripts/quest_build.py`, `scripts/tests/test_quest_scripts.py`
- Modify: `runtime/platform/visionos/vos_log.cpp` (logcat sink)

**Interfaces:**
- Consumes: Task 1's environment variables.
- Produces: `scripts/quest_build.py [--config Debug|Release] [--no-install] [--launch]` builds `build/quest/` and produces `build/quest/Diorama64.apk`; `scripts/quest_device.py` exposes `find_device(adb_devices_output: str) -> str | None` (the serial of the one connected device, `None` for zero or more than one), `install(apk: Path)`, `launch()`, `logcat(pattern: str | None)`; `scripts/quest_package.py` exposes `package(build_dir: Path, so_paths: list[Path], assets_dir: Path | None, out_apk: Path)` and the pure `aapt2_link_cmd(aapt2, manifest, android_jar, assets_dir, out)`. `ar::vos_log` lines reach logcat under tag `Diorama64` on Android.

- [ ] **Step 1: Write the script tests**

`scripts/tests/test_quest_scripts.py`:
```python
def test_find_device_one(self):
    self.assertEqual(find_device("List of devices attached\n2G0YC1ZF8M0ABC\tdevice\n\n"), "2G0YC1ZF8M0ABC")
def test_find_device_none_or_many(self):
    self.assertIsNone(find_device("List of devices attached\n\n"))
    self.assertIsNone(find_device("List of devices attached\nA\tdevice\nB\tdevice\n"))
def test_find_device_unauthorized(self):
    self.assertIsNone(find_device("List of devices attached\nA\tunauthorized\n"))
def test_aapt2_link_cmd(self):
    cmd = aapt2_link_cmd("/t/aapt2", "/m/AndroidManifest.xml", "/s/android.jar", "/a", "/o/base.apk")
    self.assertEqual(cmd, ["/t/aapt2", "link", "-o", "/o/base.apk", "--manifest", "/m/AndroidManifest.xml",
                          "-I", "/s/android.jar", "--min-sdk-version", "32", "--target-sdk-version", "32", "-A", "/a"])
def test_aapt2_link_cmd_no_assets(self):
    self.assertNotIn("-A", aapt2_link_cmd("a", "m", "j", None, "o"))
```

- [ ] **Step 2: Run them to verify they fail**

Run: `python3 -m unittest discover -s scripts/tests -v`
Expected: FAIL with `ModuleNotFoundError` (add `sys.path` insertion of `scripts/` at the top of the test file).

- [ ] **Step 3: Write `scripts/quest_device.py` and `scripts/quest_package.py`**

`quest_package.package`: `aapt2 link` as above into `base.apk`; add each `.so` at `lib/arm64-v8a/<name>` with `zipfile` (`ZIP_DEFLATED` is fine because the manifest sets `extractNativeLibs="true"`); `zipalign -f -p 4 base.apk aligned.apk`; `apksigner sign --ks <build_dir>/debug.keystore --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey --out <out_apk> aligned.apk`, generating the keystore on first use with `keytool -genkeypair -keystore ... -alias androiddebugkey -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"`. Tools are found through `ANDROID_BUILD_TOOLS` and `JAVA_HOME/bin`. `quest_device.install` runs `adb -s <serial> install -r -d <apk>`; `launch` runs `adb shell am start -n org.pagetable.diorama64/android.app.NativeActivity`; `logcat` runs `adb logcat -s Diorama64` (filtered by `pattern` if given).

- [ ] **Step 4: Run the script tests**

Run: `python3 -m unittest discover -s scripts/tests -v`
Expected: 5 tests, OK.

- [ ] **Step 5: Write the manifest**

`apps/quest/AndroidManifest.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
          package="org.pagetable.diorama64" android:versionCode="1" android:versionName="0.1"
          android:installLocation="auto">
  <uses-sdk android:minSdkVersion="32" android:targetSdkVersion="32"/>
  <uses-feature android:name="android.hardware.vulkan.level" android:version="1" android:required="true"/>
  <uses-feature android:name="android.hardware.vr.headtracking" android:version="1" android:required="true"/>
  <uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true"/>
  <queries>
    <provider android:authorities="org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker"/>
    <intent><action android:name="org.khronos.openxr.OpenXRRuntimeService"/></intent>
    <intent><action android:name="org.khronos.openxr.OpenXRApiLayerService"/></intent>
  </queries>
  <application android:hasCode="false" android:label="Diorama64" android:extractNativeLibs="true">
    <meta-data android:name="com.oculus.supportedDevices" android:value="quest3|quest3s"/>
    <activity android:name="android.app.NativeActivity" android:exported="true" android:launchMode="singleTask"
              android:screenOrientation="landscape"
              android:configChanges="density|keyboard|keyboardHidden|navigation|orientation|screenLayout|screenSize|uiMode"
              android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen">
      <meta-data android:name="android.app.lib_name" android:value="diorama64"/>
      <meta-data android:name="com.oculus.vr.focusaware" android:value="true"/>
      <intent-filter>
        <action android:name="android.intent.action.MAIN"/>
        <category android:name="com.oculus.intent.category.VR"/>
        <category android:name="android.intent.category.LAUNCHER"/>
      </intent-filter>
    </activity>
  </application>
</manifest>
```
If `aapt2 link` or the headset rejects the `com.oculus.supportedDevices` value, check Meta's current manifest reference and fix the string; note the finding in the commit message.

- [ ] **Step 6: Write the app skeleton and its CMake**

`apps/quest/CMakeLists.txt`: `add_library(diorama64 SHARED main.cpp ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)`, include that glue directory, link `android log ar_core ar_cube_adapter ar_vos_logic`, link option `-u ANativeActivity_onCreate`. `apps/quest/main.cpp`: `extern "C" void android_main(android_app *app)`: open the log with `ar::vos_log_open((std::string(app->activity->internalDataPath) + "/sm64ar.log").c_str())`, log `AR_QUEST_START`, then loop on `ALooper_pollOnce(-1, ...)` processing `android_app` commands until `app->destroyRequested`, logging each `APP_CMD_*` value.

`runtime/platform/visionos/vos_log.cpp`: under `#if defined(__ANDROID__)`, every line also goes to `__android_log_write(ANDROID_LOG_INFO, "Diorama64", line)`. Nothing else changes.

- [ ] **Step 7: Write `scripts/quest_build.py`**

Configure: `cmake -S ROOT -B build/quest -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=<config> -DSM64AR_SM64=OFF -DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR -DSM64AR_GLSLANG_DIR=$SM64AR_GLSLANG_DIR`; build; `quest_package.package(build/quest, [libdiorama64.so and every other .so the build produced under build/quest/lib], None, build/quest/Diorama64.apk)`; then install and optionally launch through `quest_device`, refusing with a clear message when `find_device` returns `None`.

- [ ] **Step 8: Build, install, run**

Run: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
Expected: the APK installs; logcat shows `AR_QUEST_START` and `APP_CMD_*` lines under tag `Diorama64`; the headset shows a black app window without crashing.

- [ ] **Step 9: Commit (two commits)**

```bash
git add runtime/platform/visionos/vos_log.cpp
git commit -m "shared: vos_log also writes to logcat on Android"
git add apps/quest scripts/quest_build.py scripts/quest_package.py scripts/quest_device.py scripts/tests/test_quest_scripts.py
git commit -m "quest: NativeActivity skeleton, APK packaging and adb scripts"
```

---

### Task 4: Pure OpenXR conversion helpers

**Files:**
- Create: `runtime/platform/quest/qx_convert.h`, `qx_convert.cpp`, `runtime/tests/test_qx_convert.cpp`
- Modify: `runtime/CMakeLists.txt` (library `ar_qx_convert`, built on host and Android; it includes only `openxr/openxr.h` from `SM64AR_OPENXR_DIR/include`, no loader)

**Interfaces:**
- Produces (namespace `ar`):
  - `ar_view qx_view_from_xr(const XrView &v, uint32_t width, uint32_t height, float near_m)`: pose copied field by field, fov as tangents (`tanf(angle)`), so `fov.left` and `fov.down` come out negative.
  - `bool qx_fill_frame_info(const XrView *views, uint32_t n, XrViewStateFlags flags, XrTime display_time, float display_hz, uint32_t width, uint32_t height, float near_m, const ar_pose &last_head, ar_frame_info *out)`: fills every field of `ar_frame_info`; `head_tracked` is 1 only when both `XR_VIEW_STATE_POSITION_VALID_BIT` and `XR_VIEW_STATE_ORIENTATION_VALID_BIT` are set; when it is 0, `tracking_from_head` is `last_head` and the views keep the poses the runtime gave; `tracking_from_head` when tracked is the midpoint of the two eye positions with view 0's orientation; `should_render` is set by the caller afterwards. Returns false if `n == 0` or `n > AR_MAX_VIEWS`.
  - `ar_session_state qx_session_state(XrSessionState s, bool exit_requested)`: `IDLE`/`UNKNOWN` → `AR_SESSION_IDLE`, `READY` → `READY`, `SYNCHRONIZED`/`VISIBLE` → `VISIBLE`, `FOCUSED` → `FOCUSED`, `STOPPING` → `STOPPING`, `LOSS_PENDING`/`EXITING` → `LOST`; `exit_requested` forces `STOPPING`.
  - `int64_t qx_pick_swapchain_format(const int64_t *formats, uint32_t n, bool *linearize)`: first of `VK_FORMAT_R8G8B8A8_SRGB`, `VK_FORMAT_B8G8R8A8_SRGB` present (`*linearize = true`), else first of `VK_FORMAT_R8G8B8A8_UNORM`, `VK_FORMAT_B8G8R8A8_UNORM` (`*linearize = false`), else 0.
  - `XrPosef qx_xr_pose(const ar_pose &p)` and `ar_pose qx_ar_pose(const XrPosef &p)`.

- [ ] **Step 1: Write the tests** (`test_qx_convert.cpp`, with `#include <openxr/openxr.h>` and `<vulkan/vulkan.h>` for the format enums)

```cpp
// fov tangents and sign
XrView v{XR_TYPE_VIEW}; v.pose = {{0,0,0,1},{0.1f,1.6f,-0.2f}}; v.fov = {-0.8f, 0.7f, 0.75f, -0.85f};
ar_view a = qx_view_from_xr(v, 1832, 1920, 0.f);
CHECK(fabsf(a.fov.left - tanf(-0.8f)) < 1e-6f && a.fov.left < 0.f && a.fov.down < 0.f && a.fov.right > 0.f);
CHECK(a.width == 1832 && a.height == 1920 && a.tracking_from_eye.pos[1] == 1.6f && a.tracking_from_eye.quat[3] == 1.f);
// tracked frame: head at the eye midpoint
XrView vs[2] = {v, v}; vs[1].pose.position.x = 0.164f;
ar_pose last = ar_pose_identity(); last.pos[1] = 9.f;
ar_frame_info fi{};
CHECK(qx_fill_frame_info(vs, 2, XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT, 123456789, 90.f, 1832, 1920, 0.f, last, &fi));
CHECK(fi.head_tracked == 1 && fi.view_count == 2 && fi.display_time_ns == 123456789 && fi.display_hz == 90.f);
CHECK(fabsf(fi.tracking_from_head.pos[0] - 0.132f) < 1e-6f && fi.tracking_from_head.pos[1] == 1.6f);
// tracking lost: last head kept
CHECK(qx_fill_frame_info(vs, 2, 0, 1, 90.f, 1832, 1920, 0.f, last, &fi));
CHECK(fi.head_tracked == 0 && fi.tracking_from_head.pos[1] == 9.f);
CHECK(!qx_fill_frame_info(vs, 0, 0, 1, 90.f, 1, 1, 0.f, last, &fi));
// session states
CHECK(qx_session_state(XR_SESSION_STATE_FOCUSED, false) == AR_SESSION_FOCUSED);
CHECK(qx_session_state(XR_SESSION_STATE_VISIBLE, false) == AR_SESSION_VISIBLE);
CHECK(qx_session_state(XR_SESSION_STATE_SYNCHRONIZED, false) == AR_SESSION_VISIBLE);
CHECK(qx_session_state(XR_SESSION_STATE_IDLE, false) == AR_SESSION_IDLE);
CHECK(qx_session_state(XR_SESSION_STATE_LOSS_PENDING, false) == AR_SESSION_LOST);
CHECK(qx_session_state(XR_SESSION_STATE_FOCUSED, true) == AR_SESSION_STOPPING);
// swapchain formats
bool lin = false;
const int64_t f1[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB};
CHECK(qx_pick_swapchain_format(f1, 2, &lin) == VK_FORMAT_B8G8R8A8_SRGB && lin);
const int64_t f2[] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_B8G8R8A8_UNORM};
CHECK(qx_pick_swapchain_format(f2, 2, &lin) == VK_FORMAT_B8G8R8A8_UNORM && !lin);
const int64_t f3[] = {VK_FORMAT_R16G16B16A16_SFLOAT};
CHECK(qx_pick_swapchain_format(f3, 1, &lin) == 0);
// pose round trip
ar_pose p = ar_pose_identity(); p.pos[2] = -1.f; p.quat[0] = 0.5f; p.quat[3] = 0.5f;
ar_pose q = qx_ar_pose(qx_xr_pose(p));
CHECK(memcmp(&p, &q, sizeof p) == 0);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/host && ctest --test-dir build/host -R qx_convert`
Expected: configure/build error, `qx_convert.h` not found.

- [ ] **Step 3: Implement `qx_convert.cpp` and add `ar_qx_convert` to `runtime/CMakeLists.txt`**

`add_library(ar_qx_convert STATIC platform/quest/qx_convert.cpp)`, PUBLIC include `platform/quest` and `${SM64AR_OPENXR_DIR}/include`, link `ar_core`; add it to `sm64ar_test` link list. Configure the host build with `-DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR` from now on (add this flag to the Task 2 configure command in your shell history; `quest_build.py` already passes it).

- [ ] **Step 4: Run the test**

Run: `cmake -S . -B build/host -G Ninja -DSM64AR_SM64=OFF -DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR && cmake --build build/host && ctest --test-dir build/host --output-on-failure`
Expected: `test_qx_convert ... Passed`.

- [ ] **Step 5: Commit**

```bash
git add runtime/platform/quest/qx_convert.h runtime/platform/quest/qx_convert.cpp runtime/tests/test_qx_convert.cpp runtime/tests/CMakeLists.txt runtime/CMakeLists.txt
git commit -m "quest: pure OpenXR conversion helpers with tests"
```

---

### Task 5: OpenXR platform: session, Vulkan device, swapchain, passthrough

**Files:**
- Create: `runtime/platform/quest/qx_platform.h`, `qx_platform.cpp`
- Modify: `runtime/CMakeLists.txt` (library `ar_quest`, Android only), `apps/quest/CMakeLists.txt`, `apps/quest/main.cpp`

**Interfaces:**
- Consumes: Task 4's helpers.
- Produces (namespace `ar`):
```cpp
struct QxVulkan { VkInstance instance; VkPhysicalDevice physical; VkDevice device; uint32_t queue_family; VkQueue queue;
                  PFN_vkGetInstanceProcAddr get_instance_proc_addr; };
struct QxSwapchainImage { VkImage image; VkImageView view; VkFormat format; uint32_t width, height, layers; };  // what ExternalView::color points at
class QxPlatform {
public:
    struct Config { bool passthrough = true; float refresh_hz = 90.f; ar_pose fixed_table; bool fixed_table_valid = false; };
    QxPlatform(android_app *app, const Config &c);
    ~QxPlatform();
    bool start(std::string *err);           // loader init, instance, system, Vulkan via XR_KHR_vulkan_enable2, session, spaces, swapchain, passthrough
    void handle_cmd(int32_t cmd);           // APP_CMD_RESUME / PAUSE / DESTROY (android_app callbacks forward here)
    void poll_events();                     // xrPollEvent; begins / ends the session; called inside wait_frame too
    const ar_platform *api() const;
    const QxVulkan &vulkan() const;
    bool frame_renderable() const;          // this frame's shouldRender and an acquired swapchain image
    bool external_view(uint32_t i, ExternalView *out) const;   // color = &QxSwapchainImage, slice i, viewport full size, linearize per format
    bool color_wants_linear() const;
    uint32_t swapchain_width() const; uint32_t swapchain_height() const;
    void end_submission();                  // after the GPU work is submitted: release the swapchain image, xrEndFrame; a no-op when this frame had no xrBeginFrame (session not running)
    bool exiting() const;
};
```
`api()` implements: `wait_frame` (poll events; if the session is not running, sleep 10 ms and return 1 with `should_render 0`, `head_tracked 0`; else `xrWaitFrame`, `xrLocateViews` in the app space, `qx_fill_frame_info`, `should_render` from `XrFrameState`), `begin_frame` (`xrBeginFrame`; when `shouldRender`, acquire and wait the swapchain image), `end_frame` (records `ar_submit_info` views for the projection layer; does not end the XR frame), `get_table` (the fixed table from `Config`, 1 when valid), `get_pad` (zeros, returns 0), `session_state` (`qx_session_state`), `set_passthrough` (pause/resume the passthrough layer); `persist_anchor`, `share_anchor`, `get_environment`, `get_preview` are null.

- [ ] **Step 1: Add the OpenXR loader and the library to CMake**

`runtime/CMakeLists.txt`, under `if(SM64AR_QUEST)`: `set(DYNAMIC_LOADER ON) set(BUILD_API_LAYERS OFF) set(BUILD_TESTS OFF) set(BUILD_CONFORMANCE_TESTS OFF) add_subdirectory(${SM64AR_OPENXR_DIR} ${CMAKE_BINARY_DIR}/openxr)` then `add_library(ar_quest STATIC platform/quest/qx_platform.cpp)` linking `ar_qx_convert ar_render_common openxr_loader vulkan android log` and compile definitions `XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_VULKAN VK_USE_PLATFORM_ANDROID_KHR`.

- [ ] **Step 2: Implement `QxPlatform::start`**

Order: `xrInitializeLoaderKHR` with `XrLoaderInitInfoAndroidKHR{app->activity->vm, app->activity->clazz}`; `xrCreateInstance` with extensions `XR_KHR_android_create_instance`, `XR_KHR_vulkan_enable2`, `XR_FB_passthrough`, `XR_FB_display_refresh_rate` (the last two only if enumerated; record which are present); `xrGetSystem(HEAD_MOUNTED_DISPLAY)`; `xrGetVulkanGraphicsRequirements2KHR`; `xrCreateVulkanInstanceKHR` (Vulkan 1.1, no layers); `xrGetVulkanGraphicsDevice2KHR`; pick the first queue family with GRAPHICS; `xrCreateVulkanDeviceKHR` with one queue, `VkPhysicalDeviceFeatures.shaderClipDistance`, `depthClamp`, `samplerAnisotropy` and `fragmentStoresAndAtomics` enabled, and the device extension `VK_EXT_shader_viewport_index_layer` (the vertex stage writes `gl_Layer`; fail with a clear message if the device lacks it) plus `VK_KHR_depth_stencil_resolve` when available; `xrCreateSession` with `XrGraphicsBindingVulkan2KHR`; app space `XR_REFERENCE_SPACE_TYPE_STAGE` if enumerated, else `LOCAL` (log which: `AR_QUEST_SPACE stage=0|1`); view configuration `PRIMARY_STEREO`, swapchain `arraySize 2`, size from the recommended view size, format from `qx_pick_swapchain_format`, usage COLOR_ATTACHMENT | SAMPLED, one `VkImageView` of type 2D_ARRAY per image; passthrough: `xrCreatePassthroughFB`, `xrCreatePassthroughLayerFB(RECONSTRUCTION)`, start both; `xrRequestDisplayRefreshRateFB(refresh_hz)` if the extension is present, logging `AR_QUEST_REFRESH requested=%g got=%g`. Fail with a message naming the step on any `XrResult` below `XR_SUCCESS`.

- [ ] **Step 3: Implement the frame functions and `end_submission`**

`end_submission`: `xrReleaseSwapchainImage`; `xrEndFrame` with `environmentBlendMode OPAQUE` and layers `[XrCompositionLayerPassthroughFB, XrCompositionLayerProjection]` where the projection has `layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT`, one `XrCompositionLayerProjectionView` per eye with the pose and fov from this frame's `xrLocateViews`, `subImage.imageArrayIndex = eye`, `imageRect` the full swapchain size; with `should_render` false, `xrEndFrame` with no layers. The passthrough layer is omitted when passthrough is off.

- [ ] **Step 4: Wire the skeleton to clear the swapchain**

In `apps/quest/main.cpp`: after `start`, each loop iteration calls `wait_frame`/`begin_frame` through `api()`; when `frame_renderable()`, record and submit one command buffer that transitions nothing and clears the acquired image to `{0,0,0,0}` with `vkCmdClearColorImage` (layout `COLOR_ATTACHMENT_OPTIMAL` as acquired; barrier to TRANSFER_DST and back), `vkQueueWaitIdle`, then `end_frame`/`end_submission`. Log `AR_QUEST_HEAD x y z` once per second from `tracking_from_head`. Link `ar_quest` in `apps/quest/CMakeLists.txt`, and have `quest_build.py` pick up `libopenxr_loader.so` from the build tree (it already packages every `.so`).

- [ ] **Step 5: Run on the headset**

Run: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
Expected: logcat shows `AR_QUEST_SPACE stage=1`, `AR_QUEST_REFRESH requested=90 got=90` (or 72), then `AR_QUEST_HEAD` lines whose values change as you move; the headset shows plain passthrough with no black frame, and pressing the Meta button then returning resumes the head lines. If `xrCreateInstance` fails with `XR_ERROR_RUNTIME_UNAVAILABLE`, stop: this is the loader blocker the spec names; report it rather than swapping loaders.

- [ ] **Step 6: Commit**

```bash
git add runtime/platform/quest/qx_platform.h runtime/platform/quest/qx_platform.cpp runtime/CMakeLists.txt apps/quest
git commit -m "quest: OpenXR session with Vulkan swapchain and passthrough"
```

---

### Task 6: GLSL emitter and runtime compiler

**Files:**
- Create: `runtime/render/vulkan/glsl_emitter.h`, `glsl_emitter.cpp`, `glsl_compiler.h`, `glsl_compiler.cpp`, `vk_layout.h`, `runtime/tests/test_glsl_emitter.cpp`
- Modify: `runtime/CMakeLists.txt` (library `ar_render_vulkan_shaders`: the emitter alone, host and Android; and `ar_glsl_compiler` with glslang)

**Interfaces:**
- Produces (namespace `ar`):
  - `struct GlslProgram { std::string vertex, fragment; };`
  - `GlslProgram emit_glsl(const CombinerIR &ir);` — the Vulkan GLSL (`#version 450`) counterpart of `emit_msl`: the same `ViewU`, `DrawU`, `LightU` uniform blocks with the same member names and order (std140; `float4x4` → `mat4`, `float4` → `vec4`, `uint` → `uint`), vertex inputs at locations 0–3 (pos, uv0, uv1, shade), the vertex stage a full port of `vmain` (interpolation from `prev_pos`, decal and silhouette pull, five clip distances via `gl_ClipDistance[5]`, `gl_Layer` written when `ir.layer_out`, normals and `lit`), the fragment stage porting the combiner body (`rgb_op`/`a_op` for all 16 operand sources, two-cycle, alpha test, texture edge, alpha-to-coverage, noise, dither, fog, screen `clip`, `flat_color`/`flat_cutout`, `depth_fill` variants). The room-lit features (`room_light`, `toy_surface`, `table_shadow`, `liquid_*`, `water_trim`, `cut_away`, `transition_cover`, `ink_flip`) are emitted as functions returning their neutral value in this plan and ported in plan 2; `LightU` is declared in full now so plan 2 changes no layouts.
  - `vk_layout.h`: `enum : uint32_t { kSetUniforms = 0, kSetTextures = 1 }`, bindings in set 0: `kBindView = 0` (UBO, dynamic offset), `kBindDraw = 1` (UBO, dynamic offset), `kBindLight = 2` (UBO), `kBindNormals = 3` (SSBO), `kBindPrevPos = 4` (SSBO); set 1: `kBindTex0 = 0`, `kBindTex1 = 1`, `kBindShadow = 2` (sampler2DShadow), `kBindUnderside = 3`, `kBindAo = 4` (sampler3D), `kBindEnv = 5` (samplerCube), `kBindEnvOut = 6`, `kBindWaterFoot = 7`. Vertex layout: stride 32, location 0 `R32G32B32_SFLOAT` offset 0, 1 `R32G32_SFLOAT` offset 12, 2 `R32G32_SFLOAT` offset 20, 3 `R8G8B8A8_UNORM` offset 28. The emitter's `layout(set=, binding=)` qualifiers use these constants (emitted as numbers).
  - `bool compile_glsl(const std::string &source, GlslStage stage, std::vector<uint32_t> *spirv, std::string *error);` with `enum class GlslStage { vertex, fragment, compute }`, using glslang with `glslang::EShTargetVulkan_1_1`, `EShTargetSpv_1_3`, the default resource limits; `glsl_compiler_init()` / `glsl_compiler_finish()` wrap `glslang::InitializeProcess/FinalizeProcess`.

- [ ] **Step 1: Write the emitter test** (`test_glsl_emitter.cpp`; links `ar_render_common ar_render_vulkan_shaders ar_glsl_compiler`)

```cpp
glsl_compiler_init();
// the cube's key: shade only
fc_combiner_key k{}; k.slots = ((uint64_t)FC_SRC_SHADE << 12) | ((uint64_t)FC_SRC_SHADE << 28);
GlslProgram p = emit_glsl(decode_combiner(normalize_combiner(k)));
std::vector<uint32_t> spv; std::string err;
CHECK(compile_glsl(p.vertex, GlslStage::vertex, &spv, &err) && !spv.empty());
CHECK(compile_glsl(p.fragment, GlslStage::fragment, &spv, &err) && !spv.empty());
CHECK(p.vertex.find("gl_Layer") != std::string::npos && p.vertex.find("gl_ClipDistance") != std::string::npos);
// every operand source in every slot compiles, with and without each option bit
for (uint32_t src = 0; src < 16; src++) for (uint32_t opts = 0; opts < 128; opts += 9) {
    fc_combiner_key kk{}; for (int s = 0; s < 16; s++) kk.slots |= (uint64_t)src << (4 * s); kk.opts = opts;
    CombinerIR ir = decode_combiner(normalize_combiner(kk));
    if (ir.unsupported) continue;
    GlslProgram q = emit_glsl(ir);
    CHECK(compile_glsl(q.vertex, GlslStage::vertex, &spv, &err));
    if (!compile_glsl(q.fragment, GlslStage::fragment, &spv, &err)) { fprintf(stderr, "src=%u opts=%u\n%s\n%s\n", src, opts, err.c_str(), q.fragment.c_str()); CHECK(false); }
}
// the IR variants the renderer uses
CombinerIR ir = decode_combiner(normalize_combiner(k));
for (uint8_t df : {kDepthOnlyHalf, kDepthOnlyFaint, kDepthOnlyPrepass}) { CombinerIR v = ir; v.depth_fill = df; CHECK(compile_glsl(emit_glsl(v).fragment, GlslStage::fragment, &spv, &err)); }
CombinerIR v = ir; v.layer_out = false; CHECK(emit_glsl(v).vertex.find("gl_Layer") == std::string::npos);
v = ir; v.flat_color = true; v.flat_cutout = true; v.clip = true; v.table_shadow = true; v.cutaway = true;
CHECK(compile_glsl(emit_glsl(v).fragment, GlslStage::fragment, &spv, &err));
// a broken source reports an error and no SPIR-V
spv.clear(); CHECK(!compile_glsl("#version 450\nvoid main() { undefined(); }", GlslStage::fragment, &spv, &err) && !err.empty() && spv.empty());
glsl_compiler_finish();
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/host 2>&1 | tail -3`
Expected: `glsl_emitter.h: No such file or directory`.

- [ ] **Step 3: Add glslang to CMake and write `glsl_compiler.cpp`**

In `runtime/CMakeLists.txt`, under `if(SM64AR_QUEST OR SM64AR_HOST_TESTS)`: `set(ENABLE_OPT OFF) set(ENABLE_GLSLANG_BINARIES OFF) set(ENABLE_HLSL OFF) set(ENABLE_SPVREMAPPER OFF) set(ENABLE_CTEST OFF) set(BUILD_EXTERNAL OFF) set(GLSLANG_TESTS OFF) set(GLSLANG_ENABLE_INSTALL OFF)` then `add_subdirectory(${SM64AR_GLSLANG_DIR} ${CMAKE_BINARY_DIR}/glslang EXCLUDE_FROM_ALL)`; `add_library(ar_glsl_compiler STATIC render/vulkan/glsl_compiler.cpp)` linking `glslang SPIRV glslang-default-resource-limits`, PUBLIC include `render/vulkan`. If the pinned glslang needs SPIRV-Tools even with `ENABLE_OPT OFF`, add `spirv-tools` and `spirv-headers` as further non-flake inputs in `flake.nix` rather than fetching at build time.

- [ ] **Step 4: Write `glsl_emitter.cpp`** as a port of `msl_emitter.cpp` under the rules in Interfaces. Keep the structure (a prelude string, `rgb_op`/`a_op`, body assembly) parallel to the Metal file so plan 2 can port the remaining functions one for one. `add_library(ar_render_vulkan_shaders STATIC render/vulkan/glsl_emitter.cpp)` linking `ar_render_common`.

- [ ] **Step 5: Run the test**

Run: `cmake -S . -B build/host -G Ninja -DSM64AR_SM64=OFF -DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR -DSM64AR_GLSLANG_DIR=$SM64AR_GLSLANG_DIR && cmake --build build/host && ctest --test-dir build/host -R glsl --output-on-failure`
Expected: `test_glsl_emitter ... Passed`.

- [ ] **Step 6: Commit**

```bash
git add runtime/render/vulkan/glsl_emitter.h runtime/render/vulkan/glsl_emitter.cpp runtime/render/vulkan/glsl_compiler.h runtime/render/vulkan/glsl_compiler.cpp runtime/render/vulkan/vk_layout.h runtime/tests/test_glsl_emitter.cpp runtime/tests/CMakeLists.txt runtime/CMakeLists.txt
git commit -m "quest: GLSL emitter for the N64 combiner and a glslang runtime compiler"
```

---

### Task 7: Vulkan render device (cube subset) with a host test under lavapipe

**Files:**
- Create: `runtime/render/vulkan/vulkan_device.h`, `vulkan_device.cpp`, `runtime/tests/vk_host_context.h`, `vk_host_context.cpp`, `runtime/tests/test_vulkan_cube.cpp`
- Modify: `runtime/CMakeLists.txt` (library `ar_render_vulkan`, host and Android)

**Interfaces:**
- Consumes: `IRenderDevice` (`render_device.h`), Task 6's emitter, compiler and `vk_layout.h`, `QxVulkan`/`QxSwapchainImage` field layout (the device takes the same fields through its own init struct so it does not depend on `ar_quest`).
- Produces (namespace `ar`):
```cpp
struct VulkanDeviceInit { VkInstance instance; VkPhysicalDevice physical; VkDevice device; uint32_t queue_family; VkQueue queue;
                          PFN_vkGetInstanceProcAddr get_instance_proc_addr; std::string pipeline_cache_path; };
struct VulkanExternalImage { VkImage image; VkImageView view; VkFormat format; uint32_t width, height, layers; };  // same layout as QxSwapchainImage
std::unique_ptr<IRenderDevice> create_vulkan_device(const VulkanDeviceInit &init);
```
Implemented in this plan: `upload_texture` (RGBA8 2D, full mip chain generated with `vkCmdBlitImage`, one sampler per `fc_sampler` combination), `set_edge_coverage`, `set_texture_filter`, `create_target`/`resize_target`/`release_target` (colour `R8G8B8A8_UNORM` 2D array of `layers`, depth `D32_SFLOAT_S8_UINT`, one render pass per target; `samples` other than 1 are accepted but rendered at 1 in this plan, `supported_samples` returns 1), `target_samples`, `bind_target_texture` (records the binding; sampling it is plan 2), `memoryless_msaa` (true if any memory type has `LAZILY_ALLOCATED`), `set_rate_map` (no-op), `begin_frame`, `encode_pass` (records the pass; the target's passes are encoded in order at the first `encode_resolve`/`end_frame`, as the Metal device does; per draw: pipeline from `pipe_identity` + target key, `ViewU`/`DrawU` through dynamic-offset UBOs in one host-visible ring buffer per frame-in-flight, vertices copied into a per-frame vertex ring, normals and prev_pos into SSBO rings, `cull`, `depth_test`/`depth_write`, `blend` (premultiplied: `ONE`, `ONE_MINUS_SRC_ALPHA`), `decal` as slope-scaled depth bias `-decal_slope`), `set_lighting` (uploads `LightU`), `encode_resolve` (a full-screen pass into `dst.color`'s view at `slice`, sampling the target's colour layer `src_layer`, writing depth from the target's depth; `linearize` applies the sRGB-to-linear curve), `end_frame` (one `vkQueueSubmit` with a fence; a completion thread waits fences in order and calls `on_gpu_done(ctx)`; two frames in flight), `wait_idle`, `last_gpu_ms` (timestamp queries), `render_encoders_last_frame`, `late_passes`, `read_target` and `read_target_depth` (staging buffer, `wait_idle` first), `compile_combiner` (emit + compile + pipeline; false with the message on failure, remembered for `take_pipeline_error`), `pipeline_list`/`warm_pipelines`/`warm_pipelines_background` (synchronous in this plan)/`pipelines_warming` (0)/`draw_time_builds`/`take_pipeline_error`/`pipeline_count` (the list uses the Metal device's line format from `metal_device.mm` `pipeline_list`, but with the format field written as the `VkFormat` number, so the two platforms' lists are never mixed), `command_buffer` (the current `VkCommandBuffer`). Stubs returning false / doing nothing, filled in plan 2: `encode_underside_map`, `encode_shadow_map`, `bake_ao`, `encode_environment_reduce`, `environment_stats`, `encode_composite`. A `VkPipelineCache` is loaded from and saved to `pipeline_cache_path` when non-empty.

- [ ] **Step 1: Write the host Vulkan context helper**

`vk_host_context.h`: `bool vk_host_create(VulkanDeviceInit *out, std::string *err)` creating a Vulkan 1.1 instance with no extensions, picking the first physical device whose name contains `llvmpipe` (else the first), a graphics queue, and a device with the same features and extensions Task 5 enables (`shaderClipDistance`, `depthClamp`, `samplerAnisotropy`, `fragmentStoresAndAtomics`, `VK_EXT_shader_viewport_index_layer`, `VK_KHR_depth_stencil_resolve` if present); `void vk_host_destroy(const VulkanDeviceInit &)`. Returns false (and the test skips with code 77) when `vkCreateInstance` fails or no device exists.

- [ ] **Step 2: Write the device test** (`test_vulkan_cube.cpp`)

```cpp
VulkanDeviceInit init{}; std::string err;
if (!vk_host_create(&init, &err)) SKIP(err.c_str());
auto dev = create_vulkan_device(init);
Renderer renderer(*dev);
fake_platform fp; fake_platform_init(&fp, 90.0); fp.width = 256; fp.height = 256; fp.view_count = 2; fp.stop_after = 4;
ar_runtime *rt = ar_runtime_create(cube_adapter(), &fp.api);
CHECK(ar_runtime_init_game(rt, "", ""));
ar_runtime_set_texture_sink(rt, [](void *u, uint32_t id, uint32_t w, uint32_t h, const uint8_t *px) { static_cast<IRenderDevice *>(u)->upload_texture(id, w, h, px); }, dev.get());
const uint32_t target = dev->create_target(256, 256, 2, 1);
ar_render_frame rf{};
for (int i = 0; i < 4; i++) {
    CHECK(ar_runtime_begin_frame(rt, &rf));
    while (ar_runtime_ticks_due(rt)) ar_runtime_step_game(rt);
    if (rf.snapshot) {
        LightingFrame lf; ArLayerOptions lo; lo.layered = true; lo.lighting = &lf; lo.overlays = false;
        dev->begin_frame();
        draw_ar_layer(renderer, target, rf, lo, 1.f / 90.f);
        ar_runtime_submit_frame(rt, &rf);
        dev->end_frame([](void *) {}, nullptr, nullptr);
    } else { ar_runtime_submit_frame(rt, &rf); }
    ar_runtime_end_frame(rt, &rf);
}
dev->wait_idle();
std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
CHECK(dev->read_target(target, rgba, w, h) && w == 256 && h == 256 && rgba.size() == 256u * 256u * 4u);
// the cube (40 cm at 1:8000 on a table 0.75 m up, head 2 m back at 1.5 m) covers the middle, not the corners
auto px = [&](uint32_t x, uint32_t y) { return &rgba[(y * 256 + x) * 4]; };
CHECK(px(128, 140)[3] == 255 && (px(128, 140)[0] + px(128, 140)[1] + px(128, 140)[2]) > 0);
CHECK(px(2, 2)[3] == 0 && px(253, 253)[3] == 0);
std::vector<float> depth; CHECK(dev->read_target_depth(target, depth, w, h) && depth[140 * 256 + 128] > 0.f && depth[2 * 256 + 2] == 0.f);
// a pipeline error is reported, not fatal
fc_combiner_key bad{}; bad.slots = ~0ull; std::string why;
CHECK(!dev->compile_combiner(bad, &why) || dev->pipeline_count() > 0);
std::string perr; (void)dev->take_pipeline_error(&perr);
CHECK(dev->pipeline_count() >= 1);
ar_runtime_destroy(rt); dev.reset(); vk_host_destroy(init);
```
(The `bad` key may decode as supported; the assertion only requires that the call returns without aborting. `read_target` returns the layer 0 image.)

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build/host 2>&1 | tail -3`
Expected: `vulkan_device.h: No such file or directory`.

- [ ] **Step 4: Implement `vulkan_device.cpp`** per Interfaces. Add `add_library(ar_render_vulkan STATIC render/vulkan/vulkan_device.cpp)` linking `ar_render_common ar_render_vulkan_shaders ar_glsl_compiler` and `Vulkan::Vulkan` on the host (`find_package(Vulkan REQUIRED)`) or `vulkan` on Android. The approach for pass recording, ring buffers and pipeline keys follows `metal_device.mm` (read `struct Rec`, `struct Target`, `struct PipeKey` and `encode_pass` there); keep the same `PipeKey` members plus `VkRenderPass`.

- [ ] **Step 5: Run the test**

Run: `cmake --build build/host && ctest --test-dir build/host -R vulkan --output-on-failure`
Expected: `test_vulkan_cube ... Passed` (not Skipped: the Task 1 shell sets `VK_DRIVER_FILES` to lavapipe). Run also with `VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation` and expect no validation error lines in the output.

- [ ] **Step 6: Commit**

```bash
git add runtime/render/vulkan/vulkan_device.h runtime/render/vulkan/vulkan_device.cpp runtime/tests/vk_host_context.h runtime/tests/vk_host_context.cpp runtime/tests/test_vulkan_cube.cpp runtime/tests/CMakeLists.txt runtime/CMakeLists.txt
git commit -m "quest: Vulkan render device (cube subset) tested under lavapipe"
```

---

### Task 8: QxSession frame loop and the cube on the headset

**Files:**
- Create: `runtime/platform/quest/qx_session.h`, `qx_session.cpp`
- Modify: `runtime/CMakeLists.txt` (`ar_quest` gains `qx_session.cpp` and links `ar_render_vulkan`), `apps/quest/main.cpp`, `apps/quest/CMakeLists.txt`

**Interfaces:**
- Consumes: `QxPlatform` (Task 5), `create_vulkan_device` (Task 7), `Renderer`, `draw_ar_layer`, `ar_runtime_*`.
- Produces (namespace `ar`):
```cpp
class QxSession {
public:
    QxSession(const ar_adapter *adapter, QxPlatform &platform, const std::string &files_dir);
    bool start(const std::string &rom_path, const std::string &save_dir, std::string *err);  // device (pipeline cache at files_dir/pipelines.vkcache), renderer, runtime, init_game, texture sink, start_game_thread
    bool frame();        // false when the platform stopped
    void stop();         // stop_game_thread, wait_idle, destroy
    uint64_t ticks_done() const;
};
```
`frame()` is `VosSession::frame` reduced: `ar_runtime_begin_frame`; if `!rf.snapshot || !platform.frame_renderable()`: `ar_runtime_submit_frame`, `platform.end_submission()`, `ar_runtime_end_frame`, return true. Else target size: layered, `tw = max view width`, `th = view height`, `tl = view_count`, `samples = dev->supported_samples(4, tl)`; resize when changed (log `AR_RENDER layered=1 target=%ux%ux%u samples=%u` like visionOS); `LightingFrame lf` default; `ArLayerOptions lo{ .layered = true, .lighting = &lf }`; `dev->begin_frame()`; `draw_ar_layer(renderer, target, rf, lo, dt)`; per view `platform.external_view(i, &ev)`, `ev.src_layer = i`, `ev.linearize = platform.color_wants_linear()`, `dev->encode_resolve(target, ev)`; `ar_runtime_submit_frame`; `dev->end_frame(on_done, new Done{rt, rf}, nullptr)` where `on_done` calls `ar_runtime_end_frame` and deletes; `platform.end_submission()`.

- [ ] **Step 1: Implement `qx_session.cpp`** per Interfaces.

- [ ] **Step 2: Wire `main.cpp`**

`android_main`: `QxPlatform::Config c; c.fixed_table = ar_pose_identity(); c.fixed_table.pos[1] = 0.75f; c.fixed_table.pos[2] = -1.0f; c.fixed_table_valid = true;` (with `LOCAL` space the platform subtracts 1.5 m from y so the table lands below eye level; `QxPlatform` applies that). `QxPlatform platform(app, c)`; `platform.start`; `QxSession session(cube_adapter(), platform, internalDataPath)`; `session.start("", internalDataPath + "/save", &err)`; loop: process `ALooper_pollOnce(0, ...)` events, forward commands to `platform.handle_cmd`, then `if (!session.frame()) break;`; on `destroyRequested` → `session.stop()`. Log any failure with `AR_QUEST_FAIL %s` and keep the app alive showing passthrough.

- [ ] **Step 3: Run on the headset**

Run: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
Expected: logcat shows `AR_RENDER layered=1 target=1832x1920x2 samples=1` (sizes per runtime), no `AR_QUEST_FAIL`; in the headset a 40 cm coloured cube stands 1 m in front of the stage origin at table height, stays world-locked as you walk around it, each face its own colour (red/green/blue/yellow/white/grey), with correct occlusion between faces and no judder at the display rate. Press the Meta button, wait 5 s, return: the cube is still there and the log resumes. Take the headset off for 10 s and put it on: same.

- [ ] **Step 4: Commit**

```bash
git add runtime/platform/quest/qx_session.h runtime/platform/quest/qx_session.cpp runtime/CMakeLists.txt apps/quest
git commit -m "quest: session frame loop; the cube adapter renders in passthrough"
```

---

### Task 9: sm64 library on Linux, the oracle on Linux, and the pack tool

**Files:**
- Modify: `scripts/sm64ar_common.py`, `scripts/sm64_oracle.py`, `games/sm64/CMakeLists.txt`, `scripts/packs.py`, `CMakeLists.txt` (add `apps/pack_tool` when `NOT APPLE AND NOT ANDROID AND SM64AR_SM64`)
- Create: `apps/pack_tool/CMakeLists.txt`, `apps/pack_tool/main.cpp`, `scripts/tests/test_packs.py`

**Interfaces:**
- Consumes: the user's ROM at `local/baserom.us.z64` (see `scripts/rom_to_z64.py`), the oracle outputs under `build/_src/sm64/build/us_pc`.
- Produces:
  - `scripts/sm64ar_common.py`: `IS_MAC = sys.platform == "darwin"`; `tool_problems() -> list[str]` (on macOS the Homebrew check as today; on Linux the names among `cmake ninja make pkg-config magick bsdtar` missing from PATH plus `sdl2`/`glew` missing from `pkg-config`); `make_program() -> str` (`gmake` if on PATH else `make`); `pack_generator_cmd(rom: Path, scratch: Path) -> list[str]` (macOS: `[HARNESS, "--offscreen", "--out", scratch, "--rom", rom]`; Linux: `[PACK_TOOL, "--rom", rom, "--res", RES_DIR]` where `PACK_TOOL = ROOT/build/host/pack_tool/sm64ar_pack_tool` and `RES_DIR = SRC/build/us_pc`); `resize_png_cmd(src, dst, cap: int | None) -> list[str]` (macOS: `["/usr/bin/sips", "-Z", str(cap), src, "--out", dst]` or `["/usr/bin/sips", "-s", "format", "png", src, "--out", dst]`; Linux: `["magick", src, "-resize", f"{cap}x{cap}>", f"png:{dst}"]` or `["magick", src, f"png:{dst}"]`).
  - `sm64ar_pack_tool --rom ROM --res RESDIR [--generate-tex IN OUT] [--generate-actors DIR]`: exit 0 and print `AR_GENERATED tex=OUT actors=DIR` on success; exit 1 with `error: ...` on stderr when the ROM is missing or the adapter refuses it, leaving `OUT` untouched.

- [ ] **Step 1: Write the Python tests** (`scripts/tests/test_packs.py`)

```python
def test_resize_cmd_linux(self):
    with mock.patch.object(common, "IS_MAC", False):
        self.assertEqual(common.resize_png_cmd("a.png", "b.png", 512), ["magick", "a.png", "-resize", "512x512>", "png:b.png"])
        self.assertEqual(common.resize_png_cmd("a.png", "b.png", None), ["magick", "a.png", "png:b.png"])
def test_resize_cmd_mac(self):
    with mock.patch.object(common, "IS_MAC", True):
        self.assertEqual(common.resize_png_cmd("a.png", "b.png", 512), ["/usr/bin/sips", "-Z", "512", "a.png", "--out", "b.png"])
def test_generator_cmd_linux(self):
    with mock.patch.object(common, "IS_MAC", False):
        cmd = common.pack_generator_cmd(pathlib.Path("/r/rom.z64"), pathlib.Path("/tmp/x"))
        self.assertEqual(cmd[1:], ["--rom", "/r/rom.z64", "--res", str(common.SRC / "build" / "us_pc")])
        self.assertTrue(cmd[0].endswith("sm64ar_pack_tool"))
def test_make_program(self):
    with mock.patch("shutil.which", lambda n: "/x/gmake" if n == "gmake" else None):
        self.assertEqual(common.make_program(), "gmake")
    with mock.patch("shutil.which", lambda n: None):
        self.assertEqual(common.make_program(), "make")
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest discover -s scripts/tests -v 2>&1 | tail -3`
Expected: `AttributeError: module 'sm64ar_common' has no attribute 'resize_png_cmd'`.

- [ ] **Step 3: Implement the helpers in `sm64ar_common.py`; port `sm64_oracle.py` and `packs.py` to them**

`sm64_oracle.py`: replace `missing_formulae()` with `tool_problems()`, `"gmake"` with `make_program()`; on Linux pass `CPP=clang -E -P -x c` as well (the clang shell has no `cpp`) and keep `CP=cp`. `packs.py`: `_sips` becomes `_resize_png(src, dst, cap)` running `resize_png_cmd`; the `subprocess.run([str(harness), "--offscreen", ...])` call becomes `pack_generator_cmd(rom, gen_out) + ["--generate-tex", str(png_dir), str(out)]` (and the same for the actors path); `BSDTAR = shutil.which("bsdtar") or "/usr/bin/bsdtar"`. Keep every check and `Refused` as it is.

- [ ] **Step 4: Run the Python tests**

Run: `python3 -m unittest discover -s scripts/tests -v`
Expected: all OK (9 tests).

- [ ] **Step 5: Build the oracle on Linux**

Run: `python3 scripts/sm64_oracle.py`
Expected: it prints the path of `build/_src/sm64/build/us_pc/sm64coopdx`, and `ls build/_src/sm64/build/us_pc/include/level_headers.h build/_src/sm64/build/us_pc/{lang,dynos,palettes,mods}` lists all five. If the upstream Makefile needs a tool the shell lacks, add it to `flake.nix` (and to `tool_problems`) rather than installing it elsewhere.

- [ ] **Step 6: Add the embedded (non-Apple) branch to `games/sm64/CMakeLists.txt`**

Replace `if(SM64AR_VISIONOS) ... else() ...` with three cases: `SM64AR_VISIONOS` exactly as today; `elseif(NOT APPLE)` the same exclusions, SDL stub, `SM64_ADAPTER_EXTRA adapter/src/link_stubs_visionos.c`, `OSX_BUILD=1` removed, `SM64_PLATFORM_DEFINES ""`, `SM64_PLATFORM_LIBS` = `z m dl Threads::Threads` on Linux and `z log android` on Android, and the sdl_stub include on the headers target; `else()` the macOS harness branch as today. `miniaudio.c` keeps its defaults off Apple (its backends are opened with `dlopen`, nothing is linked). If upstream code behind the `OSX_BUILD`/`TARGET_*` macros fails to compile on Linux, add the smallest fix as patch `0015-Linux-and-Android-build.patch` under `games/sm64/patches` (through `sm64_prepare.py`'s `git am` flow), never by editing `build/_src`.

- [ ] **Step 7: Write the pack tool**

`apps/pack_tool/main.cpp`: parse the arguments from Interfaces; `ar_adapter_sm64_set_resource_dir(res)`; `mkdtemp` a save under `/tmp/sm64ar-gen-XXXXXX`; `ar_adapter_sm64()->init(rom, save)`, on failure print `error: <last_error()>` and exit 1; `sm64_host_generate_tex(in, out)` and/or `sm64_host_generate_actors(dir)`; print `AR_GENERATED tex=<out> actors=<dir>`; remove the save; exit 0. `apps/pack_tool/CMakeLists.txt`: `add_executable(sm64ar_pack_tool main.cpp)` linking `sm64_adapter ar_core`, output directory `${CMAKE_BINARY_DIR}/pack_tool`.

- [ ] **Step 8: Build and test the tool**

Run: `cmake -S . -B build/host -G Ninja -DSM64AR_SM64=ON -DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR -DSM64AR_GLSLANG_DIR=$SM64AR_GLSLANG_DIR && cmake --build build/host && ctest --test-dir build/host --output-on-failure`
Expected: `sm64_game`, `sm64_adapter`, `sm64ar_pack_tool` build; every test still passes.

Run: `build/host/pack_tool/sm64ar_pack_tool --rom /nonexistent.z64 --res build/_src/sm64/build/us_pc --generate-tex /tmp/none /tmp/sm64ar-out; echo "exit=$?"; ls /tmp/sm64ar-out 2>&1`
Expected: a line starting `error:` on stderr, `exit=1`, and `/tmp/sm64ar-out` does not exist.

- [ ] **Step 9: Prepare a real pack on Linux**

Run (ROM at `local/baserom.us.z64`): `python3 -I scripts/packs.py fetch render96-textures && python3 -I scripts/packs.py prepare render96-textures && python3 -I scripts/packs.py verify render96-textures`
Expected: `prepare` ends without `Refused`, the `.expected.json` is written and `verify` reports the pack's `.tex` count with no missing files. Then `python3 -I scripts/packs.py fetch render96-models && python3 -I scripts/packs.py prepare render96-models` likewise (`--generate-actors`).

- [ ] **Step 10: Commit (two commits)**

```bash
git add games/sm64/CMakeLists.txt games/sm64/patches scripts/sm64_oracle.py scripts/sm64ar_common.py scripts/packs.py CMakeLists.txt
git commit -m "shared: sm64 library and oracle build on Linux; pack helpers pick their tools per host"
git add apps/pack_tool scripts/tests/test_packs.py
git commit -m "quest: Linux pack tool runs DynOS's generators headlessly"
```

---

## Done when

- `nix develop` (or `direnv allow`) gives a shell in which `cmake -S . -B build/host -G Ninja -DSM64AR_OPENXR_DIR=$SM64AR_OPENXR_DIR -DSM64AR_GLSLANG_DIR=$SM64AR_GLSLANG_DIR && cmake --build build/host && ctest --test-dir build/host` passes with `test_runtime_cube`, `test_qx_convert`, `test_glsl_emitter`, `test_vulkan_cube` and `python3 -m unittest discover -s scripts/tests` passes.
- `python3 scripts/quest_build.py --launch` installs and starts the app on a connected Quest 3S, which shows the cube on a fixed table in passthrough with head tracking and survives menu/doff/don.
- `python3 -I scripts/packs.py prepare render96-textures` and `... render96-models` succeed on Linux.
- `git log --oneline` shows the `shared:` commits separately, listing every file the Mac check must cover.

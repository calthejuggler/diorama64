# Quest 3S device checklist

Checks that could not run during implementation because no headset was connected. Run them in order from the
Nix dev shell (`direnv allow` once) with the Quest 3S in developer mode and USB debugging allowed. Each item names
the plan task it verifies.

## Plan 1

- [ ] **Task 3 (app skeleton)**: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
      Expected: the APK installs; logcat shows `AR_QUEST_START` and `APP_CMD_*` lines under tag `Diorama64`;
      the headset shows a black app window without crashing.
- [ ] **Task 5 (OpenXR session)**: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
      Expected: `AR_QUEST_SPACE stage=1`, `AR_QUEST_REFRESH requested=90 got=90` (or 72), `AR_QUEST_SWAPCHAIN ...`,
      then `AR_QUEST_HEAD x y z` lines that change as you move; plain passthrough with no black frame; Meta button
      then return resumes the head lines. If `AR_QUEST_XR xrCreateInstance failed: XR_ERROR_RUNTIME_UNAVAILABLE`
      appears, stop: that is the Khronos-loader blocker the design names; report it rather than swapping loaders.
- Note (Task 6): the table-shadow layer draws nothing on Quest until plan 2 ports the room-lit shader helpers; a missing table shadow in plan-1 builds is expected, not a bug.
- [ ] **Task 8 (cube in passthrough)**: `python3 scripts/quest_build.py --launch && python3 scripts/quest_device.py logcat`
      Expected: `AR_RENDER layered=1 target=<w>x<h>x2 samples=1`, `AR_FIRST_FRAME`, no `AR_QUEST_FAIL`; a 40 cm
      coloured cube stands 1 m in front of the stage origin at table height, world-locked as you walk around it, each
      face its own colour (red/green/blue/yellow/white/grey), correct occlusion between faces, no judder. Meta button,
      wait 5 s, return: the cube is still there and the log resumes. Headset off 10 s and on again: same.
- [ ] **Task 8 extra checks (from review)**: watch the first second after launch for a flash or dark veil before
      `AR_FIRST_FRAME` (none expected); grep logcat for `xrBeginFrame (retry)` across Meta-button and headset-off/on
      cycles and confirm the frame loop never stalls; confirm `AR_QUEST_STOP ticks=` is non-zero on exit.
- [ ] **Plan 1 final-review additions**: logcat shows `AR_QUEST_SWAPCHAIN ... format=43 linearize=1`; cube faces are
      neither washed out nor too dark and have no dark/bright fringes at the silhouette. Cover the cameras: the cube
      stays world-locked or freezes, never snaps to the origin. With a stationary boundary: `AR_QUEST_SPACE stage=0`
      and the cube about 0.75 m below eye level. Quit from the Quest menu: `AR_QUEST_STOP`, no ANR; relaunch logs the
      pipeline cache size. Grep logcat for `AR_VULKAN` failures and repeated `AR_QUEST_XR` lines (expect none). If
      `xrInitializeLoaderKHR`/`xrCreateInstance` fails, retry once with `XR_KHR_loader_init_android` enabled before
      calling it the loader blocker.
- Note (Mac check, plan 3): diff the shared files over the whole range (`git diff 7189b65..HEAD -- CMakeLists.txt
  cmake runtime/CMakeLists.txt games/sm64/CMakeLists.txt runtime/platform/visionos/vos_log.cpp games/sm64/adapter scripts`),
  not only the `shared:` commits.

## Plan 2

- [ ] **Task 2 (MSAA)**: logcat shows `AR_MSAA samples=4` (or 2). If it shows `AR_VULKAN depth_resolve=0` the Adreno driver
      lacks `VK_RESOLVE_MODE_MAX_BIT` for depth and the app runs at 1 sample; report it. With MSAA on, check texture-edge
      cutouts (tree cards, fences) for alpha fringes against passthrough (alpha-to-one is not enabled).
- [ ] **Task 4 (sm64 boots)**: `python3 scripts/quest_build.py --rom local/baserom.us.z64 --launch && python3 scripts/quest_device.py logcat`
      Expected: `AR_ADAPTERS sm64=...`, `AR_SCREEN ...` lines, `AR_RENDER layered=1 ...`, no `AR_QUEST_FAIL`; Bob-omb
      Battlefield (the level start) stands on the fixed table, Mario idle, at the display rate. Note the `AR_PERF`/
      `AR_HITCH` lines for plan 3's performance pass.
- [ ] **Task 5 (session parity)**: on launch logcat shows `AR_WARMUP` then `AR_MSAA samples=4` (or what the device
      supports); the room-lit look shows shadows and AO on the table; no `AR_HITCH` storm while idle. Try
      `adb shell am start -n org.pagetable.diorama64/android.app.NativeActivity --es look "msaa=2,shadows=objects"`
      and confirm `AR_LOOK msaa=2` / `AR_LOOK shadows=objects` lines and the visible change.
- [ ] **Task 6 (Touch controllers)**: logcat shows `AR_INPUT profile=touch attached=1`; Mario walks with the left stick,
      jumps with A, punches with B, crouches with the left trigger. Launch with
      `--es game sm64 --es start story`: the title screen appears; point the right controller at the file select and
      pull the trigger to pick a file (`AR_POINTER begin/end` and `AR_TOUCH` lines appear). Press the left menu
      button: `AR_PANEL toggle open=1` (the panel itself arrives in plan 3).
- [ ] **Task 7 (table from the room scan)**: after Space Setup with a table, the level appears on the real table at the
      right height (`AR_QUEST_SCENE tables=N chosen=<uuid>` in logcat). Hold a grip and move the controller: the level
      slides along the surface and stops at the table's edge; the right stick turns it; release; restart the app: the
      placement is kept. With a second scanned table, relaunch with
      `adb shell am start -S -n org.pagetable.diorama64/android.app.NativeActivity --ez replace true`
      and the level moves to the table you stand next to (`--ez scan true` requests Space Setup instead). Revoke the scene permission in Settings: the hint is logged
      (`AR_QUEST_SCENE permission=denied`) and passthrough stays up.
- [ ] **Task 8 (depth submission)**: the level stays locked to the table during fast head turns (positional reprojection
      now has depth); `adb logcat` shows no `XR_ERROR_*` lines.

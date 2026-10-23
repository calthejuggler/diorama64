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

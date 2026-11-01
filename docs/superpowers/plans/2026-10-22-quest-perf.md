# Quest 3S performance pass (Plan 3, Task 8)

Filled in from headset sessions. `kQuestLookSeed` in `apps/quest/quest_app.cpp` holds the first-run Look values; the
logcat shows `AR_LOOK seed k=v` at startup for each key the player has not stored yet.

## Measurement procedure

With the default Look on Bob-omb Battlefield, Castle grounds and Jolly Roger Bay, record the `AR_PERF` gpu_ms p50/p95
over 60 s each at 90 Hz (budget 11.1 ms; 13.9 ms at 72 Hz). The line is logged every 900 frames (10 s at 90 Hz) and
carries `gpu_ms p50= p95= max=`, `hz=`, `cpu_ms p50= p95= max=`, plus `late=` (late passes), `deferred=` (deferred
draws) and `gpu_last_ms=`. Read it with `python3 scripts/quest_device.py logcat`. Try the alternatives with
`adb shell am start -S -n org.pagetable.diorama64/android.app.NativeActivity --es look "msaa=2"` (and `ao=0`,
`shadows=objects`).

## Results

| Level | Settings | p50 gpu_ms | p95 gpu_ms | Refresh |
|---|---|---|---|---|
| | | | | |

## Decision rule

If p95 fits 90 Hz, `kQuestLookSeed` stays empty. Else try, each alone and in this order, `msaa=2`, `ao=0`,
`shadows=objects`, and measure again; pick the smallest set that fits 90 Hz at p95 and put it in `kQuestLookSeed`
(`apps/quest/quest_app.cpp`). If none fits, seed `msaa=2` and `ao=0` and set `Config.refresh_hz = 72`, and say so in
the README.

## Chosen seed

Chosen seed: `mode=original` (2026-11-01).

First headset session (Quest 3S, 2026-10-31, default packs including Render96 Models): on the file select the game ran
at 90 Hz with gpu_ms p50 1.9 / p95 3.2. Inside the castle with Room-lit it fell to about 7 Hz: each frame ran four
catch-up ticks with step_ms about 134 (about 33 ms per tick, CPU in the game step; the GPU was not the limit). Turning off
Draw all rooms did not help; switching Look to Original N64 did, which points at Room-lit's per-tick smooth-normal
derivation. Not yet profiled: the APK is now `profileable` so `adb shell simpleperf record --app org.pagetable.diorama64`
works on the next session. MSAA is 1 on this device (`AR_VULKAN depth_resolve=0`).

## visionOS check (the Mac, Xcode 26)

- Commit checked: 
- What was run: 
- Outcome: 

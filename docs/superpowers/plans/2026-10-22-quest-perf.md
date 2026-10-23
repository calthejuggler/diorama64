# Quest 3S performance pass (Plan 3, Task 8)

Filled in by the user from a headset session. Until then `kQuestLookSeed` in `apps/quest/quest_app.cpp` is empty (the
defaults are unchanged) and the logcat shows `AR_LOOK seed none` at startup.

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

Chosen seed: (none measured yet)

## visionOS check (the Mac, Xcode 26)

- Commit checked: 
- What was run: 
- Outcome: 

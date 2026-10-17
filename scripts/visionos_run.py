#!/usr/bin/env python3
"""scripts/visionos_run.py — install SM64AR.app in the visionOS simulator, stage files into its Documents, launch it
with arguments, and take screenshots when log markers appear. Output goes to --out (keep it under local/).
The app carries the ROM and the packs in its bundle (visionos_build.py); they are never staged.

  python3 scripts/visionos_run.py --out local/first-frame --shot 'AR_FIRST_FRAME:first.png' -- --autostart
  python3 scripts/visionos_run.py --out local/bob --stage local/route/bob.sarec:bob.sarec \\
      --shot 'AR_HOLD tick=300:bob.png' --video bob.mp4 -- --autostart --game sm64 --hold-ticks 300 --sarec bob.sarec

A --shot whose marker is an 'AR_HOLD tick=N' line releases that hold (Documents/resume-N) after the screenshot.
"""
import argparse, pathlib, re, shutil, subprocess, sys, time
import sim_util
from visionos_build import BUILD, find_app

LOG = "sm64ar.log"


def parse_pair(text, what):
    left, sep, right = text.rpartition(":")
    if not sep or not left or not right:
        raise ValueError(f"bad {what} {text!r} (use LEFT:RIGHT)")
    return left, right


def bundled_target(dst):
    parts = pathlib.PurePosixPath(dst).parts
    return dst == "rom.z64" or parts[:3] == ("save", "dynos", "packs")


def run_scenario(out, app_args, shots=(), stage=(), wait=None, video=None, fresh=False, timeout=120.0, settle=1.0,
                 config="Debug", keep_running=False, shot_before=None, pull=(), app=None):
    out = pathlib.Path(out)
    bad = [dst for _, dst in stage if bundled_target(dst)]
    if bad:
        raise ValueError(f"cannot stage {', '.join(bad)}: the app reads the ROM and packs only from its bundle "
                         "(visionos_build.py puts them there)")
    out.mkdir(parents=True, exist_ok=True)
    udid, state = sim_util.device()
    sim_util.boot(udid, state)
    if shot_before:
        sim_util.screenshot(udid, out / shot_before)
    if fresh:
        sim_util.uninstall(udid)
    sim_util.install(udid, app or find_app(BUILD, config))
    docs = sim_util.data_container(udid) / "Documents"
    docs.mkdir(parents=True, exist_ok=True)
    for src, dst in stage:
        target = docs / dst
        if pathlib.Path(src).is_dir():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(src, target, dirs_exist_ok=True, symlinks=False)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, target)
    log = docs / LOG
    log.unlink(missing_ok=True)
    for old in docs.glob("resume-*"):
        old.unlink()
    rec = sim_util.Recorder(udid, out / video) if video else None
    results = {}
    try:
        sim_util.launch(udid, list(app_args))
        pos = 0
        for marker, name in shots:
            m, pos = sim_util.wait_for_log(log, marker, timeout, pos)
            time.sleep(settle)
            sim_util.screenshot(udid, out / name)
            results[name] = m.group(0)
            hold = re.search(r"AR_HOLD tick=(\d+)", m.group(0))
            if hold:
                (docs / f"resume-{hold.group(1)}").touch()
        if wait:
            m, pos = sim_util.wait_for_log(log, wait, timeout, pos)
            results["wait"] = m.group(0)
    finally:
        try:
            if rec:
                rec.stop()
        finally:
            if not keep_running:
                sim_util.terminate(udid)
            if log.exists():
                shutil.copyfile(log, out / f"{out.name}.log")
            for name in pull:
                if (docs / name).exists():
                    shutil.copyfile(docs / name, out / name)
    text = (out / f"{out.name}.log").read_text(errors="replace") if (out / f"{out.name}.log").exists() else ""
    if "AR_FIRST_FRAME" in text:
        p = sim_util.check_projection(text)
        results["projection"] = " ".join(f"{k}={v:g}" for k, v in p.items())
    return results


def main(argv):
    app_args = []
    if "--" in argv:
        i = argv.index("--")
        argv, app_args = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--shot", action="append", default=[], help="REGEX:FILE — screenshot when a log line matches")
    ap.add_argument("--shot-before", help="FILE — screenshot of the booted simulator before launching")
    ap.add_argument("--stage", action="append", default=[], help="HOST_FILE_OR_DIR:NAME — copy into the app's Documents (a directory "
                    "recursively; never the ROM or packs: the app has them in its bundle)")
    ap.add_argument("--wait", help="REGEX — wait for this line after the shots")
    ap.add_argument("--video", help="FILE — record the whole run")
    ap.add_argument("--fresh-container", action="store_true", help="uninstall first (clears Documents)")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--keep-running", action="store_true")
    ap.add_argument("--pull", action="append", default=[], help="NAME — copy Documents/NAME into --out after the run")
    a = ap.parse_args(argv)
    try:
        res = run_scenario(a.out, app_args, [parse_pair(s, "--shot") for s in a.shot],
                           [parse_pair(s, "--stage") for s in a.stage], a.wait, a.video, a.fresh_container, a.timeout,
                           keep_running=a.keep_running, shot_before=a.shot_before, pull=a.pull)
    except (TimeoutError, RuntimeError, ValueError, subprocess.TimeoutExpired) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for k, v in res.items():
        print(f"{k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

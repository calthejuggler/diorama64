#!/usr/bin/env python3
"""scripts/pack_costs.py — measure what each model of a variant's source packs costs, for
`packs.py prepare render96-models-balanced` (the per-actor mix of two prepared model packs).

  python3 scripts/pack_costs.py [--ticks N]     # -> local/packs/actor_costs.json (packs.COSTS)

For packs.COST_LEVELS: the drawn instances of every curated actor (no packs), then for each source pack of every variant
and each of its bins drawn in that level, a one-actor pack: the vertices it adds to the frame. The source packs'
.verified.json hashes pin the result to their files. Runs the macOS harness offscreen (SM64AR_HARNESS=path picks a build;
default build/macos-rel, else build/macos). Pack data is only ever read by the harness, never executed or imported here.
"""
import json, os, pathlib, re, shutil, subprocess, sys, tempfile
import packs as pk

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROM = ROOT / "local" / "baserom.us.z64"
PACKS = ROOT / "local" / "packs"
COST_TICKS = 300


def mem(out):
    return [{k: float(v) for k, v in re.findall(r"(\w+)=([-\d.]+)", m)} for m in re.findall(r"^AR_MEM (.*)$", out, re.M)]


def actors(out):
    m = re.search(r"^AR_ACTORS(.*)$", out, re.M)
    return {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", m.group(1)) if int(v) > 0} if m else {}


def default_harness():
    env = os.environ.get("SM64AR_HARNESS")
    if env:
        return env
    rel = ROOT / "build" / "macos-rel" / "harness" / "bin" / "sm64ar_harness"
    return str(rel if rel.exists() else ROOT / "build" / "macos" / "harness" / "bin" / "sm64ar_harness")


def costs_run(harness, level, save, models, names, ticks):
    out_dir = pathlib.Path(tempfile.mkdtemp(prefix="sm64ar-costs-"))
    try:
        r = subprocess.run([str(c) for c in [harness, "--offscreen", "--out", out_dir, "--rom", ROM, "--save", save, "--level", level,
                            "--warmup-ticks", ticks, "--frames", "1", "--png-every", "0", "--mem-stats", "--look", "mode=roomlit",
                            "--models", models, "--actor-counts", ",".join(names)]], capture_output=True, text=True)
    finally:
        shutil.rmtree(out_dir, ignore_errors=True)
    lines = mem(r.stdout)
    if r.returncode != 0 or not lines or "frame_verts" not in lines[-1]:
        raise RuntimeError(r.stdout[-1500:] + r.stderr[-1500:])
    return int(lines[-1]["frame_verts"]), actors(r.stdout)


def costs(ticks=COST_TICKS):
    harness = default_harness()
    m = pk.load_manifest()
    names = list(pk.CURATED) + list(pk.BOWSER)
    sources = sorted({v[k] for v in pk.VARIANTS.values() for k in ("base", "fallback") if v[k] in m})
    res = {"levels": list(pk.COST_LEVELS), "ticks": ticks, "harness": pathlib.Path(harness).parent.parent.parent.name,
           "base_verts": {}, "instances": {}, "extra": {}, "sources": {}}
    for pid in sources:
        e = m[pid]
        v = PACKS / pid / "prepared" / f"{e['name']}.verified.json"
        if not v.is_file():
            raise RuntimeError(f"{e['name']}: not prepared (python3 -I scripts/packs.py prepare {pid})")
        res["sources"][e["name"]] = pk.sha256_file(v)
    empty = pathlib.Path(tempfile.mkdtemp(prefix="sm64ar-costs-save-"))
    try:
        for level in pk.COST_LEVELS:
            base, inst = costs_run(harness, level, empty, "original", names, ticks)
            res["base_verts"][level], res["instances"][level] = base, inst
            for pid in sources:
                e = m[pid]
                folder = PACKS / pid / "prepared" / e["name"]
                for actor in sorted(a for a in inst if (folder / f"{a}.bin").is_file()):
                    save = pathlib.Path(tempfile.mkdtemp(prefix="sm64ar-costs-save-"))
                    try:
                        (save / "dynos" / "packs" / "One").mkdir(parents=True)
                        (save / "dynos" / "packs" / "One" / f"{actor}.bin").symlink_to(folder / f"{actor}.bin")
                        verts, _ = costs_run(harness, level, save, "3d:One", names, ticks)
                    finally:
                        shutil.rmtree(save, ignore_errors=True)
                    res["extra"].setdefault(e["name"], {}).setdefault(actor, {})[level] = verts - base
                    print(f"{level} {e['name']} {actor} x{inst[actor]}: +{verts - base} vertices", flush=True)
    finally:
        shutil.rmtree(empty, ignore_errors=True)
    dest = pk.COSTS
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(res, indent=1, sort_keys=True) + "\n")
    print(f"wrote {dest}")
    return 0


def main(argv):
    if argv and not (len(argv) == 2 and argv[0] == "--ticks" and argv[1].isdigit() and int(argv[1]) > 0):
        print(__doc__)
        return 2
    try:
        return costs(int(argv[1]) if argv else COST_TICKS)
    except RuntimeError as e:
        print(e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

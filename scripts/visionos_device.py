#!/usr/bin/env python3
"""scripts/visionos_device.py — for your own Apple Vision Pro (./build.py runs build and install for you).
The build signs with your team and carries your ROM and packs inside the app; the other commands talk to a paired headset.

  python3 scripts/visionos_device.py devices
  python3 scripts/visionos_device.py build --team ABCDE12345 --bundle-id com.yourname.diorama64
  python3 scripts/visionos_device.py install --device "Your Vision Pro"
  python3 scripts/visionos_device.py launch --device "Your Vision Pro" --bundle-id com.yourname.diorama64 [--terminate-existing] [-- --record session.sarec]
  python3 scripts/visionos_device.py log --device "Your Vision Pro" --bundle-id com.yourname.diorama64 --out local/device
"""
import argparse, hashlib, json, pathlib, re, subprocess, sys
from sm64ar_common import ROOT, env

N64_MAGIC = bytes([0x80, 0x37, 0x12, 0x40])


def valid_team(t):
    return bool(re.fullmatch(r"[A-Z0-9]{10}", t or ""))


def valid_bundle_id(b):
    return bool(re.fullmatch(r"[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+", b or ""))


def devices_cmd():
    return ["xcrun", "devicectl", "list", "devices"]


def build_cmd(team, bundle_id):
    return [sys.executable, ROOT / "scripts" / "visionos_build.py", "--device", "--team", team, "--bundle-id", bundle_id,
            "--config", "Release"]


def install_cmd(device, app):
    return ["xcrun", "devicectl", "device", "install", "app", "--device", device, str(app)]


def launch_cmd(device, bundle_id, args, terminate_existing=False):
    # --terminate-existing goes before the bundle id (after it, everything is the app's own arguments).
    return ["xcrun", "devicectl", "device", "process", "launch", "--device", device,
            *(["--terminate-existing"] if terminate_existing else []), bundle_id, *args]


def log_cmd(device, bundle_id, out_dir):
    return ["xcrun", "devicectl", "device", "copy", "from", "--device", device, "--domain-type", "appDataContainer",
            "--domain-identifier", bundle_id, "--source", "Documents/sm64ar.log", "--destination", f"{out_dir}/sm64ar.log"]


def check_rom(path):
    p = pathlib.Path(path)
    if not p.is_file():
        raise ValueError(f"{p}: no such file (./build.py puts your ROM there, in any byte order)")
    with open(p, "rb") as f:
        head = f.read(4)
    if head != N64_MAGIC:
        raise ValueError(f"{p}: not a .z64 ROM (./build.py converts any byte order)")
    return p


PACK_NAME = re.compile(r"[A-Za-z0-9 ._()-]+")


def valid_pack_name(n):
    return bool(n) and PACK_NAME.fullmatch(n) is not None and n[0] not in ".-"


def reverify_cmd(src):
    return [sys.executable, "-I", ROOT / "scripts" / "packs.py", "verify", pathlib.Path(src).parent.parent.name]


STAMP = ".sm64ar-verified"
SHA256 = re.compile(r"[0-9a-f]{64}")


def verified_manifest(src):
    src = pathlib.Path(src)
    v = src.parent / f"{src.name}.verified.json"
    if not v.is_file():
        raise ValueError(f"{src}: not verified (run: python3 -I scripts/packs.py verify ID)")
    return json.loads(v.read_text())["files"]


def check_files(src, want):
    """Every file of the folder, re-hashed against want; anything else (an extra, missing or changed file) refuses."""
    src = pathlib.Path(src)
    files = sorted(src.iterdir())
    if [p.name for p in files] != sorted(want):
        raise ValueError(f"{src}: files differ from the verified list")
    for p in files:
        if p.suffix not in (".tex", ".bin") or p.is_symlink() or not p.is_file() \
                or hashlib.sha256(p.read_bytes()).hexdigest() != want[p.name]:
            raise ValueError(f"{p}: changed since it was verified")
    return files


def check_verified(src):
    return check_files(src, verified_manifest(src))


def write_stamp(dest, want):
    """dest/.sm64ar-verified: "<sha256>  <name>" per verified file, sorted. On the device a pack loads only when its stamp
    exists and every listed file re-hashes to it; files it does not list are never loaded."""
    for name, h in want.items():
        if not (isinstance(h, str) and SHA256.fullmatch(h)) or "/" in name or name.startswith(".") \
                or pathlib.PurePath(name).suffix not in (".tex", ".bin"):
            raise ValueError(f"{name!r}: not a stageable verified entry")
    (pathlib.Path(dest) / STAMP).write_text("".join(f"{want[n]}  {n}\n" for n in sorted(want)))


def snapshot(src, tmp):
    """Copies the prepared folder and its verified.json under tmp/snap and re-hashes the COPY, so what is bundled is exactly what was checked; then writes the staging stamp into the copy."""
    src = pathlib.Path(src)
    snap = pathlib.Path(tmp) / "snap" / src.name
    snap.mkdir(parents=True)
    for p in sorted(src.iterdir()):
        if p.is_symlink() or not p.is_file():
            raise ValueError(f"{p}: not a regular file")
        (snap / p.name).write_bytes(p.read_bytes())
    (snap.parent / f"{src.name}.verified.json").write_bytes((src.parent / f"{src.name}.verified.json").read_bytes())
    check_verified(snap)
    write_stamp(snap, verified_manifest(snap))
    return snap


def main(argv):
    extra = []
    if "--" in argv:
        i = argv.index("--"); argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["devices", "build", "install", "launch", "log"])
    ap.add_argument("--device"); ap.add_argument("--team"); ap.add_argument("--bundle-id")
    ap.add_argument("--out", default=str(ROOT / "local" / "device"))
    ap.add_argument("--config", default="Release")
    ap.add_argument("--terminate-existing", action="store_true", help="launch: quit a running copy of the app first")
    a = ap.parse_args(argv)
    if a.command == "build":
        if not (valid_team(a.team) and valid_bundle_id(a.bundle_id)):
            ap.error("build needs --team (10 characters, A-Z 0-9) and --bundle-id (like com.yourname.diorama64)")
        cmd = build_cmd(a.team, a.bundle_id)
    elif a.command == "devices":
        cmd = devices_cmd()
    else:
        if not a.device:
            ap.error(f"{a.command} needs --device (see: visionos_device.py devices)")
        if a.command == "install":
            from visionos_build import BUILD_DEVICE, find_app
            try:
                app = find_app(BUILD_DEVICE, a.config, device=True)
            except RuntimeError as e:
                ap.error(f"{e}: run the build command first")
            cmd = install_cmd(a.device, app)
        else:
            if not valid_bundle_id(a.bundle_id):
                ap.error(f"{a.command} needs --bundle-id (the one you built with)")
            if a.command == "launch":
                cmd = launch_cmd(a.device, a.bundle_id, extra, a.terminate_existing)
            else:
                pathlib.Path(a.out).mkdir(parents=True, exist_ok=True)
                cmd = log_cmd(a.device, a.bundle_id, a.out)
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], env=env()).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

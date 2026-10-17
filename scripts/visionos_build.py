#!/usr/bin/env python3
"""scripts/visionos_build.py — configure and build for visionOS (CMake Xcode generator), then check that the product really
is the expected slice (lipo cannot tell iOS, xrOS and their simulators apart). It first prepares the sources
(sm64_prepare, fetch_lua, sm64_oracle; each skipped when up to date). ./build.py runs this for you.

  python3 scripts/visionos_build.py                    # simulator app (target SM64AR), Debug -> build/xrsim
  python3 scripts/visionos_build.py --target sm64_adapter
  python3 scripts/visionos_build.py --device           # device app, unsigned (no Apple account) -> build/xros
  python3 scripts/visionos_build.py --device --team ABCDE12345 --bundle-id com.yourname.diorama64   # signed
  python3 scripts/visionos_build.py --packs "SM64 Reloaded,Render96 Models"   # only these prepared packs (default: all)

The app (target SM64AR) always carries your data in SM64AR.app/Bundled (scripts/bundle_local.py): your ROM and the
prepared packs. The app reads them only there.
"""
import argparse, pathlib, re, shutil, subprocess, sys
from sm64ar_common import ROOT, env, run

DEFAULT_BUNDLE_ID = "com.local.sm64ar"

BUILD = ROOT / "build" / "xrsim"
BUILD_DEVICE = ROOT / "build" / "xros"
XROS_SIMULATOR, XROS = 12, 11


def configure_cmd(build_dir, device=False, team=None, bundle_id=None, bundled_dir=None):
    cmd = ["cmake", "-S", ROOT, "-B", build_dir, "-G", "Xcode",
           "-DCMAKE_SYSTEM_NAME=visionOS", f"-DCMAKE_OSX_SYSROOT={'xros' if device else 'xrsimulator'}",
           "-DCMAKE_OSX_ARCHITECTURES=arm64",
           "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0", "-DCMAKE_XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET=26.0",
           # Libraries never sign; only the app target signs, with SM64AR_TEAM_ID (apps/visionos/CMakeLists.txt).
           "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO",
           "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY", "-DSM64AR_TESTS=OFF", f"-DSM64AR_TEAM_ID={team or ''}",
           # Always: a cached id from an earlier signed configure would otherwise stick to a later unsigned one.
           f"-DSM64AR_BUNDLE_ID={bundle_id or DEFAULT_BUNDLE_ID}",
           f"-DSM64AR_BUNDLED_DIR={bundled_dir or ''}"]
    return cmd


def xcode_args(team):
    return ["-quiet"] + (["-allowProvisioningUpdates", "-allowProvisioningDeviceRegistration"] if team else [])


def parse_build_platform(otool_text):
    m = re.search(r"cmd LC_BUILD_VERSION\s+cmdsize\s+\d+\s+platform\s+(\d+)", otool_text)
    return int(m.group(1)) if m else None


def find_product(build_dir, config, name, device=False):
    sdk = "xros" if device else "xrsimulator"
    found = sorted(pathlib.Path(build_dir).glob(f"**/{config}-{sdk}/{name}"))
    if len(found) != 1:
        raise RuntimeError(f"expected one {config}-{sdk}/{name} under {build_dir}, found {[str(f) for f in found]}")
    return found[0]


def find_app(build_dir, config, device=False):
    return find_product(build_dir, config, "SM64AR.app", device)


def references_atan2f(nm_undefined_text):
    """True if `nm -u` output names _atan2f (libsm64_game.a's own version would replace libm's)."""
    return "_atan2f" in nm_undefined_text.split()


ROM_SUFFIXES = (".z64", ".v64", ".n64")


def bundle_archives(app_dir, skip_bundled=False):
    app_dir = pathlib.Path(app_dir)
    return sorted(p for p in app_dir.rglob("*") if p.suffix.lower() in ROM_SUFFIXES
                  and not (skip_bundled and p.relative_to(app_dir).parts[0] == "Bundled"))


XCODE_NOISE = 'DVTDeviceOperation: Encountered a build number "" that is incompatible with DVTBuildVersion'


def build_quietly(cmd):
    """xcodebuild repeats a harmless device-list warning every few seconds; everything else is passed through."""
    p = subprocess.Popen([str(c) for c in cmd], env=env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in p.stdout:
        if XCODE_NOISE not in line:
            sys.stdout.write(line)
    if p.wait():
        raise subprocess.CalledProcessError(p.returncode, cmd)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--target", default="SM64AR")
    ap.add_argument("--no-configure", action="store_true")
    ap.add_argument("--device", action="store_true", help="build for Apple Vision Pro (xros) instead of the simulator")
    ap.add_argument("--team", help="Apple development team ID (with --device and --bundle-id): sign the app")
    ap.add_argument("--bundle-id", help="bundle identifier unique to your team (with --device and --team)")
    ap.add_argument("--packs", help="the prepared packs to bundle, by name, comma-separated (default: every prepared pack; "
                    "\"\" none)")
    a = ap.parse_args(argv)
    app = a.target == "SM64AR"
    names = None if a.packs is None else [n.strip() for n in a.packs.split(",") if n.strip()]
    if (a.team or a.bundle_id) and not a.device:
        ap.error("--team and --bundle-id need --device")
    if bool(a.team) != bool(a.bundle_id):
        ap.error("--team and --bundle-id go together")
    if a.team:
        from visionos_device import valid_bundle_id, valid_team
        if not valid_team(a.team):
            ap.error("--team is 10 characters, A-Z 0-9 (Xcode > Settings > Accounts)")
        if not valid_bundle_id(a.bundle_id):
            ap.error("--bundle-id looks like com.yourname.sm64ar")
    build_dir = BUILD_DEVICE if a.device else BUILD
    prep = [[sys.executable, ROOT / "scripts" / s] for s in ("sm64_prepare.py", "fetch_lua.py", "sm64_oracle.py")]
    for cmd in prep:
        if run(cmd, check=False):
            return 1
    bundled, expected = None, None
    if app:
        import bundle_local
        bundled = build_dir / "bundled-local"
        try:
            expected = bundle_local.stage(bundled, names=names)
        except (ValueError, RuntimeError, OSError) as e:
            print(f"error: the app's data (Bundled/): {e}", file=sys.stderr)
            return 1
    try:   # changed bundled data: the app is rebuilt from scratch, so Xcode re-signs it whole
        old = find_app(build_dir, a.config, a.device) if app else None
    except RuntimeError:
        old = None
    if old and bundle_local.check_app(old, expected):
        print(f"removing {old} (bundled data changes: rebuilt and re-signed whole)")
        shutil.rmtree(old)
    if not a.no_configure:
        run(configure_cmd(build_dir, a.device, a.team, a.bundle_id, bundled))
    build_quietly(["cmake", "--build", build_dir, "--config", a.config, "--target", a.target, "--", *xcode_args(a.team)])
    if a.target == "SM64AR":
        product = find_app(build_dir, a.config, a.device)
        binary = product / "SM64AR"
        # ar_vos is built only for visionOS, so the macOS no_atan2f ctest cannot see it.
        vos = find_product(build_dir, a.config, "libar_vos.a", a.device)
        if references_atan2f(run(["nm", "-u", vos], capture=True)):
            print(f"error: {vos} references atan2f (use double atan2)", file=sys.stderr)
            return 1
        problems = [f"archive or ROM in bundle: {p}" for p in bundle_archives(product, skip_bundled=True)]
        problems += bundle_local.check_app(product, expected)
        if problems:
            print("error: " + "; ".join(problems), file=sys.stderr)
            return 1
    else:
        product = binary = find_product(build_dir, a.config, f"lib{a.target}.a", a.device)
    want = XROS if a.device else XROS_SIMULATOR
    plat = parse_build_platform(run(["otool", "-l", binary], capture=True))
    if plat != want:
        print(f"error: {binary} is platform {plat}, expected {want} ({'XROS' if a.device else 'XROS_SIMULATOR'})", file=sys.stderr)
        return 1
    signing = "" if not a.device else f", signed for team {a.team}" if a.team else ", unsigned"
    print(f"{a.target}: {product} (platform {plat}{signing})")
    if app:
        print(f"bundled: {bundle_local.size_mb(product / 'Bundled'):.0f} MB of your data; app {bundle_local.size_mb(product):.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

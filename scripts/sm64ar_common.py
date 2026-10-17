"""sm64ar_common.py — shared paths and helpers for all tooling scripts."""
import functools, os, pathlib, plistlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PIN = "8cd6e5977d9f920d51ca71f2c61801d019ed79c6"
UPSTREAM = ROOT / "games" / "sm64" / "upstream"
# Overridable so tests can prepare a throwaway copy without touching the live worktree.
SRC = pathlib.Path(os.environ.get("SM64AR_SRC") or ROOT / "build" / "_src" / "sm64")
PATCH_DIR = pathlib.Path(os.environ.get("SM64AR_PATCH_DIR") or ROOT / "games" / "sm64" / "patches")
KEEP = pathlib.Path(os.environ.get("SM64AR_KEEP") or SRC.parent / "sm64-build-keep")
MIN_XCODE, TESTED_XCODE = 26, "26.3"
BUILD_FLAGS = ["DISCORD_SDK=0", "COOPNET=0", "UPDATER=0", "USE_APP=0"]
# Homebrew formulae the build needs (make: GNU make, since upstream's Makefile needs more than macOS's make 3.81).
FORMULAE = ("cmake", "ninja", "make", "sdl2", "glew")


def _selected_xcode():
    """`xcode-select -p` (read only: the build never changes which Xcode is selected)."""
    try:
        r = subprocess.run(["xcode-select", "-p"], capture_output=True, text=True)
    except OSError:
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def developer_folder(path):
    if path and (pathlib.Path(path) / "Contents" / "Developer").is_dir():
        return str(pathlib.Path(path) / "Contents" / "Developer")
    return path


DEVELOPER_DIR = developer_folder(os.environ.get("DEVELOPER_DIR") or _selected_xcode())
BREW_PLACES = ("/opt/homebrew/bin/brew", "/usr/local/bin/brew")


def find_brew(which=shutil.which, places=BREW_PLACES):
    return which("brew") or next((p for p in places if os.access(p, os.X_OK)), None)


def xcode_version(dev):
    try:
        return str(plistlib.loads((pathlib.Path(dev).parent / "version.plist").read_bytes()).get("CFBundleShortVersionString", ""))
    except (OSError, ValueError, plistlib.InvalidFileException):
        return ""


def has_xros_sdk(dev):
    return any((pathlib.Path(dev) / "Platforms" / "XROS.platform" / "Developer" / "SDKs").glob("XROS*.sdk"))


def xcode_check(version, xros_sdk, dev):
    m = re.match(r"(\d+)", version or "")
    if not m:
        return (f"no Xcode at {dev or '(none selected)'}: install Xcode {MIN_XCODE} or newer, then set "
                "DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer (or select it in Xcode > Settings > Locations)"), None
    if int(m.group(1)) < MIN_XCODE:
        return (f"Xcode {version} at {dev} is too old: install Xcode {MIN_XCODE} or newer and set DEVELOPER_DIR to its "
                "Contents/Developer"), None
    if not xros_sdk:
        return f"Xcode {version} at {dev} has no visionOS SDK: install it in Xcode > Settings > Components", None
    return None, (None if version == TESTED_XCODE else f"note: building with Xcode {version} (tested with Xcode {TESTED_XCODE})")


def xcode_problem(dev=None):
    dev = dev or DEVELOPER_DIR
    return xcode_check(xcode_version(dev) if dev else "", bool(dev) and has_xros_sdk(dev), dev)


@functools.lru_cache(maxsize=None)
def require_xcode():
    problem, note = xcode_problem()
    if problem:
        raise SystemExit(f"error: {problem}")
    if note:
        print(note, file=sys.stderr)
    return DEVELOPER_DIR


def env():
    e = dict(os.environ)
    e["DEVELOPER_DIR"] = require_xcode()
    brew = find_brew()
    if brew and str(pathlib.Path(brew).parent) not in e.get("PATH", "").split(":"):
        e["PATH"] = f"{pathlib.Path(brew).parent}:{e.get('PATH', '')}"
    return e

def run(cmd, capture=False, cwd=None, check=True):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env(), check=check, text=True, capture_output=capture)
    return r.stdout if capture else r.returncode

def missing_formulae(names=FORMULAE, brew=None):
    brew = brew or find_brew()
    if not brew or not shutil.which(brew):
        raise RuntimeError("Homebrew is not installed: install it as https://brew.sh says")

    def listed(*n):
        return subprocess.run([brew, "list", "--versions", *n], capture_output=True).returncode == 0
    return [] if listed(*names) else [n for n in names if not listed(n)]

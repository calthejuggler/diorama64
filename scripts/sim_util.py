"""scripts/sim_util.py — drive the visionOS simulator headlessly with xcrun simctl.
Never creates devices or installs runtimes: an existing 'Apple Vision Pro' on a visionOS 26 or newer runtime is required
(the newest runtime that has one)."""
import json, pathlib, re, signal, subprocess, tempfile, threading, time
import sim_image
from sm64ar_common import env, run

BUNDLE_ID = "com.local.sm64ar"
DEVICE_NAME = "Apple Vision Pro"
SETTLE_S = 90.0                   # cold boot: screenshots stay all black for a while after bootstatus
CONTRACT_PROJECTION = {"left": -1.0, "right": 1.0, "up": 0.5625, "down": -0.5625, "near": 0.1}
RUNTIME = re.compile(r"com\.apple\.CoreSimulator\.SimRuntime\.xrOS-(\d+)-(\d+)(?:-(\d+))?$")
MIN_RUNTIME = 26


def simctl(*args, capture=True, check=True):
    return run(["xcrun", "simctl", *args], capture=capture, check=check)


def runtime_version(runtime_id):
    m = RUNTIME.match(runtime_id)
    return tuple(int(g or 0) for g in m.groups()) if m else None


def pick_device(devices_json):
    runtimes = sorted(((runtime_version(r), r) for r in json.loads(devices_json).get("devices", {})
                       if runtime_version(r) and runtime_version(r)[0] >= MIN_RUNTIME), reverse=True)
    devices = json.loads(devices_json)["devices"] if runtimes else {}
    for _, r in runtimes:
        for d in devices[r]:
            if d.get("name") == DEVICE_NAME and d.get("isAvailable", False):
                return d["udid"], d.get("state", "")
    raise RuntimeError(f"no available '{DEVICE_NAME}' on a visionOS {MIN_RUNTIME} or newer simulator runtime: install one "
                       f"in Xcode > Settings > Components and create the device in Window > Devices and Simulators")


def device():
    return pick_device(simctl("list", "devices", "-j"))


def boot(udid, state):
    if state != "Booted":
        simctl("boot", udid)
    simctl("bootstatus", udid, "-b")
    settle(udid)


def is_all_black(png):
    with tempfile.TemporaryDirectory() as d:
        bmp = pathlib.Path(d) / "s.bmp"
        sim_image.to_bmp(png, bmp, width=96)
        _w, _h, rows = sim_image.read_bmp(bmp.read_bytes())
    return sim_image.black_fraction(rows) >= 0.999


def wait_until_not_black(take, is_black, timeout, poll=1.0):
    deadline = time.time() + timeout
    while True:
        if not is_black(take()):
            return
        if time.time() > deadline:
            raise TimeoutError(f"the simulator display is still all black {timeout:.0f} s after boot")
        time.sleep(poll)


def settle(udid, timeout=SETTLE_S):
    with tempfile.TemporaryDirectory() as d:
        png = pathlib.Path(d) / "settle.png"

        def take():
            screenshot(udid, png)
            return png
        wait_until_not_black(take, is_all_black, timeout)


def shutdown(udid):
    simctl("shutdown", udid, check=False)


def install(udid, app):
    simctl("install", udid, str(app))


def uninstall(udid):
    simctl("uninstall", udid, BUNDLE_ID, check=False)


def data_container(udid):
    return pathlib.Path(simctl("get_app_container", udid, BUNDLE_ID, "data").strip())


def parse_launch_pid(text):
    m = re.search(rf"{re.escape(BUNDLE_ID)}:\s*(\d+)", text)
    if not m:
        raise RuntimeError(f"cannot parse simctl launch output: {text!r}")
    return int(m.group(1))


def launch(udid, args):
    return parse_launch_pid(simctl("launch", "--terminate-running-process", udid, BUNDLE_ID, *args))


def terminate(udid):
    simctl("terminate", udid, BUNDLE_ID, check=False)


def screenshot(udid, path):
    simctl("io", udid, "screenshot", "--type=png", str(path))


class Recorder:
    def __init__(self, udid, path):
        self.p = subprocess.Popen(["xcrun", "simctl", "io", udid, "recordVideo", "--codec=h264", "--force", str(path)],
                                  stderr=subprocess.PIPE, text=True, env=env())
        deadline, line = time.time() + 20, ""
        while time.time() < deadline:
            line = self.p.stderr.readline()
            if "Recording started" in line:
                # Keep draining stderr: a full, never-read pipe would block simctl in a long recording.
                threading.Thread(target=self.p.stderr.read, daemon=True).start()
                return
            if self.p.poll() is not None:
                break
        self.p.kill()
        raise RuntimeError(f"recordVideo did not start: {line!r}")

    def stop(self):
        self.p.send_signal(signal.SIGINT)
        self.p.wait(timeout=60)


def wait_for_log(path, pattern, timeout, start=0):
    rx, deadline, pos = re.compile(pattern), time.time() + timeout, start
    while True:
        try:
            with open(path, "rb") as f:
                f.seek(pos)
                while True:
                    line = f.readline()
                    if not line.endswith(b"\n"):
                        break
                    pos += len(line)
                    m = rx.search(line.decode("utf-8", "replace"))
                    if m:
                        return m, pos
        except FileNotFoundError:
            pass
        if time.time() > deadline:
            raise TimeoutError(f"no line matching {pattern!r} in {path} after {timeout} s")
        time.sleep(0.2)


def check_projection(log_text, tol=1e-3):
    m = re.search(r"AR_PROJECTION view=0 left=(\S+) right=(\S+) up=(\S+) down=(\S+) near=(\S+) recovered=(\d)", log_text)
    if not m:
        raise RuntimeError("no AR_PROJECTION line: the live projection was not logged")
    got = dict(zip(("left", "right", "up", "down", "near"), map(float, m.groups()[:5])))
    got["recovered"] = int(m.group(6))
    if not got["recovered"]:
        raise RuntimeError(f"the live projection is not infinite reverse-Z: {m.group(0)}")
    bad = [k for k, v in CONTRACT_PROJECTION.items() if abs(got[k] - v) > tol]
    if bad:
        raise RuntimeError(f"live projection differs from the measured contract in {bad}: {m.group(0)}")
    return got

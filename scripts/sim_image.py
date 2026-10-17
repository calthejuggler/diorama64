#!/usr/bin/env python3
"""scripts/sim_image.py — measure simulator screenshots with the stdlib only: sips converts the PNG to an sRGB,
downscaled BMP, and a small reader parses it.

  python3 scripts/sim_image.py local/shots/cube.png [...]     # prints one JSON line per image
"""
import json, pathlib, struct, sys, tempfile
from sm64ar_common import run

SRGB_PROFILE = "/System/Library/ColorSync/Profiles/sRGB Profile.icc"
CUBE_RED = (220, 60, 60)


def srgb_encode(v):
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


DOUBLE_ENCODED_RED = tuple(round(255 * srgb_encode(c / 255)) for c in CUBE_RED)


def to_bmp(png, bmp, width=960):
    run(["sips", "-m", SRGB_PROFILE, "--resampleWidth", str(width), "-s", "format", "bmp", str(png), "--out", str(bmp)],
        capture=True)


def read_bmp(data):
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    hdr, w, h, _planes, bpp, comp = struct.unpack_from("<IiiHHI", data, 14)
    if bpp not in (24, 32) or comp not in (0, 3):
        raise ValueError(f"unsupported BMP: {bpp} bpp, compression {comp}")
    if comp == 3 and struct.unpack_from("<III", data, 54) != (0x00FF0000, 0x0000FF00, 0x000000FF):
        raise ValueError("unsupported BMP channel masks")
    top_down, h = h < 0, abs(h)
    step = bpp // 8
    stride = (w * step + 3) & ~3
    rows = []
    for y in range(h):
        base = off + y * stride
        rows.append([(data[base + x * step + 2], data[base + x * step + 1], data[base + x * step]) for x in range(w)])
    if not top_down:
        rows.reverse()
    return w, h, rows


def red_face(rows):
    xs, ys, cols = [], [], []
    for y, row in enumerate(rows):
        for x, (r, g, b) in enumerate(row):
            if r - max(g, b) > 60:
                xs.append(x); ys.append(y); cols.append((r, g, b))
    if not cols:
        return {"count": 0}
    med = tuple(sorted(c[k] for c in cols)[len(cols) // 2] for k in range(3))
    return {"count": len(cols), "cx": sum(xs) / len(xs), "cy": sum(ys) / len(ys), "median": med}


def srgb_decode(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def linear(rgb):
    return tuple(srgb_decode(c / 255) for c in rgb)


# the simulator compositor scales our layer by a uniform linear gain (~0.73; the drawable itself holds
# the exact bytes), so colours are compared by their linear G/R and B/R ratios. The gain is reported and must be sane.
GAIN_RANGE = (0.5, 1.1)


def gain_and_class(median, rel_tol=0.25):
    r, g, b = linear(median)
    if r <= 0:
        return "other", None
    for name, want in (("correct", CUBE_RED), ("double-encoded", DOUBLE_ENCODED_RED)):
        wr, wg, wb = linear(want)
        ok = all(abs(got / r - w / wr) <= rel_tol * (w / wr) for got, w in ((g, wg), (b, wb)))
        gain = r / wr
        if ok and GAIN_RANGE[0] <= gain <= GAIN_RANGE[1]:
            return name, gain
    return "other", None


def classify(median):
    return gain_and_class(median)[0]


def black_fraction(rows):
    n = sum(len(r) for r in rows)
    return sum(1 for r in rows for p in r if max(p) < 8) / n


def green_fraction(rows):
    n = sum(len(r) for r in rows)
    return sum(1 for r in rows for (cr, cg, cb) in r if cg - max(cr, cb) > 20) / n


def measure(png):
    with tempfile.TemporaryDirectory() as d:
        bmp = pathlib.Path(d) / "x.bmp"
        to_bmp(png, bmp)
        w, h, rows = read_bmp(bmp.read_bytes())
    red = red_face(rows)
    out = {"image": str(png), "size": [w, h], "red_face": red, "black_fraction": round(black_fraction(rows), 4),
           "green_fraction": round(green_fraction(rows), 4)}
    if red["count"]:
        cls, gain = gain_and_class(red["median"])
        out["red_class"] = cls
        if gain is not None:
            out["red_gain_linear"] = round(gain, 3)
    return out


def same_place(a, b, px=3.0, ratio=0.05):
    ra, rb = a["red_face"], b["red_face"]
    if not ra.get("count") or not rb.get("count"):
        return False
    return (abs(ra["cx"] - rb["cx"]) <= px and abs(ra["cy"] - rb["cy"]) <= px
            and abs(ra["count"] - rb["count"]) <= ratio * max(ra["count"], rb["count"]))


def moved_right(a, b, px=20.0):
    ra, rb = a["red_face"], b["red_face"]
    return bool(ra.get("count")) and bool(rb.get("count")) and rb["cx"] - ra["cx"] > px


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(json.dumps(measure(p)))

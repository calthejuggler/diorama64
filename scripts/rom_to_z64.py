#!/usr/bin/env python3
"""Convert an N64 ROM in .v64 (byte-swapped) or .n64 (little-endian) order to .z64 (big-endian)."""
import hashlib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
Z64, V64, N64 = bytes.fromhex("80371240"), bytes.fromhex("37804012"), bytes.fromhex("40123780")
# The expected US ROM hash is read at run time, never kept in the repository: local/baserom.us.sha1 (yours), else the
# pinned upstream tree's own sm64.us.sha1 (build/_src/sm64, after scripts/sm64_prepare.py).
SHA1_FILES = (ROOT / "local" / "baserom.us.sha1", ROOT / "build" / "_src" / "sm64" / "sm64.us.sha1")


def expected_sha1(files=SHA1_FILES):
    for p in files:
        p = pathlib.Path(p)
        if p.is_file():
            words = p.read_text(errors="replace").split()
            if words and re.fullmatch(r"[0-9a-fA-F]{40}", words[0]):
                return words[0].lower()
    return None

def to_z64(data: bytes) -> bytes:
    magic = data[:4]
    if magic == Z64:
        return data
    if magic == V64:
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        return bytes(b)
    if magic == N64:
        b = bytearray(len(data))
        b[0::4], b[1::4], b[2::4], b[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(b)
    raise ValueError(f"not an N64 ROM (magic {magic.hex()})")

def main(argv):
    if len(argv) != 3:
        print("usage: rom_to_z64.py IN OUT", file=sys.stderr)
        return 2
    with open(argv[1], "rb") as f:
        data = f.read()
    try:
        out = to_z64(data)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    sha = hashlib.sha1(out).hexdigest()
    want = expected_sha1()
    if want is None:
        check = "SHA-1 not checked: no expected hash in local/baserom.us.sha1 or build/_src/sm64/sm64.us.sha1"
    elif sha == want:
        check = "SHA-1 matches the US ROM"
    else:
        check = None
        print("warning: the SHA-1 is not the US ROM's (expected hash from "
              f"{next(str(p) for p in SHA1_FILES if pathlib.Path(p).is_file())})", file=sys.stderr)
    with open(argv[2], "wb") as f:
        f.write(out)
    print(f"wrote {argv[2]}" + (f" ({check})" if check else ""))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))

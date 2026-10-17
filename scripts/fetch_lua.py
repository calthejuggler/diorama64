#!/usr/bin/env python3
"""scripts/fetch_lua.py — download the pinned Lua 5.3.5 sources into build/_deps (never committed) and check SHA-256.
sm64coopdx needs Lua 5.3 but ships only a desktop liblua53.a; we build it from source for macOS and visionOS."""
import hashlib, io, pathlib, shutil, sys, tarfile, urllib.request
from sm64ar_common import ROOT

URL = "https://www.lua.org/ftp/lua-5.3.5.tar.gz"
SHA256 = "0c2eed3f960446e1a3e4b9a1ca2f3ff893b6ce41942cf54d5dd59ab4b3b058ac"
DEST = ROOT / "build" / "_deps" / "lua-5.3.5"
STAMP = DEST / ".ar-sha256"


def verify(data, sha256):
    got = hashlib.sha256(data).hexdigest()
    if got != sha256:
        raise ValueError(f"SHA-256 mismatch: got {got}, pinned {sha256}")


def extract(data, dest):
    """Extracts regular files and dirs below the archive's top directory into dest; refuses absolute or '..' paths."""
    dest = pathlib.Path(dest)
    tmp = dest.with_name(dest.name + ".tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        for m in t.getmembers():
            parts = pathlib.PurePosixPath(m.name).parts
            if m.name.startswith("/") or ".." in parts or not (m.isfile() or m.isdir()):
                shutil.rmtree(tmp, ignore_errors=True)
                raise ValueError(f"unsafe archive member {m.name!r}")
            if len(parts) < 2:
                continue
            out = tmp.joinpath(*parts[1:])
            if m.isdir():
                out.mkdir(parents=True, exist_ok=True)
                continue
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(t.extractfile(m).read())
    shutil.rmtree(dest, ignore_errors=True)
    tmp.rename(dest)


def main():
    if STAMP.exists() and STAMP.read_text().strip() == SHA256 and (DEST / "src" / "lapi.c").exists():
        print(f"lua 5.3.5 up to date in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL, timeout=60) as r:
        data = r.read()
    verify(data, SHA256)
    extract(data, DEST)
    STAMP.write_text(SHA256 + "\n")
    print(f"lua 5.3.5 verified and extracted to {DEST}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

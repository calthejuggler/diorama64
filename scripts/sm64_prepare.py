#!/usr/bin/env python3
import hashlib, shutil, subprocess, sys
from sm64ar_common import KEEP, PATCH_DIR, PIN, ROOT, SRC, UPSTREAM, run

STAMP = SRC / ".ar-stamp"
IDENT = ["-c", "user.name=sm64ar", "-c", "user.email=sm64ar@localhost"]

def patches():
    return sorted(PATCH_DIR.glob("[0-9][0-9][0-9][0-9]-*.patch"))

def stamp_of(ps):
    h = hashlib.sha256(PIN.encode())
    for p in ps:
        h.update(p.name.encode()); h.update(p.read_bytes())
    return h.hexdigest()

def restore_build():
    if KEEP.exists():
        if (SRC / "build").exists():
            shutil.rmtree(SRC / "build")
        shutil.move(str(KEEP), str(SRC / "build"))

def main():
    if run(["git", "-C", UPSTREAM, "rev-parse", "HEAD"], capture=True).strip() != PIN:
        print(f"upstream is not at pin {PIN}", file=sys.stderr); return 1
    ps = patches(); st = stamp_of(ps)
    if STAMP.exists() and STAMP.read_text().strip() == st:
        print("sm64 source up to date"); return 0
    SRC.parent.mkdir(parents=True, exist_ok=True)
    if SRC.exists():
        if (SRC / "build").exists():
            if KEEP.exists():
                shutil.rmtree(KEEP)
            shutil.move(str(SRC / "build"), str(KEEP))
        subprocess.run(["git", "-C", str(UPSTREAM), "worktree", "remove", "--force", str(SRC)], capture_output=True)
        shutil.rmtree(SRC, ignore_errors=True)
    run(["git", "-C", UPSTREAM, "worktree", "prune"])
    run(["git", "-C", UPSTREAM, "worktree", "add", "--detach", "-f", SRC, PIN], capture=True)
    try:
        if ps:
            r = subprocess.run(["git", "-C", str(SRC), *IDENT, "am", "--3way", "--whitespace=nowarn", *map(str, ps)],
                               capture_output=True, text=True)
            if r.returncode:
                subprocess.run(["git", "-C", str(SRC), "am", "--abort"], capture_output=True)
                print(r.stdout + r.stderr, file=sys.stderr)
                print("patch series failed to apply", file=sys.stderr); return 1
        STAMP.write_text(st + "\n")
        print(f"prepared {SRC} ({len(ps)} patches)")
        return 0
    finally:
        restore_build()

if __name__ == "__main__":
    sys.exit(main())

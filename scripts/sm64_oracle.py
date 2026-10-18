#!/usr/bin/env python3
import os, sys
from sm64ar_common import BUILD_FLAGS, ROOT, SRC, missing_formulae, run

STAMP = SRC / "build" / ".ar-oracle"
EXE = SRC / "build" / "us_pc" / "sm64coopdx"


def up_to_date():
    prep = SRC / ".ar-stamp"
    return prep.is_file() and STAMP.is_file() and EXE.exists() and STAMP.read_text() == prep.read_text()


def main():
    if run([sys.executable, ROOT / "scripts" / "sm64_prepare.py"], check=False):
        return 1
    if up_to_date():
        print("sm64 desktop build up to date")
        return 0
    try:
        missing = missing_formulae()
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if missing:
        print(f"error: missing Homebrew formulae: {' '.join(missing)} (install them: brew install {' '.join(missing)})",
              file=sys.stderr)
        return 1
    STAMP.unlink(missing_ok=True)
    # The sm64coopdx Makefile assumes Homebrew gcc (a versioned cpp-N) and coreutils (gcp) on macOS.
    # Use Xcode's clang instead; -x c because the inputs (level_rules.mk, ...) are not .c files.
    run(["gmake", "-C", SRC, f"-j{os.cpu_count()}", *BUILD_FLAGS, "CPP=clang -E -P -x c", "CP=cp"])
    # On macOS the game resolves resources (lang, mods, palettes, dynos, ...) at <exe dir>/../Resources
    # (src/pc/platform.c sys_resource_path, app-bundle layout). Point that at the plain build dir.
    res = SRC / "build" / "Resources"
    if not res.is_symlink():
        res.symlink_to("us_pc")
    if not EXE.exists():
        print("MISSING oracle executable", file=sys.stderr)
        return 1
    STAMP.write_text((SRC / ".ar-stamp").read_text())
    print(EXE)
    return 0


if __name__ == "__main__":
    sys.exit(main())

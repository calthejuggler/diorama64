#!/usr/bin/env python3
"""scripts/bundle_local.py — every app build (`visionos_build.py`, `./build.py`): the user's own ROM and the prepared,
stamped packs go INTO the app under Bundled/, the only place the app reads them from.

  Bundled/rom.z64                       local/baserom.us.z64 (a .z64 SM64 ROM)
  Bundled/packs/<name>/                 every local/packs/<id>/prepared/<name>: re-verified (packs.py verify), copied,
                                        re-hashed and stamped (.sm64ar-verified, which the app checks)

Every check refuses the whole build: nothing is bundled from a file that does not verify. The app reads them only from
Bundled/ (read-only), never from Documents.
"""
import hashlib, pathlib, shutil, subprocess, tempfile
from sm64ar_common import ROOT, env

BUNDLED = "Bundled"
ROM = ROOT / "local" / "baserom.us.z64"
PACKS = ROOT / "local" / "packs"


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def prepared_packs(root=PACKS):
    """A name prepared twice is refused."""
    from visionos_device import valid_pack_name
    out = {}
    for p in sorted(pathlib.Path(root).glob("*/prepared/*")):
        if not p.is_dir():
            continue
        if not valid_pack_name(p.name):
            raise ValueError(f"{p}: not a pack name")
        if p.name in out:
            raise ValueError(f"pack name {p.name!r} is prepared twice ({out[p.name].parent.parent.name}, {p.parent.parent.name})")
        out[p.name] = p
    return list(out.values())


def finder_junk(p):
    return p.name == ".DS_Store" or p.name.startswith("._")      # Finder writes these into folders it shows


def tree_hashes(d):
    """{relative posix path: sha256} of every file under d (hidden files too, Finder's own files aside); a symlink or
    other non-file refuses."""
    d = pathlib.Path(d)
    out = {}
    for p in sorted(d.rglob("*")):
        if finder_junk(p):
            continue
        if p.is_symlink():
            raise ValueError(f"{p}: a symlink")
        if p.is_file():
            out[p.relative_to(d).as_posix()] = sha256(p)
        elif not p.is_dir():
            raise ValueError(f"{p}: not a regular file")
    return out


def stage(dest, rom=ROM, packs_root=PACKS, reverify=True, log=print, names=None):
    """Every file is re-checked against its source after the copy; the built app's Bundled/ must match the returned
    {relative path: sha256} byte for byte."""
    import visionos_device as vd
    dest = pathlib.Path(dest)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    rom = vd.check_rom(rom)                                    # only a .z64 (big-endian) ROM
    shutil.copyfile(rom, dest / "rom.z64")
    if sha256(dest / "rom.z64") != sha256(rom):
        raise ValueError(f"{dest / 'rom.z64'}: copy differs from {rom}")
    log(f"bundle: rom.z64 <- {rom}")
    packs = prepared_packs(packs_root)
    if names is not None:
        missing = sorted(set(names) - {p.name for p in packs})
        if missing:
            raise ValueError(f"no prepared pack named {', '.join(map(repr, missing))} (python3 -I scripts/packs.py prepare ID)")
        packs = [p for p in packs if p.name in set(names)]
    if packs:
        (dest / "packs").mkdir()
    for src in packs:
        if reverify:
            cmd = [str(x) for x in vd.reverify_cmd(src)]
            if subprocess.run(cmd, env=env()).returncode:
                raise ValueError(f"{src.name}: re-verification failed (see above); nothing was bundled")
        with tempfile.TemporaryDirectory(prefix="sm64ar-bundle-") as tmp:
            snap = vd.snapshot(src, tmp)
            shutil.copytree(snap, dest / "packs" / src.name)
        verified, out = vd.verified_manifest(src), dest / "packs" / src.name
        if sorted(p.name for p in out.iterdir()) != sorted([*verified, vd.STAMP]) or \
                any(sha256(out / n) != h for n, h in verified.items()):
            raise ValueError(f"{src.name}: bundled files differ from the verified list")
        log(f"bundle: packs/{src.name}/ <- {src} ({len(verified)} files, stamped)")
    return tree_hashes(dest)


def check_app(app, expected):
    b = pathlib.Path(app) / BUNDLED
    if not b.is_dir():
        return [f"{b} missing"]
    try:
        got = tree_hashes(b)
    except ValueError as e:
        return [str(e)]
    problems = [f"{BUNDLED}/{k} missing" for k in sorted(set(expected) - set(got))]
    problems += [f"{BUNDLED}/{k} not staged" for k in sorted(set(got) - set(expected))]
    problems += [f"{BUNDLED}/{k} differs from its source" for k in sorted(set(got) & set(expected)) if got[k] != expected[k]]
    return problems


def size_mb(path):
    return sum(p.stat().st_size for p in pathlib.Path(path).rglob("*") if p.is_file() and not p.is_symlink()) / 1e6

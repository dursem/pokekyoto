#!/usr/bin/env python3
"""Build the pinned Porymap 6.3.1 source with Pokémon Kyoto's Opal tile + logical palette editor support (8-subtile M3 authoring)."""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

PORYMAP_COMMIT = "26b919d7ff4b54152de010abd5bf344af2ebe116"
ROOT = Path(__file__).resolve().parent
PATCHES = [ROOT / "patches" / "opal-palettes.patch", ROOT / "patches" / "opal-tiles.patch"]


def run(args, cwd=None):
    print("+", " ".join(map(str, args)))
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def find_program(candidates):
    for name in candidates:
        p = shutil.which(name)
        if p:
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--directory", type=Path, default=ROOT / "build")
    ap.add_argument("--qmake", default=None)
    ap.add_argument("--make", dest="make_program", default=None)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--patch-only", action="store_true")
    args = ap.parse_args()

    work = args.directory.resolve()
    source = work / "porymap-source"
    build = work / "porymap-build"
    work.mkdir(parents=True, exist_ok=True)

    if not source.exists():
        run(["git", "init", source])
        run(["git", "remote", "add", "origin", "https://github.com/huderlem/porymap.git"], cwd=source)
        run(["git", "fetch", "--depth=1", "origin", PORYMAP_COMMIT], cwd=source)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=source)

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if head != PORYMAP_COMMIT:
        raise SystemExit(f"Refusing to patch {source}: expected {PORYMAP_COMMIT}, found {head}")

    for patch in PATCHES:
        reversed_ok = subprocess.run(["git", "apply", "--reverse", "--check", str(patch)], cwd=source,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not reversed_ok:
            run(["git", "apply", "--check", patch], cwd=source)
            run(["git", "apply", patch], cwd=source)

    run([sys.executable, ROOT / "upgrade_triple_layer.py", "--source", source])

    print(f"Patched source: {source}")
    if args.patch_only:
        return

    qmake = args.qmake or find_program(["qmake6", "qmake"])
    make = args.make_program or find_program(["mingw32-make", "nmake", "make"])
    if not qmake or not make:
        raise SystemExit("Qt qmake and a compatible make tool are required. Use --patch-only if you only want the patched source.")

    build.mkdir(parents=True, exist_ok=True)
    run([qmake, source / "porymap.pro"], cwd=build)
    make_name = Path(make).name.lower()
    if make_name == "nmake.exe" or make_name == "nmake":
        run([make], cwd=build)
    else:
        run([make, f"-j{args.jobs}"], cwd=build)
    print(f"Build directory: {build}")


if __name__ == "__main__":
    main()

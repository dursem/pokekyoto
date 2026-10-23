#!/usr/bin/env python3
"""Installeer de nachtverlichting met een uitzondering voor de glazen overkapping."""

import argparse
import ast
import difflib
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime
from pathlib import Path


REQUIRED_FILES = (
    "Makefile", "include/map_palette.h", "src/map_palette.c",
    "src/field_camera.c", "src/palette.c", "src/field_weather.c",
    "src/palette_util.c", "test/map_palette.c", "test/tile_cache.c",
    "tools/map_palettes/compiler.py", "tools/map_palettes/test_compiler.py",
)
PYTHON_FILES = (
    "tools/map_palettes/compiler.py", "tools/map_palettes/test_compiler.py",
)
INSTALLER_VERSION = "2.1"


def git(project, *args):
    return subprocess.run(
        ["git", "-C", str(project), *args], capture_output=True,
        text=True, encoding="utf-8", errors="replace", check=False,
    )


def git_apply(project, *args):
    # Never ignore Python indentation or let a user's Git configuration
    # silently fix whitespace in the source being installed.
    return git(project, "-c", "apply.ignoreWhitespace=false", "apply",
               "--whitespace=nowarn", *args)


def has_night_lights(project):
    return b"sNightLights[]" in (project / "src/map_palette.c").read_bytes()


def already_installed(project, update):
    return (has_night_lights(project)
            and git_apply(project, "--reverse", "--check", str(update)).returncode == 0)


def choose_patch(project, candidates):
    diagnostics = []
    for patch, description in candidates:
        checked = git_apply(project, "--check", str(patch))
        if checked.returncode:
            diagnostics.append(patch.name + ":\n" + (checked.stderr or checked.stdout).strip())
            continue
        if patch.name == "glas_aanvulling.patch" and not has_night_lights(project):
            diagnostics.append("De oorspronkelijke nachtverlichtingstabel ontbreekt.")
            continue
        return (patch, description), diagnostics
    return None, diagnostics


def install(project, patch, description, check_only, originals):
    # Check the complete captured inputs again before touching the project.
    if any((project / name).read_bytes() != data for name, data in originals.items()):
        print("FOUT: een bronbestand is tijdens de controle gewijzigd. Voer het script opnieuw uit.", file=sys.stderr)
        return 1
    if check_only:
        print("Controle geslaagd: " + description.lower() + " kan worden toegepast.")
        print("Er zijn geen bestanden gewijzigd.")
        return 0
    applied = git_apply(project, str(patch))
    if applied.returncode:
        print("FOUT: Git kon de patch niet toepassen:\n" + (applied.stderr or applied.stdout).strip(), file=sys.stderr)
        return 1
    print(description + " toegepast.")
    print("De ramen blijven geel oplichten; de glazen overkapping verduistert normaal mee.")
    print("Bouw je ROM opnieuw in deze projectmap met je gebruikelijke commando, bijvoorbeeld:")
    print("  make -j$(nproc)")
    return 0


def uniform_crlf(data):
    return b"\r\n" in data and b"\n" not in data.replace(b"\r\n", b"")


def exact_diff(originals, outputs):
    parts = []
    for name, before in originals.items():
        after = outputs[name]
        if before == after:
            continue
        for line in difflib.diff_bytes(
            difflib.unified_diff, before.splitlines(keepends=True),
            after.splitlines(keepends=True),
            fromfile=("a/" + name).encode(), tofile=("b/" + name).encode(),
        ):
            parts.append(line)
            if not line.endswith(b"\n"):
                parts.append(b"\n\\ No newline at end of file\n")
    return b"".join(parts)


def try_crlf_install(project, candidates, update, check_only, originals, diagnostics):
    crlf_files = [name for name in PYTHON_FILES if uniform_crlf(originals[name])]
    if not crlf_files:
        return None

    # Normalize only the two affected Python files, in a temporary copy.
    # Strict patch matching still checks every other byte, including indentation.
    with tempfile.TemporaryDirectory(prefix="pokekyoto_nachtlicht_") as temporary:
        staged = Path(temporary)
        for name, data in originals.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data.replace(b"\r\n", b"\n") if name in crlf_files else data)
        initialized = git(staged, "init", "-q")
        if initialized.returncode:
            diagnostics.append("Tijdelijke controlemap kon niet worden voorbereid:\n" + initialized.stderr.strip())
            return None
        if already_installed(staged, update):
            print("De glasuitzondering is al geïnstalleerd. Er is niets gewijzigd.")
            return 0
        selected, staged_diagnostics = choose_patch(staged, candidates)
        if selected is None:
            diagnostics.append("Controle met genormaliseerde Windows-regelafbrekingen:\n"
                               + "\n\n".join(staged_diagnostics))
            return None
        patch, description = selected
        applied = git_apply(staged, str(patch))
        if applied.returncode:
            diagnostics.append("Tijdelijke toepassing mislukt:\n" + (applied.stderr or applied.stdout).strip())
            return None
        outputs = {name: (staged / name).read_bytes() for name in REQUIRED_FILES}
        try:
            for name in PYTHON_FILES:
                ast.parse(outputs[name], filename=name)
        except SyntaxError as error:
            diagnostics.append("Python-syntaxcontrole mislukt: " + str(error))
            return None
        for name in crlf_files:
            outputs[name] = outputs[name].replace(b"\n", b"\r\n")
        adapted = staged / "aangepaste_regelafbrekingen.patch"
        adapted.write_bytes(exact_diff(originals, outputs))
        checked = git_apply(project, "--check", str(adapted))
        if checked.returncode:
            diagnostics.append("Controle tegen de oorspronkelijke bestanden mislukt:\n"
                               + (checked.stderr or checked.stdout).strip())
            return None
        print("Windows-regelafbrekingen herkend in " + ", ".join(crlf_files) + ".")
        print("De oorspronkelijke regelafbrekingen blijven behouden; code en inspringing zijn strikt gecontroleerd.")
        return install(project, adapted, description, check_only, originals)


def write_diagnostics(project, diagnostics):
    archive = project / ("nachtverlichting_diagnose_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f") + ".zip")
    try:
        current = {name: (project / name).read_bytes() for name in REQUIRED_FILES}
        metadata = {
            "installer_version": INSTALLER_VERSION,
            "git_head": git(project, "rev-parse", "HEAD").stdout.strip(),
            "included_source_files": list(PYTHON_FILES),
            "checks": diagnostics,
            "files": {
                name: {
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "crlf_lines": data.count(b"\r\n"),
                    "lf_only_lines": data.count(b"\n") - data.count(b"\r\n"),
                } for name, data in current.items()
            },
        }
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as output:
            for name in PYTHON_FILES:
                output.writestr(name, current[name])
            output.writestr("diagnose.json", json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
    except OSError as error:
        print("Het diagnosebestand kon niet worden gemaakt: " + str(error), file=sys.stderr)
        return False
    print("Diagnosebestand gemaakt: " + archive.name)
    print("Dit bevat alleen de twee Python-bestanden en controlegegevens; er wordt niets verstuurd.")
    print("Stuur dit ZIP-bestand mee als de installatie blijft mislukken.")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Behoud verlichte ramen en sluit de glazen bankjesoverkapping uit van geel licht."
    )
    parser.add_argument("project", nargs="?", default=".", help="Bestaande PokéKyoto-projectmap; standaard de huidige map.")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="Controleer zonder bestanden te wijzigen.")
    mode.add_argument("--diagnose", action="store_true", help="Maak een diagnose-ZIP zonder bronbestanden te wijzigen.")
    args = parser.parse_args()
    project = Path(args.project).expanduser().resolve()
    folder = Path(__file__).resolve().parent
    full = folder / "nachtverlichting.patch"
    update = folder / "glas_aanvulling.patch"
    missing = [name for name in REQUIRED_FILES if not (project / name).is_file()]
    if missing:
        print("FOUT: kies de hoofdmap van je bestaande PokéKyoto-project.", file=sys.stderr)
        print("Ontbrekend: " + ", ".join(missing), file=sys.stderr)
        return 1
    try:
        root = git(project, "rev-parse", "--show-toplevel")
    except FileNotFoundError:
        print("FOUT: Git is niet beschikbaar. Gebruik je WSL-bouwomgeving met Git.", file=sys.stderr)
        return 1
    if root.returncode or Path(root.stdout.strip()).resolve() != project:
        print("FOUT: voer dit uit in de hoofdmap van je bestaande PokéKyoto Git-repository.", file=sys.stderr)
        return 1

    if args.diagnose:
        return 0 if write_diagnostics(project, []) else 1
    if not full.is_file() or not update.is_file():
        print("FOUT: zet beide .patch-bestanden naast dit installatiescript.", file=sys.stderr)
        return 1
    originals = {name: (project / name).read_bytes() for name in REQUIRED_FILES}

    if already_installed(project, update):
        print("De glasuitzondering is al geïnstalleerd. Er is niets gewijzigd.")
        return 0

    candidates = (
        (full, "Nachtverlichting en glasuitzondering"),
        (update, "Aanvulling voor de bestaande nachtverlichting"),
    )
    selected, diagnostics = choose_patch(project, candidates)
    if selected is not None:
        return install(project, *selected, args.check, originals)
    result = try_crlf_install(project, candidates, update, args.check, originals, diagnostics)
    if result is not None:
        return result

    print("FOUT: de patch past niet zonder conflicten. Er zijn geen bronbestanden gewijzigd.", file=sys.stderr)
    print("\n\n".join(diagnostics), file=sys.stderr)
    if args.check:
        print("Maak zo nodig een diagnosebestand met: python3 installeer_nachtverlichting.py --diagnose", file=sys.stderr)
    else:
        write_diagnostics(project, diagnostics)
    return 1


if __name__ == "__main__":
    sys.exit(main())

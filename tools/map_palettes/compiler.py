#!/usr/bin/env python3
"""Compile and certify extended tileset palette libraries."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


class PaletteError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise PaletteError(message)


def warn(message):
    print(f"Map palettes: warning: {message}", file=sys.stderr)


def read_json(path):
    return json.loads(path.read_text())


def words(path):
    data = path.read_bytes()
    require(len(data) % 2 == 0, f"{path}: odd binary size")
    return list(struct.unpack(f"<{len(data) // 2}H", data))


def read_sidecar(path, metatiles):
    value = read_json(path)
    require(isinstance(value, dict) and value.get("version") == 1,
            f"{path}: expected a version 1 palette sidecar")
    require(value.get("metatiles_sha256") == hashlib.sha256(metatiles.read_bytes()).hexdigest(),
            f"{path}: metatile binary checksum mismatch; save the paired files together")
    references = value.get("palettes")
    require(isinstance(references, list), f"{path}: palettes must be an array")
    return references


def palette(path):
    lines = path.read_text().splitlines()
    require(lines[:3] == ["JASC-PAL", "0100", "16"], f"{path}: expected a 16-colour JASC palette")
    require(len(lines) == 19, f"{path}: expected exactly 16 colours")
    result = []
    for line in lines[3:]:
        rgb = [int(c) for c in line.split()]
        require(len(rgb) == 3 and all(0 <= c <= 255 for c in rgb), f"{path}: invalid RGB colour")
        result.append(sum((c >> 3) << (5 * i) for i, c in enumerate(rgb)))
    # Light colours (time-of-day blending) are flagged in a .pla next to the palette, as gbagfx reads them.
    light_flags = path.with_suffix(".pla")
    if light_flags.exists():
        for number, line in enumerate(light_flags.read_text().splitlines(), 1):
            entry = line.split("#", 1)[0].strip()
            if not entry:
                continue
            require(entry.isdecimal() and 0 <= int(entry) < 16,
                    f"{light_flags}:{number}: expected a colour index 0..15")
            result[int(entry)] |= 0x8000
    return result


def mask(values, limit=14):
    require(isinstance(values, list) and all(type(v) is int and 0 <= v < limit for v in values),
            f"Expected palette IDs in [0, {limit - 1}], got {values}")
    return sum(1 << v for v in set(values))


def validate_mask(required, reserved, pinned, location):
    require(not reserved & pinned, f"{location}: pinned palette conflicts with reserved bank")
    demand = (required | pinned).bit_count()
    capacity = 14 - reserved.bit_count()
    require(demand <= capacity,
            f"{location}: {demand} palettes exceed {capacity} available banks; "
            f"logical={[i for i in range(18) if (required | pinned) & (1 << i)]}, "
            f"reserved={[i for i in range(14) if reserved & (1 << i)]}")
    return demand


def grid_format(root):
    """Metatile id mask and primary metatile counts, from the headers when the project has them."""
    fmt = {"id_mask": 0x3FF, "primary": 512, "primary_frlg": 640}
    fieldmap = root / "include/fieldmap.h"
    grid = root / "include/global.fieldmap.h"
    if fieldmap.exists() and grid.exists():
        defines = dict(re.findall(r'#define\s+(\w+)\s+(0[xX][0-9a-fA-F]+|\d+)\b', fieldmap.read_text() + grid.read_text()))
        fmt = {"id_mask": int(defines["MAPGRID_METATILE_ID_MASK"], 0),
               "primary": int(defines["NUM_METATILES_IN_PRIMARY"], 0),
               "primary_frlg": int(defines["NUM_METATILES_IN_PRIMARY_FRLG"], 0)}
    return fmt


def definitions(root):
    headers = (root / "src/data/tilesets/headers.h").read_text()
    metatiles = (root / "src/data/tilesets/metatiles.h").read_text()
    paths = dict(re.findall(r'const u16\s+(\w+)\[\]\s*=\s*INCBIN_U16\("([^"]+)"\)', metatiles))
    result = {}
    for symbol, body in re.findall(r'const struct Tileset\s+(\w+)\s*=\s*\{(.*?)\};', headers, re.S):
        match = re.search(r'\.metatiles\s*=\s*(\w+)', body)
        if match and match[1] in paths:
            result[symbol] = (root / paths[match[1]], body)
    return result


def metatile_masks(path, references=None):
    entries = words(path)
    require(len(entries) % 12 == 0, f"{path}: expected triple-layer metatiles")
    count = len(entries) // 12
    if references is None:
        references = [[word >> 12 for word in entries[i:i + 12]] for i in range(0, len(entries), 12)]
    require(len(references) == count, f"{path}: palette sidecar must have {count} rows")
    result = []
    for i, row in enumerate(references):
        require(isinstance(row, list) and len(row) == 12, f"{path}: row {i} needs twelve palette IDs")
        result.append(mask(row, 18))
    return result, references


def connection_kind(origin, target, label):
    """How the engine crosses a seamless connection touching an extended library.

    'seamless': the same tileset pair, so the palette allocation carries over and cells of the previous
    map keep valid references. 'reload': another pair on the same primary tileset and grid
    format; fieldmap.c's LoadConnectedMapTilesetPalettes reloads every map bank and redraws the view in
    the crossing frame. Anything else cannot be crossed seamlessly: the primary tileset isn't reloaded.
    """
    require(origin["primary_tileset"] == target["primary_tileset"]
            and origin.get("layout_version", "emerald") == target.get("layout_version", "emerald"),
            f"{label}: a seamless connection touching an extended palette library needs the same primary tileset "
            f"and grid format on both sides")
    return "seamless" if origin["secondary_tileset"] == target["secondary_tileset"] else "reload"


def make_grid(layout, layouts, maps_by_id, map_data, masks, root):
    """Match fieldmap.c's padded grid and cardinal connection copy rectangles."""
    width, height = layout["width"], layout["height"]
    stride, rows = width + 15, height + 14
    cells = [None] * (stride * rows)
    strip = set()
    data = words(root / layout["blockdata_filepath"])
    require(len(data) == width * height, f"{layout['id']}: incorrect map.bin size")
    for y in range(height):
        cells[(y + 7) * stride + 7:(y + 7) * stride + 7 + width] = data[y * width:(y + 1) * width]
    for connection in map_data.get("connections") or []:
        direction = connection["direction"]
        if direction not in ("up", "down", "left", "right"):
            continue
        neighbor = maps_by_id[connection["map"]]
        other = layouts[neighbor["layout"]]
        # The neighbour's strip is drawn with this layout's tilesets, so it is certified with this layout's masks
        # either way; a different pair only decides how the crossing is handled (see connection_kind).
        connection_kind(layout, other, f"{map_data['id']} -> {neighbor['id']}")
        source = words(root / other["blockdata_filepath"])
        ow, oh = other["width"], other["height"]
        offset = connection["offset"]
        for y in range(oh):
            for x in range(ow):
                if direction == "up":
                    dx, dy = x + offset + 7, y - oh + 7
                    inside = 0 <= dy < 7
                elif direction == "down":
                    dx, dy = x + offset + 7, height + 7 + y
                    inside = height + 7 <= dy < rows
                elif direction == "left":
                    dx, dy = x - ow + 7, y + offset + 7
                    inside = 0 <= dx < 7
                else:
                    dx, dy = width + 7 + x, y + offset + 7
                    inside = width + 7 <= dx < stride
                if inside and 0 <= dx < stride and 0 <= dy < rows:
                    cells[dy * stride + dx] = source[y * ow + x]
                    strip.add(dy * stride + dx)
    id_mask = grid_format(root)["id_mask"]
    border = words(root / layout["border_filepath"])
    border_mask = 0
    for value in border:
        index = value & id_mask
        require(index < len(masks) and masks[index] is not None, f"{layout['id']}: invalid border metatile {index}")
        border_mask |= masks[index]
    output = []
    substituted = set()
    for cell, value in enumerate(cells):
        if value is None or value & id_mask == id_mask:
            output.append(border_mask)
            continue
        index = value & id_mask
        if (index >= len(masks) or masks[index] is None) and cell in strip:
            # field_camera.c draws a strip metatile this layout's tilesets lack as metatile 0.
            substituted.add(index)
            index = 0
        require(index < len(masks) and masks[index] is not None, f"{layout['id']}: invalid metatile {index}")
        output.append(masks[index])
    if substituted:
        warn(f"{layout['id']}: connected maps' strips name metatiles {sorted(substituted)} that its tilesets lack; "
             f"they are drawn as metatile 0")
    return output, stride, rows, border_mask


def certify(layout, map_data, layouts, maps_by_id, masks, root, reserved, pinned, mutations, resident=0):
    grid, width, height, border = make_grid(layout, layouts, maps_by_id, map_data, masks, root)
    mutation_mask = resident
    for metatile in mutations:
        require(type(metatile) is int and 0 <= metatile < len(masks) and masks[metatile] is not None,
                f"{layout['id']}: invalid dynamic metatile {metatile}")
        mutation_mask |= masks[metatile]
    peak = 0
    # Includes stale trailing cache cells during reversals, not just the LCD rectangle.
    for y in range(layout["height"] + 1):
        for x in range(layout["width"] + 1):
            required = mutation_mask
            for dy in range(-1, 17):
                for dx in range(-1, 17):
                    xx, yy = x + dx, y + dy
                    required |= grid[yy * width + xx] if 0 <= xx < width and 0 <= yy < height else border
            peak = max(peak, validate_mask(required, reserved, pinned,
                       f"{map_data['id']} camera=({x}, {y}), conservative 18x18 cache envelope"))
    return peak


def effect_requirements(root, spec, layouts, maps):
    reserved = mask(spec.get("reserved_banks", []))
    pinned = mask(spec.get("pinned_palettes", [])) | 1
    effects = set(spec.get("effects", []))
    layout_ids = {v["id"] for v in layouts}
    preview_path = root / "src/map_preview_screen.c"
    preview_text = preview_path.read_text() if preview_path.exists() else ""
    previews = dict(re.findall(r'\.mapsec\s*=\s*(\w+),(.*?)\n    }', preview_text, re.S))
    for data in maps:
        if data["layout"] not in layout_ids:
            continue
        preview = previews.get(data.get("region_map_section"), "")
        if preview:
            require(not ("MPS_TYPE_FADE_IN" in preview and re.search(r'\.usesAllPalettes\s*=\s*TRUE', preview)),
                    f"{data['id']}: all-bank preview cannot crossfade over an extended map")
            effects.add("preview")
        script = root / "data/maps" / data["name"] / "scripts.pory"
        text = script.read_text() if script.exists() else ""
        if re.search(r'\bpokemart\b', text):
            effects.add("shop")
        if "DoPokemonLeagueLightingEffect" in text:
            effects.add("league_lighting")
        if "DrawElevatorCurrentFloorWindow" in text:
            effects.add("elevator")
        if "RayquazaSpotlight" in text or data["name"] == "SkyPillar_Top":
            effects.add("spotlight")
        if data["name"] == "Route111":
            effects.add("mirage_tower")
        if "SecretBase" in data["name"]:
            effects.add("decoration")
    for effect in effects:
        require(effect in ("shop", "elevator", "spotlight", "decoration", "preview", "league_lighting", "mirage_tower"),
                f"Unknown palette effect {effect}")
        if effect == "shop":
            reserved |= (1 << 13) | (1 << 12) | (1 << 11 if any(v.get("layout_version") == "frlg" for v in layouts) else 0)
        elif effect in ("elevator", "decoration", "preview"):
            reserved |= 1 << 13
        elif effect == "spotlight":
            reserved |= 1 << 12
        elif effect == "league_lighting":
            pinned |= 1 << 7
        elif effect == "mirage_tower":
            pinned |= 1 << 6
    return reserved, pinned, effects


def compile_project(root, manifest, game="emerald"):
    require(isinstance(manifest, dict) and manifest.get("version") == 1, "Unsupported palette manifest version")
    require(set(manifest) <= {"version", "tilesets"}, "Unknown manifest fields")
    require(isinstance(manifest.get("tilesets"), list), "tilesets must be an array")
    defs = definitions(root)
    layouts = {v["id"]: v for v in read_json(root / "data/layouts/layouts.json")["layouts"]}
    maps = [read_json(path) for path in sorted((root / "data/maps").glob("*/map.json"))]
    maps_by_id = {m["id"]: m for m in maps}
    code = ["// Generated by tools/map_palettes/compiler.py; do not edit."]
    extensions, profiles, report = [], [], []
    seen = set()
    for spec in manifest.get("tilesets", []):
        require(isinstance(spec, dict), "Each extended tileset must be an object")
        require(set(spec) <= {"tileset", "palette_refs", "palettes", "reserved_banks", "pinned_palettes", "effects", "dynamic_metatiles", "resident_palettes"},
                f"Unknown tileset fields: {set(spec)}")
        name = spec["tileset"]
        require(name in defs and name not in seen, f"Unknown or duplicate tileset {name}")
        seen.add(name)
        path, body = defs[name]
        require(re.search(r'\.isSecondary\s*=\s*TRUE', body), f"{name}: only secondary tilesets may be extended")
        require(len(spec["palettes"]) == 4, f"{name}: exactly four additional palettes are required")
        secondary, references = metatile_masks(path, read_sidecar(root / spec["palette_refs"], path))
        selected = [v for v in layouts.values() if v["secondary_tileset"] == name
                    and v.get("layout_version", "emerald") == ("frlg" if game == "firered" else "emerald")]
        if not selected:
            continue
        selected_ids = {v["id"] for v in selected}
        for origin in maps:
            source_layout = layouts[origin["layout"]]
            if source_layout.get("layout_version", "emerald") != ("frlg" if game == "firered" else "emerald"):
                continue
            for connection in origin.get("connections") or []:
                if connection["direction"] not in ("up", "down", "left", "right"):
                    continue
                target = layouts[maps_by_id[connection["map"]]["layout"]]
                if target["id"] in selected_ids:
                    connection_kind(source_layout, target, f"{origin['id']} -> {connection['map']} (incoming)")
        reserved, pinned, effects = effect_requirements(root, spec, selected, maps)
        if "BattleDome" in body:
            pinned |= 1 << 8
        require(not reserved & pinned, f"{name}: reserved/pinned conflict")
        resident = mask(spec.get("resident_palettes", []), 18)
        # Door animation tables still name legacy logical palettes. Keep their union resident.
        door_path = root / "src/field_door.c"
        door_text = door_path.read_text() if door_path.exists() else ""
        door_palettes = dict(re.findall(r'static const u8 (sDoorAnimPalettes_\w+)\[\] = \{([^}]+)\}', door_text))
        pairs = {name} | {v["primary_tileset"] for v in selected}
        has_doors = False
        for tileset, palette_name in re.findall(r'\.tileset = &(\w+),[^}]*?\.palettes = (\w+)', door_text):
            if tileset in pairs:
                has_doors = True
                require(palette_name in door_palettes, f"Unknown door palette table {palette_name}")
                resident |= mask([int(v.strip()) for v in door_palettes[palette_name].split(",") if v.strip()])
        if "shop" in effects:
            for value in secondary:
                resident |= value
            for layout in selected:
                for value in metatile_masks(defs[layout["primary_tileset"]][0])[0]:
                    resident |= value
        require("decoration" not in effects,
                f"{name}: decoration placement uses a separate metatile-to-sprite renderer; keep Secret Base tilesets legacy")
        fmt = grid_format(root)
        for layout in selected:
            primary_count = fmt["primary_frlg"] if layout.get("layout_version") == "frlg" else fmt["primary"]
            primary, _ = metatile_masks(defs[layout["primary_tileset"]][0])
            require(all(not value >> 14 for value in primary),
                    f"{layout['id']}: primary metatiles may not reference UI/extra palettes")
            require(len(primary) <= primary_count and len(secondary) <= fmt["id_mask"] - primary_count,
                    f"{layout['id']}: too many metatiles")
            # A layout whose tiles stream (see docs/TILESET_CAPACITY_GUIDE.md) keeps VRAM slots 1008-1023 for doors.
            streamed = any((defs[symbol][0].parent / "metatile_tiles_ext.bin").exists()
                           for symbol in (layout["primary_tileset"], layout["secondary_tileset"]))
            if has_doors and not streamed:
                used_tiles = {v & 1023 for v in words(path) + words(defs[layout["primary_tileset"]][0])}
                require(len(used_tiles) <= 1024 - 16, f"{layout['id']}: animated doors require sixteen unused tile IDs")
            combined = primary + [None] * (primary_count - len(primary)) + secondary
            users = [m for m in maps if m["layout"] == layout["id"]]
            require(users, f"{layout['id']}: extended layout has no map to validate")
            peak = 0
            mutations = list(spec.get("dynamic_metatiles", []))
            labels_path = root / "include/constants/metatile_labels.h"
            labels_text = labels_path.read_text() if labels_path.exists() else ""
            labels = {key: int(value, 0) for key, value in re.findall(r'#define\s+(METATILE_\w+)\s+(0[xX][0-9a-fA-F]+|[0-9]+)\b', labels_text)}
            for map_data in users:
                script_path = root / "data/maps" / map_data["name"] / "scripts.pory"
                text = script_path.read_text() if script_path.exists() else ""
                text = re.sub(r'//[^\n]*', '', text)
                for token in re.findall(r'\bsetmetatile\s*\([^,]*,[^,]*,\s*([^,\s]+)', text):
                    if token in labels:
                        mutations.append(labels[token])
                    elif re.fullmatch(r'0[xX][0-9a-fA-F]+|[0-9]+', token):
                        mutations.append(int(token, 0))
                    else:
                        require(bool(spec.get("dynamic_metatiles")), f"{map_data['id']}: declare dynamic_metatiles for computed setmetatile {token}")
                peak = max(peak, certify(layout, map_data, layouts, maps_by_id, combined, root,
                                        reserved, pinned, mutations, resident))
            code.append(f"extern const struct MapLayout {layout['name']};")
            profiles.append(f"    {{&{layout['name']}, {reserved}, {pinned}, {resident}, {len(primary)}}},")
            report.append({"layout": layout["id"], "peak": peak, "capacity": 14 - reserved.bit_count()})
        rows = []
        nights = []
        weather = []
        for i, item in enumerate(spec["palettes"]):
            require(set(item) <= {"day", "night", "weather"}, f"{name}: unknown palette fields")
            rows.append("    {" + ", ".join(hex(v) for v in palette(root / item["day"])) + "},")
            if item.get("night"):
                symbol = f"sMapPaletteNight_{name}_{i}"
                code.append(f"static const u16 {symbol}[16] = {{" + ", ".join(hex(v) for v in palette(root / item["night"])) + "};")
                nights.append(symbol)
            else:
                nights.append("NULL")
            kind = item.get("weather", "dark_contrast")
            require(kind in ("none", "dark_contrast", "contrast"), f"{name}: invalid weather policy {kind}")
            weather.append({"none": 0, "dark_contrast": 1, "contrast": 2}[kind])
        code += [f"static const u16 ALIGNED(4) sMapPaletteExtra_{name}[4][16] = {{", *rows, "};",
                 f"static const u16 *const sMapPaletteNight_{name}[4] = {{" + ", ".join(nights) + "};",
                 f"static const u8 sMapPaletteRefs_{name}[] = {{"]
        code += ["    " + ", ".join(map(str, row)) + "," for row in references]
        code.append("};")
        extensions.append(f"    {{&{name}, sMapPaletteExtra_{name}, sMapPaletteNight_{name}, sMapPaletteRefs_{name}, {len(references)}, "
                          + "{" + ", ".join(map(str, weather)) + "}},")
    code += ["const struct TilesetPaletteExtension gTilesetPaletteExtensions[] = {", *extensions,
             "    {0},", "};", "const struct MapPaletteProfile gMapPaletteProfiles[] = {", *profiles,
             "    {0},", "};", ""]
    return "\n".join(code), report


def enable(root, name):
    manifest_path = root / "data/tilesets/map_palettes.json"
    manifest = read_json(manifest_path)
    require(not any(v["tileset"] == name for v in manifest["tilesets"]), f"{name} already enabled")
    defs = definitions(root)
    require(name in defs, f"Unknown tileset {name}")
    source, body = defs[name]
    require(re.search(r'\.isSecondary\s*=\s*TRUE', body), "Only secondary tilesets can be extended")
    _, references = metatile_masks(source)
    require(all(p < 14 for row in references for p in row), "Resolve legacy UI palette references before enabling this tileset")
    sidecar = source.with_name("palette_refs.json")
    require(not sidecar.exists(), f"Refusing to overwrite {sidecar}")
    extras = [source.parent / "palettes" / f"extra_{i}.pal" for i in range(4)]
    require(all(not p.exists() for p in extras), "Extra palette files already exist")
    base_path = source.parent / "palettes/06.pal"
    base = base_path.read_text()
    palette(base_path)
    for path in extras:
        path.write_text(base)
    sidecar.write_text(json.dumps({"version": 1, "metatiles_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                                   "palettes": references}, indent=2) + "\n")
    manifest["tilesets"].append({"tileset": name, "palette_refs": sidecar.relative_to(root).as_posix(),
                                "palettes": [{"day": p.relative_to(root).as_posix()} for p in extras],
                                "effects": [], "dynamic_metatiles": []})
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--game", choices=["emerald", "firered"], default="emerald")
    parser.add_argument("--enable", metavar="TILESET", help="create authored extra palettes and sidecar without changing metatile IDs")
    args = parser.parse_args()
    try:
        if args.enable:
            enable(args.root.resolve(), args.enable)
        source, report = compile_project(args.root, read_json(args.root / "data/tilesets/map_palettes.json"), args.game)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(source)
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Map palettes: OK ({len(report)} extended layouts)")
        if not args.output:
            for item in report:
                print(f"{item['layout']}: peak {item['peak']}/{item['capacity']}")
    except (PaletteError, OSError, KeyError, ValueError) as error:
        print(f"Map palettes: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

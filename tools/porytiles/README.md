# Porytiles integration

This directory contains a source patch for upstream Porytiles commit
`4c244d587c3daf16447366d0d8398b84a28370fe` (2.0.0) and a build script. It does not
replace or modify a separately installed Porytiles.
Upstream: <https://github.com/grunt-lucas/porytiles>. Upstream's MIT license continues
to apply.

The patch teaches Porytiles the extended tilesets
([TILESET_CAPACITY_GUIDE.md](../../docs/TILESET_CAPACITY_GUIDE.md)) and extra map
palettes ([MAP_PALETTES.md](../../docs/MAP_PALETTES.md)), and fixes several
upstream bugs (see [Upstream fixes](#upstream-fixes)).

## Building

Porytiles is C++23. You need Git, CMake 3.20+, [uv](https://docs.astral.sh/uv/) (it
fetches the Python that generates Porytiles' config code), libpng, zlib, and GCC 15+ or
Clang 18+ with libc++. Then, from the project root:

```sh
python3 tools/porytiles/build.py --cxx g++-15 --install ~/.local/bin
```

The script fetches the pinned source into `tools/porytiles/build/source`, applies
`extended.patch`, builds the `porytiles` binary under `build/cmake`, and with `--install`
copies a stripped binary to that directory. `--test` also builds and runs the test suite,
`--jobs N` controls compilation, `--directory PATH` uses an external build directory, and
`--no-build` only prepares patched source. Existing checkouts at another commit are
rejected.

Without root access, a recent GCC and the libraries can come from conda-forge, for
example with micromamba:

```sh
micromamba create -n porytiles -c conda-forge gxx=15 libpng zlib ninja
python3 tools/porytiles/build.py --cxx ~/micromamba/envs/porytiles/bin/g++ \
    --prefix-path ~/micromamba/envs/porytiles --install ~/.local/bin
```

The binary links its C++ runtime statically, so it runs without that environment.

## Using it with this project

For a step-by-step guide, see [the Porytiles tutorial](../../docs/PORYTILES_TUTORIAL.md).

Porytiles reads its project settings from `porytiles/config.yaml`, which this project already has.
Run it from the project root. The commands are listed by `porytiles --help`, for example
`porytiles create-tileset --secondary gTileset_MyTown` and
`porytiles compile-tileset gTileset_MyTown`. Run `make` afterwards: its tileset capacity
and map palette checks validate what Porytiles wrote.

A secondary tileset is compiled together with a Porytiles-managed primary so it can reuse
the primary's tiles and palettes. None of the project's primaries are Porytiles-managed, so give a
new secondary its own palettes instead, in `porytiles/tilesets/gTileset_MyTown/config.yaml`:

```yaml
tileset:
  primary_pairing:
    mode: "off"
```

### Extended tilesets

Porytiles reads `NUM_TILES_IN_PRIMARY_EXTENDED` and `NUM_TILES_IN_SECONDARY_EXTENDED`
from `include/tile_cache.h`, so primary tilesets hold up to 1024 tiles and secondary
tilesets up to 2048 with no configuration. When a compiled tileset uses a tile past the
first 512 of either sheet, Porytiles writes `metatile_tiles_ext.bin` beside its
`metatiles.bin`. When a primary tileset first needs one, Porytiles also sets its
`.isCompressed` to `FALSE` and declares its tiles uncompressed, which extended primaries
require.

Animated tiles are placed at the start of each sheet, inside the range the tile cache
can pin.

### Extra palettes

A secondary tileset can use logical palettes 14–17 once it is listed in
`data/tilesets/map_palettes.json`. Either enable it with
`python3 tools/map_palettes/compiler.py --enable gTileset_X`, or set this in its
`porytiles/tilesets/gTileset_X/config.yaml`:

```yaml
tileset:
  extra_palettes: true
```

The compile then packs colours into palettes 6–17 and writes `palette_refs.json`, the
four `palettes/extra_N.pal` files, and the manifest entry if it is missing. Other manifest
fields (`effects`, `dynamic_metatiles`, and so on) are left for you to edit.

Porytiles' `true_color` `tiles.png` mode stores palette × 16 + colour in 8 bits, which
cannot express palettes past 15. A tileset with extra palettes therefore gets a greyscale
`tiles.png`. The tile data is the same.

## Limits

- Porytiles can import and decompile primary tilesets only. Secondary tilesets are
  created and compiled in Porytiles, and compile only in `optimize` mode, which repacks
  tiles and palettes. Keep editing the existing secondary tilesets in the patched
  Porymap (`opalmap`).
- Porytiles override palettes (`porytiles_src/palettes`) cover palettes 0–15 only.

## Upstream fixes

These are independent of this project and could be sent upstream:

- GCC failed to compile `constexpr std::string` constants longer than its small-string
  buffer.
- `metatiles.bin` was written in text mode, which corrupts it on Windows.
- Compiling a secondary tileset decompiled its existing metatiles against its own
  `tiles.png` with absolute tile ids, reading past the end of the tile list. That step is
  only needed outside `optimize` mode and is now skipped there. The decompiler also checks
  its bounds now.
- A secondary tileset with more colours than its own palettes hold passed validation and
  then crashed in the palette packer. Validation now uses the packer's budget.
- Colour-limit errors always said the "16th" colour was the first one over the limit.
- One `#define` that Porytiles couldn't evaluate hid every other define in the same
  header.
- Importing a tileset whose animation driver function is empty failed.
- Importing a tileset with `.isCompressed = FALSE` declared compressed tiles.
- Compiling a secondary tileset with primary pairing `off` crashed.
- A compile after changing only a tileset's config was skipped as "nothing to do".
- The Doxygen docs build is optional (`-DPORYTILES_BUILD_DOCS=OFF`).

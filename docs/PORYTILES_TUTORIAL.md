# Making a new tileset with Porytiles

This guide walks you through making a brand-new tileset for Pokékyoto, from drawing it to
walking on it in game. You draw normal pictures; Porytiles turns them into the files
the game and Porymap use.

## What you need

- A terminal open in the Pokékyoto folder. In VS Code, that's **Terminal → New Terminal**.
- Porytiles. Type `porytiles --version` and press Enter. If it prints a version, you're
  set. If it says "command not found", ask whoever maintains your setup to build it
  ([tools/porytiles/README.md](../tools/porytiles/README.md)).
- [opalmap](https://github.com/cynderquil/opalmap), the patched Porymap.
- An image editor that can save PNGs with transparency (Aseprite, LibreSprite,
  Photoshop...).

## Words used here

- **Tile**: an 8×8 pixel square.
- **Metatile**: a 16×16 block made of 2×2 tiles. It's what you paint maps with in
  Porymap.
- **Layers**: every metatile has three layers stacked on top of each other: **bottom**,
  **middle** and **top**. The player walks over the bottom and middle layers and under the
  top layer, so put tree tops and roofs on the top layer.
- **Primary tileset**: the shared tileset a map uses for common things (grass, water,
  paths). Pokékyoto's already exist; you won't make one in this guide.
- **Secondary tileset**: the tileset for one town or area. This guide makes one of
  these.

## Step 1: Pick a name

Tileset names start with `gTileset_` followed by words written together with capital
letters, for example `gTileset_MyTown`. This guide uses `gTileset_MyTown`; replace it with
your own name everywhere.

## Step 2: Give the tileset its settings

Create the folder `porytiles/tilesets/gTileset_MyTown` and, inside it, a file called
`config.yaml` containing:

```yaml
tileset:
  primary_pairing:
    mode: "off"
```

This tells Porytiles the tileset gets its own colours instead of borrowing the primary
tileset's. (Keep the quotes around `"off"`.)

## Step 3: Create the tileset

In the terminal, run:

```sh
porytiles create-tileset --secondary gTileset_MyTown
```

This makes the folder `data/tilesets/secondary/my_town/` with two parts:

| Folder | What it is |
|---|---|
| `porytiles_src/` | **Your** files. You edit these. |
| `porytiles_bin/` | Files Porytiles makes for the game and Porymap. Never edit these by hand. |

It also adds the tileset to `src/data/tilesets/headers.h` (and two other files there) so
the game knows about it.

## Step 4: Draw your metatiles

Open the three pictures in `data/tilesets/secondary/my_town/porytiles_src/`:
`bottom.png`, `middle.png` and `top.png`. Each one is one layer.

The rules:

- The pictures are **128 pixels wide**: 8 metatiles per row. Make them taller in steps of
  16 pixels to add more rows. All three pictures must be the same size.
- Each **16×16 square is one metatile**, counted left to right, top to bottom, starting at
  0. The square at the same spot in all three pictures belongs to the same metatile.
- **Magenta (255, 0, 255)** means "nothing here" (fully transparent pixels work too).
  Don't use magenta for anything you want to see.
- Colours:
  - Each 8×8 tile can use at most **15 colours**.
  - The whole tileset can use about **120 colours**. Turning on extra palettes (below)
    raises that to **180**.
  - The GBA can't show every colour, so keep each red, green and blue value a multiple
    of 8 (0, 8, 16 … 248). Other values get rounded.
- **Never move or delete existing metatiles** once maps use them. Maps remember metatiles
  by their number, so shifting them scrambles every map that uses the tileset. Add new
  ones at the end.

## Step 5: Say what each metatile does

By default every metatile is plain ground you can walk on. To make one special (tall
grass, water, a ledge...), open `porytiles_src/attributes.csv`. It starts with:

```
id,behavior
```

Add a line per special metatile: its number, a comma, and a behaviour from
`include/constants/metatile_behaviors.h`. For example, to make metatile 0 tall grass:

```
id,behavior
0,MB_TALL_GRASS
```

Whether the player can walk *through* a metatile is set later in Porymap when painting
the map (the collision layer), not here.

## Step 6: Compile

```sh
porytiles compile-tileset gTileset_MyTown
```

No message means it worked. If something's wrong, Porytiles prints what and where, for
example a tile with too many colours, and even draws the problem pixels. Fix your picture
and run the command again.

## Step 7: Use it in Porymap

1. Open Pokékyoto in the patched Porymap. If it was already open, use **File → Reload Project**.
2. Open the map you want to use it on (or create a new one).
3. In the **Metatiles** panel on the right, set **Secondary Tileset** to `gTileset_MyTown`.
4. Paint the map with your metatiles.

Only paint maps with it in Porymap. Don't change the tileset in Porymap's tileset editor:
your PNGs are the real source, and Porytiles would overwrite (or refuse to overwrite) those
changes.

## Step 8: Build and play

```sh
make -j
```

This also checks the tileset for problems. Then play your ROM and walk around your new
area.

## Changing the tileset later

Every time you edit the pictures or `attributes.csv`:

1. Save your pictures.
2. Run `porytiles compile-tileset gTileset_MyTown`.
3. In Porymap, **File → Reload Project**.
4. Run `make -j`.

## Big tilesets

Pokékyoto tilesets can hold up to **2048 different tiles** (primary tilesets 1024). You don't
need to do anything: Porytiles removes duplicate tiles (including flipped copies) and
handles the extra space itself.

## More colours: extra palettes

If your tileset needs more than about 120 colours, add `extra_palettes: true` to its
`config.yaml`:

```yaml
tileset:
  extra_palettes: true
  primary_pairing:
    mode: "off"
```

Compile again. The tileset now has four more palettes (up to about 180 colours) and is
added to `data/tilesets/map_palettes.json`. Porymap shows it the same way; only the
`tiles.png` preview in `porytiles_bin` turns grey, which is normal. A map can only show so
many palettes at once, and `make` tells you if a map needs too many in one area. See
[MAP_PALETTES.md](MAP_PALETTES.md) for the details.

## Troubleshooting

| What you see | What to do |
|---|---|
| `porytiles: command not found` | Porytiles isn't installed. See [tools/porytiles/README.md](../tools/porytiles/README.md). |
| Compile does nothing after a change | It only recompiles when a file changed. Make sure you saved your pictures. Add `--no-verify-checksums` to force it. |
| "hit limit of … unique tiles" | Too many different tiles. Reuse more tiles, or split the art into two tilesets. |
| "global color count violation" | Too many colours. Merge similar colours, or turn on extra palettes. |
| An error about "extrinsic transparency" | You used magenta (255, 0, 255) in the art or a palette. Change it to another colour. |
| "Automatic primary pairing found no layouts" or "not Porytiles-managed" | The tileset's `config.yaml` is missing `mode: "off"`, or isn't in `porytiles/tilesets/<name>/`. |
| "Changes present in Porymap assets" | Something edited the files in `porytiles_bin`. Redo that change in your PNGs, then compile with `--no-verify-checksums`. |
| `make` reports a tileset or palette error | Read the message: it names the map and what went over a limit. |

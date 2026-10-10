# Kyoto foreground cloud weather and Paraglider

Native source integration for `dursem/pokekyoto`, based on master
`17149d664f15f4edb50dd050d90818bdeb603ed6` (the triple-metatile fork of
pokeemerald-expansion 1.17.0).

This revision keeps the three foreground cloud shapes, player/NPC silhouettes,
and the Paraglider Key Item. The lower animated cloud-surface renderer and its
atlas assets have been removed. Cloud weather no longer rewrites BG tilemaps,
reserves BG palette 13, or reserves any background character tiles. Your map's
existing tiles and triple layers remain managed by the native Kyoto engine.

## Install and build

The accompanying ZIP contains `Kyoto_Foreground_Install/install.sh`, a complete
patch against the base commit, and an upgrade patch against the previous
Clouds + Glide source package (`0bc21df2`). Do not install the old package first.

Place the installer directory inside the repository, beside `Makefile`, and run
from the repository root in Ubuntu/WSL:

```sh
bash Kyoto_Foreground_Install/install.sh
make -j4
```

The installer requires saved/committed tracked changes, validates a patch before
making changes, creates a feature branch, and applies the changes to the working
tree and Git index. It does not reset the repository, commit, push, or merge.
The package directory is excluded locally from source commits. Review and
commit the changes with GitHub Desktop, then publish the feature branch.

The ordinary build produces `pokeemerald.gba`. The production source does not
auto-grant the item or replace maps, tilesets, event scripts, or the intro.
No binary-address patch or HMA metadata is required.

## Enable cloud weather and gliding

| Setting | Value | Result |
| --- | --- | --- |
| Map weather | `WEATHER_SUNNY_CLOUDS` | Three drifting foreground cloud shapes |
| Air terrain behavior | `MB_GLIDE_CLOUD` (`0xD7`) | Passable only while gliding |
| Landing behavior | `MB_GLIDE_LANDING` (`0xD8`) | Launch and landing surface |
| Key Item | `ITEM_PARAGLIDER` (`874`) | Use from Bag or register for SELECT |

Use the existing customized Porymap. Reload it after source installation to
read the new behavior names. Cloud tiles are now ordinary map artwork: no blue
marker tile or special surface palette is needed. Cloud terrain behavior and
visual artwork are independent choices.

Give the item through an event script:

```asm
    giveitem ITEM_PARAGLIDER, 1
```

The new item ID is appended, preserving all previous item IDs. Set landings to
passable collision and a compatible elevation. Stand on a landing, face an
adjacent air tile, and use the Paraglider. Move in four directions. Landing
puts the glider away after the step finishes. Midair dismounting is rejected.

NPC current/previous cells and map edges stay blocked. Place a landing before
warps or connections. Do not remove the item or force Surf/bike states while
airborne. Glide state is derived from cloud terrain plus item ownership after
menus/loading; there is no save-block extension. Cold-boot saved-game continue
still needs explicit route-level validation.

## Art and palettes

`graphics/glide/paraglider.png` is the indexed 64x256 build sheet: four 64x64
poses, south/north/west/east. Individual pose PNGs are editing exports. The
fixed runtime palette is `paraglider.gbapal`, with a matching `.pal` source.
`item.png` is the 24x24 Bag icon.

Foreground runtime assets live in `graphics/weather/kyoto_clouds/`. The
`editing/` directory contains the large, medium and small PNGs and the complete
indexed 64x704 cell sheet. Repack an edited cell sheet with Python/Pillow:

```sh
python3 tools/rebuild_kyoto_clouds.py graphics/weather/kyoto_clouds/editing/cloud_cells.png graphics/weather/kyoto_clouds
```

Keep index 0 transparent and the silhouette shade slots 4..12 intact. Cloud
and silhouette colors follow the project's existing weather/time palette
handling. This feature does not install another clock or day/night cycle.

## Runtime integration

The foreground uses eleven 64x64 OBJ cells (704 OBJ tiles) and a 22,528-byte
heap buffer. The paraglider uses one further 64x64 sprite allocation (64 OBJ
tiles) and palette tag `0x11FE`. Keep this tag unique. Allocation failures
preserve native actors and glide terrain permissions.

The weather buffer is freed before full-screen menu allocation. Full-screen
callback changes invalidate old weather sprites, and rendering waits for
recreation on returning to the field. Custom menus that allocate before
changing callback2 need the same early cleanup entry point as Bag/Trainer Card.
The fade guard keeps field reconstruction black until the foreground is ready.

Silhouettes are prepared after the map palette frame transaction from current OAM and
pending sprite animation uploads, then transferred after native VBlank/DMA.
The ARM pixel shader executes in an 832-byte stack buffer with a linker size
assertion. The lower-layer IWRAM raster routine has been removed; a stack
headroom assertion remains to catch future over-allocation.

## Validation (mGBA 0.10.5)

The production source builds. A temporary 20x20 fixture with air terrain,
landings and an NPC was used for runtime tests; it is not included in the
production changes. Both normal and triple native BG tilemaps were compared
against VRAM, with native character-block selection intact.

Passed: walking blocked at air terrain, launching from both Bag and SELECT,
four-direction movement, NPC collision, rejected midair dismounting, automatic
landing, and five Bag round trips. Bag palette banks 0 and 1 match the project's
original `gBagScreenMale_Pal`. Foreground sprites and glider return after Bag.

Performance: 1,800 updates in 1,800 video frames while idle; 89 in 90 in each
horizontal scrolling sample. Update detection uses both cloud sprite position
and camera coordinates, since their screen motions can cancel while walking
left. These measurements are close to GBA's 59.73 Hz; they are not a blanket
60-fps guarantee for every map/NPC load. The recording is real emulator output
at native timing, enlarged with nearest-neighbor scaling.

Not yet covered: physical hardware, every Kyoto route, battles/scripted warps
while gliding, and a cold boot continuing a saved game above clouds. Emulator
save states used in the test harness are not a substitute for that last test.

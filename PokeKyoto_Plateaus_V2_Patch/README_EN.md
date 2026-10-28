# PokéKyoto: approved plateaus/name-border V2, source-only port

This is an **installable source patch**, not another ROM overlay. It ports the
approved binary V2 behavior into the user's existing optimized decomp. No FRLG
update; no changes to feature flags, species, sound, overworld objects or maps.

## Installation

Python 3.9+ and the standard library are sufficient for installation. Extract
this package into the existing PokéKyoto project; do not replace that project
with an older GitHub snapshot.

```bash
cd /home/dave-/PokemonDev/pokekyoto
python3 PokeKyoto_Plateaus_V2_Patch/install.py . --check
python3 PokeKyoto_Plateaus_V2_Patch/install.py . --apply
make -j4
python3 PokeKyoto_Plateaus_V2_Patch/install.py . --rom-report
```

Run each next command only if the preceding command succeeded. `--check` is
read-only and is also the default. The installer does not run a compiler,
network request or Git write. Use ordinary dependency-aware `make`: no `-B`,
no `NODEP=1`, and no broad clean command. Only one normal rebuild is necessary.

## Exactly nine project files

| File | Change |
|---|---|
| `include/battle_bg.h` | Declare `KyotoBattlePlatformX`. |
| `src/battle_bg.c` | Shared X-anchor helper, using the displayed environment override; include `battle_interface.h` and `contest.h`. |
| `src/battle_anim_mons.c` | Apply the helper to the existing X/X2 lookup only. |
| `src/battle_controllers.c` | Apply the helper to trainer intro and both trainer-slide paths. |
| `src/battle_message.c` | Restore the leftmost pixel column of the normal dynamic action-name window. |
| `graphics/battle_environment/kyoto_grass/tiles.png` | Approved shifted grass graphics, indexed source. |
| `graphics/battle_environment/kyoto_grass/map.bin` | Corresponding uncompressed source tilemap. |
| `graphics/battle_environment/kyoto_land/tiles.png` | Approved shifted land graphics, indexed source. |
| `graphics/battle_environment/kyoto_land/map.bin` | Corresponding uncompressed source tilemap. |

No full `.c` files are overwritten. Only checked, narrowly scoped blocks are
edited; unrelated local code is retained. Asset fingerprints and palettes are
checked before any write. Divergent assets or relevant modified code are
rejected rather than guessed at. Already-applied edits are idempotent.

The reference sources were inspected in `dursem/pokekyoto`, corresponding to
commit `add41daf166fdd251b1bb14e7b1dc22c3ed3b61b`. That commit is a compatibility
reference, **not** a reason to reset the user's local optimized project.

## Geometry and behavior

The upper grass platform is translated +13 px; the upper land platform -2 px.
Both end at x=238 in the 240px-wide viewport, leaving pixel 239 as background.
Single-layout anchors are player/opponent 60/178 for grass and 64/174 for plain
land. Other environments/layouts and contests retain their original X values.
All Y logic, species-specific offsets, elevation and sprite art are unchanged.

`GetBattleEnvironmentOverride()` is used instead of the raw environment global,
matching the existing binary V2 hook's behavior. The helper lives after that
static function. `battle_interface.h` is included explicitly: it declares
`GetBattlerCoordsIndex` and `BATTLE_COORDS_SINGLES`.

Opponent trainer dialogue slides already add 32 to their X value. The new
expression is `KyotoBattlePlatformX(battler, 208) - 32`; the subsequent +32
remains. Thus unsupported environments still start from 176 and end at 208.

The normal action-name window remains 11x2 tiles at tile (1,37), palette 12,
base block 0x170. It is filled with palette index 1, then its first pixel column
is restored to index 4. The installer validates this template. The nickname
string, ` do?`, font fitting, and text coordinates are not changed. Do not copy
the complete baked action background into the dynamic name window: it contains
lettering that must remain covered by the dynamic window.

## Compression and ROM footprint

The source build retains its existing `.4bpp.smol` and `.smolTM` conversions.
No V1 LZ77 stream, hard-coded ROM pointer, trampoline, or appended abandoned
V1/V2 data is installed. The V2 binary used safe LZ77; this source port feeds
identical referenced pixels/tilemaps into the native decomp pipeline instead.

The 128x56 indexed land PNG contains 99 used tiles plus 13 zero-filled padding
tiles. Those extra blank tiles are not referenced by the map. They complete the
normal rectangular tilesheet; all visible V2 pixels remain identical.

The exact rebuilt size is not known in advance. Measure `__rom_end` from the
new linker map. The ROM may still be padded to 32 MiB. This visual patch uses a
small amount of code/asset space, but does not re-enable removed features.

## Backups, errors and restore

The installer creates `pokekyoto-plateaus-v2-backup-<timestamp>-<id>` beside the
project, not inside it. It stores original changed source/assets, available
`.gba/.elf/.map` artifacts, checksums, and an actual unified source diff against
the user's local files. Four binary-payload annotations accompany the code diff;
use the supplied payload files for those assets.

The generated diff can be reviewed by the developer; the normal installer is
preferred because it also validates assets and saves backups. No branch or
commit is created. For Git, stage only the nine paths listed above.

Ctrl+C/write errors during source application roll back touched source files.
A later, separately executed failed `make` does **not** automatically undo the
source patch. Restore using the exact backup path printed by the installer:

```bash
python3 PokeKyoto_Plateaus_V2_Patch/install.py . --restore /absolute/backup/path
make -j4
```

Restore refuses to overwrite subsequently edited source files and does not
silently replace the current ROM. The old ROM remains in `build_before` in the
backup. No saves are modified. Never use `git reset --hard` for this patch.

## Validation performed for this package

- 36 installer/transaction tests, including idempotence, unchanged unrelated
  edits/config/audio, source-layout guards, rollback on write error/Ctrl+C,
  backup corruption, symlinks, busy-build rejection and generated-diff checking.
- Isolated C execution: 4,992 anchor cases, 26 opponent-slide compensation cases,
  all 1,408 name-window pixels, and normal/non-normal window guards.
- Isolated source helper and border code compile for ARM7TDMI Thumb using Clang.
- All referenced tiles and full tilemaps checked against the approved V2 ROM:
  SHA-256 `a9a17b7691e45063fb867e3eb783d16c0cc00d3e64882a75b500bb22232e12b8`.

**Not performed here:** a complete rebuilt decomp ROM or an mGBA run of that
new source build. The previously approved binary V2 was tested in mGBA, but that
does not imply this package has been linked and gameplay-tested as a full ROM.
The isolated tests use stubbed game queries and source fixtures, not a full
checkout. Runtime coverage of opposing trainers, doubles/contests, all moves
and all Pokémon is not claimed. This port does not fix species Y-offsets.

Reproduce the local developer checks:

```bash
python3 -m unittest discover -s PokeKyoto_Plateaus_V2_Patch/tests -v
python3 PokeKyoto_Plateaus_V2_Patch/tests/check_c_logic.py
# Optional (requires Pillow; read-only, never modifies the ROM):
python3 PokeKyoto_Plateaus_V2_Patch/tests/check_v2_assets.py /path/to/approved_V2.gba
```

After the real build, test grass and land encounters, trainer intro/dialogue,
move animations, action/move menus, Bag/party return, long nicknames, doubles
and contests. Use a copy of a regular save, not a V1/V2 emulator savestate.

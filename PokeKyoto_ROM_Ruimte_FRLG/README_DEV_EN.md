# V2 regression fix

The V1 NPC index incorrectly merged `object_event_pic_tables_followers.h` and
`object_event_graphics_info_followers.h` with legacy NPC declarations. Static
identifiers such as `sPicTable_Pidgey` legitimately recur in these separate
source contexts. V2 excludes the two follower headers from that NPC index;
it does not delete/rename them, silently choose a conflicting NPC definition,
change the Pidgey family setting, or alter object/species IDs.

The original exception was reproduced against the actual V1 installer using
a representative test fixture. The same fixture passes in V2. The full user
source tree is NOT present locally; no full-game dry run/build is claimed.

The V1 source marker is deliberately retained to prevent double application.
V2 also prints its version before planning and the last 25 build-log lines on
a failed build. Build flags and patch scope otherwise remain unchanged.

# Implementation notes / limitations

Status: source transformation package; full PokéKyoto ROM compilation and mGBA
regression tests have NOT been performed here. GitHub branch creation returned
403, so no remote source change was made. Fifteen synthetic tests cover the parser,
conditional declaration registration, rollback integrity, and host-C checks of
both Emerald/FRLG preprocessing modes and gimmick gating. See TEST_RESULTS.txt.

Run `python3 kyoto_optimize.py /path/to/pokekyoto` for a dry run. Add
`--build-compare` to run matched full builds before/after and measure `__rom_end`.
Failed optimized builds roll back the source and restore baseline binaries.
The output directory must be outside the repository and stores originals, patch,
logs, ELF/map/ROM files and a JSON report. It is not pushed to GitHub.

The source planner disables the existing species-family/form toggles; it does
not renumber species, object graphics, moves, items or abilities. Only the five
specified built-in gimmick rows and their Can*/Activate* entry points are blocked.
It preserves the implementations behind early returns for reversibility. It does
NOT claim that every gimmick helper/graphic/item has been removed from the binary.

For FRLG NPCs, the planner follows existing ObjectEventGraphicsInfo -> frame
arrays -> people graphics dependencies, reuses pre-existing object IDs, adds only
needed declarations behind !IS_FRLG and registers NPC palettes ahead of the
palette-list sentinel. It does not change IS_FRLG, player-avatar mappings, field
movement, maps/events, trainer sprites, or audio. Player-state graphics are
excluded. Full build/runtime validation of this transformation is still needed.

The extraction intentionally stops on ambiguous declarations, unsupported nested
conditional contexts, absent function bodies or unexpected source structure.
It also refuses Domain references in a targeted gimmick function/table entry.
No broad game-mode switch, hard-coded new numeric IDs, or raw ROM patching is used.

Reference source: dursem/pokekyoto @ add41daf166fdd251b1bb14e7b1dc22c3ed3b61b.
The planner works on the caller's current tree and checks structure instead of
resetting it to this commit. All generations, regional forms and primal forms
are kept. Ultra Necrozma is disabled; ordinary/fused Necrozma is retained. Custom
Fakemon reusing Ultra Beast species slots require manual consideration.

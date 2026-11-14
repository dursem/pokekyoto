# Tree encounter v4

A temporary active tree is signalled by five staggered small green leaf sprites. The map
art and collision data remain unchanged. The bottom two metatiles refer to one
shared encounter, consumed before battle (also on running away or losing).

## Leaf density in v4

`TREE_ENCOUNTER_LEAF_COUNT` is now 5 (previously 3). The 336-frame cycle and
first three trajectories are unchanged. Added motions: start 12/duration 172
at (12,-20), drifting left; start 162/duration 170 at (21,-16), drifting right.
Both use the same existing four 8x8 frames, foliage indices, camera anchoring and
live palette copying. This adds two reserved sprite slots, not new tile graphics
or palette slots. At most five leaves are visible at once; their starts are
staggered, not a simultaneous burst. All trajectories finish within the cycle.

## Starter progress gate

The shared SyncMap entrypoint checks FLAG_SYS_POKEMON_GET and
VAR_BIRCH_LAB_STATE >= 2, configured in include/config/tree_encounter.h.
The flag alone is insufficient: the Route101 starter script sets it BEFORE
ChooseStarter. State 2 is written AFTER ChooseStarter returns with the starter
already in the party; state 3 completes the later lab dialogue.

While locked there is no active profile, no accumulated step counter, no RNG
consumption by this feature and no leaf allocation. Debug activation returns
status 5 with a starter message, never a bypass. A progress change clears stale
active/armed/burst state and starts a fresh timer on unlock, even without a map
change. Progress 2 -> 3 -> 5 does not reset an already active tree.
PlayerCanUseTrees also checks the gate and still requires a healthy non-egg
party member on land. No starter-species comparison is made, allowing evolution
or storage of the original starter. No save-layout changes or new persistent
flags are required. All configured maps share the gate.

## Map and art configuration

`src/data/tree_encounters.h` contains the existing 2x3 broad/tall tree shapes and
independent weighted test tables for LittlerootTown (Sendai), Route101 and Route103.
Both bottom metatiles and the complete upper shape must match. The General primary
tileset is required. Leaf pixels and palette handling are unchanged from v1.

`include/config/tree_encounter.h` retains 25-100 steps between activations, an
8-step retry, a 6-tile candidate radius and a 12-tile maximum active distance.
`TREE_ENCOUNTER_PATH_RADIUS` is 12 by default and must include SEARCH_RADIUS + 1.

## Reachability, not just empty space

FindNearbyTree builds one four-neighbour reachable set from the actual player
position. A candidate must have at least one reached standing tile directly south
of a bottom metatile, with a compatible trunk elevation and no reserved interaction.
Debug activation uses the exact same predicate, with distance ranking only after
reachability. Random selection uses reservoir sampling over eligible trees only.

The read-only path filter rejects:

* map bounds and collision bits;
* any of the registered crown/trunk graphic IDs, even if collision is zero;
* occupied current or previous NPC positions;
* elevation changes (zero-elevation transition tiles retain the player's layer);
* warp coordinates and active scripted coordinate events;
* water requiring surfing, jumps, stairs, bridge behaviors, ice, forced movement,
  and unknown movement behaviors.

Ordinary ground, grass, shallow water, puddles and sand are allowed when other
checks pass. Directional blocking is checked at both ends of every edge through
`IsMetatileDirectionallyImpassable` with a local ObjectEvent containing the source
behavior. The actual player ObjectEvent is never modified.

Do not replace this call with GetCollisionAtCoords/GetObjectObjectCollidesWith:
those engine APIs can change movement state or trigger/despawn overworld Pokémon.
No events or scripts are executed during this search.

The path is deliberately conservative: searches do not cross maps, use special
movement mechanics, change walking layers, or leave the local radius. A tree
reachable only by such a route is skipped. This does not simulate arbitrary future
story logic. Existing current-map collision and active coordinate conditions are
read as they stand.

After each real player step an active tree is rechecked, stopping the search as
soon as an approach is reached. A newly blocked approach deactivates the tree.
Standing still does not continuously re-run pathfinding for moving NPCs; the next
player step triggers revalidation. Interaction still requires physical adjacency,
the active tree position and compatible elevation.

## Memory, cleanup and failure behavior

The bounded heap workspace is 2504 bytes with the default radius. It is allocated
with AllocZeroedUnchecked and always freed before returning. The checked allocator
would fatal-error on exhaustion in this engine, so it is intentionally not used.
Allocation failure refuses activation. No per-frame BFS or persistent search buffer
is added. Gameplay performance and integration still need emulator validation.

Sprites retain the existing ownership cookie/handle checks, menu reload recovery,
camera anchoring and live foliage palette copy. Source hooks are unchanged from v1.

## Installer and scope of testing

V4 supports fresh installations and hash-verified upgrades from the supplied v1,
v2 or v3. Manual module/config edits abort before writing. Existing encounter tables
are kept; only missing Sendai/Route101 registrations or their missing default
tables are added. Source preflight verifies the starter script's ordering, along
with the existing tree metatile identity and engine integration points.
No map, tile, collision, starter script or save file is overwritten.

The regression, reachability and starter-gate tests are rerun with the five-leaf
count, plus new leaf-density and v3-upgrade tests. See TEST_RESULTS.txt for actual
results. C tests use host engine mocks, ASan and UBSan, not the GBA engine.
No full ROM build or mGBA test of v4 has been performed. The retained ROM identity
report belongs to the v3 inspection of pokeemerald(9).gba. Existing emulator videos
do not demonstrate this v4 leaf-density change.

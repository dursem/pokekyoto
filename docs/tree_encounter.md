# Tree encounter v2

A temporary active tree is signalled by three small green leaf sprites. The map
art and collision data remain unchanged. The bottom two metatiles refer to one
shared encounter, consumed before battle (also on running away or losing).

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

V2 supports fresh installation and hash-verified upgrades from the shipped v1.
User-edited encounter tables are preserved; edited module/config files abort the
upgrade. No whole engine source, map, tileset or save file is replaced. Backups are
outside the project; restoring an upgrade backup restores the pre-upgrade files.

Host tests cover the original behavior, isolated islands/trees, gates, detours,
water with collision zero, elevation transitions, corner cutting, NPC reservations,
warps, active/inactive coordinate scripts, directional barriers, debug/random
selection, local budget limits, heap failures and read-only operation. Ninety-six
random mazes are cross-checked against a separate flood-fill implementation.

These are C tests with engine mocks plus installer tests. No full ROM build or
mGBA test of v2 has been completed. The earlier video used a temporary test ROM
integration and is not evidence for this pathfinding module.

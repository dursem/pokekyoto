// Tree encounter. Original trees remain map graphics; only leaves are sprites.
#ifdef TREE_ENCOUNTER_HOST_TEST
#include "tree_encounter_test_stubs.h"
#else
#include "global.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "map_palette.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "random.h"
#include "sprite.h"
#include "wild_encounter.h"
#include "constants/map_groups.h"
#include "constants/flags.h"
#include "constants/vars.h"
#include "constants/metatile_behaviors.h"
#include "constants/species.h"
#endif
#include "tree_encounter.h"
#include "tree_encounter_sway.h"
#include "config/tree_encounter.h"

struct TreeEncounterShape
{
    u16 tiles[6];
};

struct TreeEncounterSlot
{
    enum Species species;
    u8 minLevel;
    u8 maxLevel;
    u8 weight;
};

struct TreeEncounterMap
{
    u8 mapGroup;
    u8 mapNum;
    const struct TreeEncounterSlot *slots;
    u8 count;
};

struct TreeEncounterState
{
    const struct MapLayout *layout;
    const struct TreeEncounterMap *profile;
    s16 x; // bottom-left tile in the current, padded map grid
    s16 y;
    u16 steps;
    u16 targetSteps;
    u16 leafFrame;
    u8 mapGroup;
    u8 mapNum;
    u8 shape;
    bool8 initialized;
    bool8 starterUnlocked;
    bool8 active;
    bool8 armed;
    bool8 burst;
};

struct TreeLeafMotion
{
    u16 start;
    u16 duration;
    s8 x;
    s8 y;
    s8 direction;
};

extern const struct Tileset gTileset_General;
extern const u8 EventScript_TreeEncounter[];

#include "data/tree_encounters.h"

static EWRAM_DATA struct TreeEncounterState sTree = {0};
static EWRAM_DATA u8 sLeafSpriteIds[TREE_ENCOUNTER_LEAF_COUNT] = {0};
static EWRAM_DATA bool8 sVisualsAllocated = FALSE;

static void TreeLeafCallback(struct Sprite *sprite);
static bool8 EnsureLeafSprites(void);
static bool8 TreeMatches(s16 x, s16 y, u32 shape);
static bool8 PlayerCanUseTrees(void);
static bool8 StarterProgressUnlocked(void);
static void SyncMap(void);

// Four 8x8, 4bpp frames. Only 3-4 pixels per leaf are visible.
// Pixel values are the ORIGINAL foliage palette indices (4, 5, 6 and 8).
static const u32 sTreeLeafTiles[] =
{
    0, 0, 0x00040000, 0x00065000, 0x00008000, 0, 0, 0,
    0, 0, 0,          0x00645000, 0,          0, 0, 0,
    0, 0, 0x00004000, 0x00065000, 0x00080000, 0, 0, 0,
    0, 0, 0x00050000, 0x00060000, 0x00080000, 0, 0, 0,
};

static const struct SpriteSheet sTreeLeafSheet =
{
    .data = sTreeLeafTiles,
    .size = sizeof(sTreeLeafTiles),
    .tag = TREE_ENCOUNTER_LEAF_TAG,
};

static const struct OamData sTreeLeafOam =
{
    .shape = SPRITE_SHAPE(8x8),
    .size = SPRITE_SIZE(8x8),
    .priority = 1,
};

static const struct SpriteTemplate sTreeLeafTemplate =
{
    .tileTag = TREE_ENCOUNTER_LEAF_TAG,
    .paletteTag = TREE_ENCOUNTER_LEAF_TAG,
    .oam = &sTreeLeafOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = TreeLeafCallback,
};

// Five staggered leaves per cycle; the first three retain their original motion.
// The extra leaves share the same four tiny frames and foliage palette.
static const struct TreeLeafMotion sLeafMotions[TREE_ENCOUNTER_LEAF_COUNT] =
{
    { 38, 176,  5, -14, -1 },
    { 84, 164, 25, -11,  1 },
    {126, 168, 16, -18,  1 },
    { 12, 172, 12, -20, -1 },
    {162, 170, 21, -16,  1 },
};
static const s8 sLeafWobble[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};

#define TREE_SPRITE_COOKIE 0x5445

static s32 AbsDistance(s32 n)
{
    return n < 0 ? -n : n;
}

static u16 NextStepTarget(void)
{
    return TREE_ENCOUNTER_MIN_STEPS
        + Random() % (TREE_ENCOUNTER_MAX_STEPS - TREE_ENCOUNTER_MIN_STEPS + 1);
}

static bool8 SlotIsValid(const struct TreeEncounterSlot *slot)
{
    return slot->species != SPECIES_NONE && slot->weight != 0
        && slot->minLevel != 0 && slot->minLevel <= slot->maxLevel
        && slot->maxLevel <= MAX_LEVEL;
}

static u32 TableWeight(const struct TreeEncounterMap *profile)
{
    u32 i, total = 0;
    if (profile == NULL)
        return 0;
    for (i = 0; i < profile->count; i++)
        if (SlotIsValid(&profile->slots[i]))
            total += profile->slots[i].weight;
    return total;
}

static const struct TreeEncounterMap *FindProfile(void)
{
    u32 i;
    if (!TREE_ENCOUNTER_ENABLED || gMapHeader.mapLayout == NULL
     || gMapHeader.mapLayout->primaryTileset != &gTileset_General)
        return NULL;
    for (i = 0; i < ARRAY_COUNT(sTreeEncounterMaps); i++)
    {
        const struct TreeEncounterMap *profile = &sTreeEncounterMaps[i];
        if (profile->mapGroup == (u8)gSaveBlock1Ptr->location.mapGroup
         && profile->mapNum == (u8)gSaveBlock1Ptr->location.mapNum
         && TableWeight(profile) != 0)
            return profile;
    }
    return NULL;
}

static bool8 OwnLeafSprite(u32 index)
{
    u8 id = sLeafSpriteIds[index];
    return sVisualsAllocated && id < MAX_SPRITES && gSprites[id].inUse
        && gSprites[id].callback == TreeLeafCallback
        && gSprites[id].data[7] == TREE_SPRITE_COOKIE
        && gSprites[id].data[6] == (s16)index;
}

void TreeEncounter_ReleaseVisuals(void)
{
    u32 i;
    // TREE_ENCOUNTER_CANOPY_SWAY_V4_RELEASE
    TreeEncounterSway_Release();
    if (!sVisualsAllocated)
        return;
    for (i = 0; i < TREE_ENCOUNTER_LEAF_COUNT; i++)
    {
        // Menus may have reset the sprite array. Never destroy a reused slot.
        if (OwnLeafSprite(i))
            DestroySprite(&gSprites[sLeafSpriteIds[i]]);
        sLeafSpriteIds[i] = MAX_SPRITES;
    }
    FreeSpriteTilesByTag(TREE_ENCOUNTER_LEAF_TAG);
    FreeSpritePaletteByTag(TREE_ENCOUNTER_LEAF_TAG);
    sVisualsAllocated = FALSE;
}

void TreeEncounter_Reset(void)
{
    TreeEncounter_ReleaseVisuals();
    sTree = (struct TreeEncounterState){0};
}

static bool8 StarterProgressUnlocked(void)
{
    // The flag is set before ChooseStarter. The lab-state milestone is set
    // only after that choice/battle returns, when the starter is in the party.
    // Do not test the starter species: it may evolve or be stored later.
    return FlagGet(TREE_ENCOUNTER_STARTER_FLAG)
        && VarGet(TREE_ENCOUNTER_STARTER_VAR) >= TREE_ENCOUNTER_STARTER_MIN_STATE;
}

static void SyncMap(void)
{
    bool8 starterUnlocked = StarterProgressUnlocked();
    if (sTree.initialized
     && sTree.starterUnlocked == starterUnlocked
     && sTree.layout == gMapHeader.mapLayout
     && sTree.mapGroup == (u8)gSaveBlock1Ptr->location.mapGroup
     && sTree.mapNum == (u8)gSaveBlock1Ptr->location.mapNum)
        return;
    TreeEncounter_Reset();
    sTree.initialized = TRUE;
    sTree.layout = gMapHeader.mapLayout;
    sTree.mapGroup = gSaveBlock1Ptr->location.mapGroup;
    sTree.mapNum = gSaveBlock1Ptr->location.mapNum;
    sTree.starterUnlocked = starterUnlocked;
    // Pre-starter steps never accumulate. Unlocking starts a fresh timer.
    // Losing the milestone (debug/new save) also clears any active effect.
    sTree.profile = starterUnlocked ? FindProfile() : NULL;
    if (sTree.profile != NULL)
        sTree.targetSteps = NextStepTarget();
}

static bool8 PlayerCanUseTrees(void)
{
    u32 i;
    if (!StarterProgressUnlocked()
     || IsOverworldLinkActive()
     || TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING | PLAYER_AVATAR_FLAG_UNDERWATER))
        return FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES) != SPECIES_NONE
         && !GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SANITY_IS_EGG)
         && GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP) != 0)
            return TRUE;
    return FALSE;
}

static bool8 InCurrentMap(s16 x, s16 y)
{
    return sTree.layout != NULL && x >= MAP_OFFSET && y >= MAP_OFFSET
        && x < sTree.layout->width + MAP_OFFSET
        && y < sTree.layout->height + MAP_OFFSET;
}

static bool8 TreeMatches(s16 x, s16 y, u32 shape)
{
    u32 i;
    if (shape >= ARRAY_COUNT(sTreeEncounterShapes)
     || !InCurrentMap(x, y - 2) || !InCurrentMap(x + 1, y))
        return FALSE;
    for (i = 0; i < 6; i++)
        if (MapGridGetMetatileIdAt(x + i % 2, y - 2 + i / 2)
            != sTreeEncounterShapes[shape].tiles[i])
            return FALSE;
    return TRUE;
}

static bool8 ObjectOccupies(s16 x, s16 y)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (gObjectEvents[i].active
         && !gObjectEvents[i].isPlayer
         && ((gObjectEvents[i].currentCoords.x == x && gObjectEvents[i].currentCoords.y == y)
          || (gObjectEvents[i].previousCoords.x == x && gObjectEvents[i].previousCoords.y == y)))
            return TRUE;
    return FALSE;
}

static bool8 HasReservedInteraction(s16 x, s16 y)
{
    u32 i;
    if (ObjectOccupies(x, y) || ObjectOccupies(x + 1, y))
        return TRUE;
    if (gMapHeader.events != NULL)
        for (i = 0; i < gMapHeader.events->bgEventCount; i++)
        {
            const struct BgEvent *bg = &gMapHeader.events->bgEvents[i];
            if (bg->y + MAP_OFFSET == y
             && (bg->x + MAP_OFFSET == x || bg->x + MAP_OFFSET == x + 1))
                return TRUE;
        }
    return FALSE;
}

// Reachability is deliberately read-only. Calling GetCollisionAtCoords here
// would be unsafe: that API may trigger/despawn overworld Pokemon and change
// movement state. Only its pure directional metatile check is reused.
#define TREE_PATH_SIDE (2 * TREE_ENCOUNTER_PATH_RADIUS + 1)
#define TREE_PATH_CELLS (TREE_PATH_SIDE * TREE_PATH_SIDE)
#if TREE_ENCOUNTER_PATH_RADIUS < TREE_ENCOUNTER_SEARCH_RADIUS + 1
#error "Tree encounter path radius must include both front-of-trunk tiles."
#endif
#if TREE_PATH_CELLS > 65535
#error "Tree encounter path queue indices must fit in u16."
#endif

enum TreePathCellState
{
    TREE_PATH_UNKNOWN,
    TREE_PATH_BLOCKED,
    TREE_PATH_OPEN,
    TREE_PATH_REACHED,
};

// Temporary heap workspace (2504 bytes at radius 12), never kept between calls.
// Allocation failure means no activation, not an unverified reachable tree.
struct TreeReachability
{
    s16 left, top;
    u16 queue[TREE_PATH_CELLS];
    u8 state[TREE_PATH_CELLS];
    u8 behavior[TREE_PATH_CELLS];
};

static bool8 IsOrdinaryWalkingBehavior(u32 behavior)
{
    // Fail closed on water, ledges, ice/sliding, bridge layers, stairs, warps,
    // and unknown/custom movement behaviors. Some reachable trees can therefore
    // be skipped, but these mechanics can never create a fictitious path.
    switch (behavior)
    {
    case MB_NORMAL:
    case MB_TALL_GRASS:
    case MB_LONG_GRASS:
    case MB_LONG_GRASS_SOUTH_EDGE:
    case MB_SHORT_GRASS:
    case MB_DEEP_SAND:
    case MB_SAND:
    case MB_ASHGRASS:
    case MB_FOOTPRINTS:
    case MB_PUDDLE:
    case MB_SHALLOW_WATER:
    case MB_NO_RUNNING:
    case MB_INDOOR_ENCOUNTER:
    case MB_IMPASSABLE_EAST:
    case MB_IMPASSABLE_WEST:
    case MB_IMPASSABLE_NORTH:
    case MB_IMPASSABLE_SOUTH:
    case MB_IMPASSABLE_NORTHEAST:
    case MB_IMPASSABLE_NORTHWEST:
    case MB_IMPASSABLE_SOUTHEAST:
    case MB_IMPASSABLE_SOUTHWEST:
    case MB_IMPASSABLE_SOUTH_AND_NORTH:
    case MB_IMPASSABLE_WEST_AND_EAST:
        return TRUE;
    default:
        return FALSE;
    }
}

static bool8 IsTreeGraphicTile(s16 x, s16 y)
{
    u32 shape, tile;
    u32 id = MapGridGetMetatileIdAt(x, y);
    // Even if an individual tree tile was authored with collision 0, don't
    // count walking through its crown/trunk as an approach to another tree.
    for (shape = 0; shape < ARRAY_COUNT(sTreeEncounterShapes); shape++)
        for (tile = 0; tile < ARRAY_COUNT(sTreeEncounterShapes[shape].tiles); tile++)
            if (id == sTreeEncounterShapes[shape].tiles[tile])
                return TRUE;
    return FALSE;
}

static bool8 HasPathInterruptingEvent(s16 x, s16 y)
{
    u32 i;
    const struct MapEvents *events = gMapHeader.events;
    if (events == NULL)
        return FALSE;
    // A path must stay in this map and must not depend on a script letting
    // the player through. Inactive coordinate scripts do not block a path.
    for (i = 0; i < events->warpCount; i++)
        if (events->warps[i].x + MAP_OFFSET == x
         && events->warps[i].y + MAP_OFFSET == y)
            return TRUE;
    for (i = 0; i < events->coordEventCount; i++)
    {
        const struct CoordEvent *event = &events->coordEvents[i];
        if (event->x + MAP_OFFSET == x && event->y + MAP_OFFSET == y
         && event->script != NULL
         && (event->elevation == ELEVATION_TRANSITION || event->elevation == PlayerGetElevation())
         && (event->trigger == 0 || VarGet(event->trigger) == event->index))
            return TRUE;
    }
    return FALSE;
}

static bool8 IsClearApproach(s16 x, s16 y)
{
    u8 elevation;
    if (!InCurrentMap(x, y) || MapGridGetCollisionAt(x, y) != 0
     || ObjectOccupies(x, y) || IsTreeGraphicTile(x, y)
     || HasPathInterruptingEvent(x, y)
     || !IsOrdinaryWalkingBehavior(MapGridGetMetatileBehaviorAt(x, y)))
        return FALSE;
    elevation = MapGridGetElevationAt(x, y);
    // Keep the search on the player's walking layer. Transition tiles retain
    // that layer; they are NOT a shortcut to every elevation on the map.
    return elevation == PlayerGetElevation() || elevation == ELEVATION_TRANSITION;
}

static bool8 CanFaceTrunkFromBelow(s16 x, s16 y)
{
    u8 elevation = MapGridGetElevationAt(x, y);
    return IsClearApproach(x, y + 1)
        && (elevation == ELEVATION_TRANSITION || elevation == ELEVATION_MULTI_LEVEL
         || elevation == PlayerGetElevation());
}

static bool8 CanSelectTree(s16 x, s16 y, u32 shape)
{
    return TreeMatches(x, y, shape) && !HasReservedInteraction(x, y)
        && (CanFaceTrunkFromBelow(x, y) || CanFaceTrunkFromBelow(x + 1, y));
}

static s32 PathCellIndex(const struct TreeReachability *path, s16 x, s16 y)
{
    s32 dx = x - path->left;
    s32 dy = y - path->top;
    if (dx < 0 || dy < 0 || dx >= TREE_PATH_SIDE || dy >= TREE_PATH_SIDE)
        return -1;
    return dy * TREE_PATH_SIDE + dx;
}

static bool8 PathCellIsOpen(struct TreeReachability *path, s16 x, s16 y, s32 index)
{
    if (index < 0)
        return FALSE;
    if (path->state[index] == TREE_PATH_UNKNOWN)
    {
        if (!IsClearApproach(x, y))
            path->state[index] = TREE_PATH_BLOCKED;
        else
        {
            path->state[index] = TREE_PATH_OPEN;
            path->behavior[index] = MapGridGetMetatileBehaviorAt(x, y);
        }
    }
    return path->state[index] != TREE_PATH_BLOCKED;
}

static bool8 PathReachesApproach(const struct TreeReachability *path, s16 x, s16 y)
{
    u32 side;
    for (side = 0; side < 2; side++)
    {
        s32 index = PathCellIndex(path, x + side, y + 1);
        if (index >= 0 && path->state[index] == TREE_PATH_REACHED
         && CanFaceTrunkFromBelow(x + side, y))
            return TRUE;
    }
    return FALSE;
}

static bool8 BuildReachableArea(struct TreeReachability *path, bool8 stopAtTree, s16 treeX, s16 treeY)
{
    static const s8 dx[4] = {0, 0, -1, 1};
    static const s8 dy[4] = {1, -1, 0, 0};
    static const enum Direction dirs[4] = {DIR_SOUTH, DIR_NORTH, DIR_WEST, DIR_EAST};
    struct ObjectEvent probe = {0};
    s16 px, py;
    s32 start;
    u32 head = 0, tail = 0;
    PlayerGetDestCoords(&px, &py);
    path->left = px - TREE_ENCOUNTER_PATH_RADIUS;
    path->top = py - TREE_ENCOUNTER_PATH_RADIUS;
    start = PathCellIndex(path, px, py);
    // Unknown/multilevel player elevation is ambiguous: refuse to guess.
    if (PlayerGetElevation() == ELEVATION_TRANSITION
     || PlayerGetElevation() == ELEVATION_MULTI_LEVEL
     || !PathCellIsOpen(path, px, py, start))
        return FALSE;
    path->state[start] = TREE_PATH_REACHED;
    path->queue[tail++] = start;
    while (head < tail)
    {
        u32 direction;
        u16 current = path->queue[head++];
        s16 x = path->left + current % TREE_PATH_SIDE;
        s16 y = path->top + current / TREE_PATH_SIDE;
        if (stopAtTree && PathReachesApproach(path, treeX, treeY))
            return TRUE;
        probe.currentMetatileBehavior = path->behavior[current];
        for (direction = 0; direction < ARRAY_COUNT(dirs); direction++)
        {
            s16 nextX = x + dx[direction];
            s16 nextY = y + dy[direction];
            s32 index = PathCellIndex(path, nextX, nextY);
            if (!PathCellIsOpen(path, nextX, nextY, index)
             || path->state[index] == TREE_PATH_REACHED
             || IsMetatileDirectionallyImpassable(&probe, nextX, nextY, dirs[direction]))
                continue;
            // Nodes are enqueued once, with no diagonals/corner cutting.
            if (tail >= TREE_PATH_CELLS)
                return FALSE;
            path->state[index] = TREE_PATH_REACHED;
            path->queue[tail++] = index;
        }
    }
    return !stopAtTree || PathReachesApproach(path, treeX, treeY);
}

static bool8 ActiveTreeIsReachable(void)
{
    bool8 reachable;
    struct TreeReachability *path;
    if (!CanSelectTree(sTree.x, sTree.y, sTree.shape))
        return FALSE;
    path = AllocZeroedUnchecked(sizeof(*path));
    if (path == NULL)
        return FALSE;
    reachable = BuildReachableArea(path, TRUE, sTree.x, sTree.y);
    Free(path);
    return reachable;
}

static bool8 FindNearbyTree(bool8 nearest, s16 *foundX, s16 *foundY, u8 *foundShape)
{
    s16 px, py, x, y;
    u32 shape, count = 0;
    s32 bestDistance = 0x7FFFFFFF;
    struct TreeReachability *path = AllocZeroedUnchecked(sizeof(*path));
    if (path == NULL)
        return FALSE;
    if (!BuildReachableArea(path, FALSE, 0, 0))
    {
        Free(path);
        return FALSE;
    }
    PlayerGetDestCoords(&px, &py);
    for (y = py - TREE_ENCOUNTER_SEARCH_RADIUS; y <= py + TREE_ENCOUNTER_SEARCH_RADIUS; y++)
        for (x = px - TREE_ENCOUNTER_SEARCH_RADIUS; x <= px + TREE_ENCOUNTER_SEARCH_RADIUS; x++)
            for (shape = 0; shape < ARRAY_COUNT(sTreeEncounterShapes); shape++)
            {
                s32 distance;
                bool8 choose;
                if (!CanSelectTree(x, y, shape) || !PathReachesApproach(path, x, y))
                    continue;
                distance = AbsDistance(y - py) + (px <= x ? x - px : AbsDistance(px - x - 1));
                count++;
                choose = nearest ? distance < bestDistance : Random() % count == 0;
                if (choose)
                {
                    *foundX = x;
                    *foundY = y;
                    *foundShape = shape;
                    bestDistance = distance;
                }
            }
    Free(path);
    return count != 0;
}

static bool8 ActivateTree(s16 x, s16 y, u8 shape)
{
    TreeEncounter_ReleaseVisuals();
    sTree.active = FALSE;
    sTree.armed = FALSE;
    sTree.burst = FALSE;
    if (!EnsureLeafSprites())
        return FALSE;
    sTree.x = x;
    sTree.y = y;
    sTree.shape = shape;
    sTree.active = TRUE;
    sTree.leafFrame = 0;
    sTree.steps = 0;
    return TRUE;
}

static void DeactivateTree(void)
{
    TreeEncounter_ReleaseVisuals();
    sTree.active = FALSE;
    sTree.armed = FALSE;
    sTree.burst = FALSE;
    sTree.steps = 0;
    sTree.targetSteps = sTree.profile != NULL ? NextStepTarget() : 0;
}

void TreeEncounter_OnStep(void)
{
    s16 px, py, x, y;
    u8 shape;
    SyncMap();
    if (sTree.profile == NULL || !PlayerCanUseTrees())
        return;
    if (sTree.active)
    {
        PlayerGetDestCoords(&px, &py);
        if (!TreeMatches(sTree.x, sTree.y, sTree.shape)
         || AbsDistance(px - sTree.x) > TREE_ENCOUNTER_MAX_DISTANCE
         || AbsDistance(py - sTree.y) > TREE_ENCOUNTER_MAX_DISTANCE
         || !ActiveTreeIsReachable())
            DeactivateTree();
        return;
    }
    if (sTree.burst || ++sTree.steps < sTree.targetSteps)
        return;
    sTree.steps = 0;
    sTree.targetSteps = TREE_ENCOUNTER_RETRY_STEPS;
    if (FindNearbyTree(FALSE, &x, &y, &shape))
        ActivateTree(x, y, shape);
}

const u8 *TreeEncounter_GetInteractionScript(s16 x, s16 y, u8 elevation)
{
    s16 px, py;
    u8 targetElevation;
    SyncMap();
    if (!sTree.active || sTree.profile == NULL || !PlayerCanUseTrees()
     || y != sTree.y || (x != sTree.x && x != sTree.x + 1)
     || !TreeMatches(sTree.x, sTree.y, sTree.shape))
        return NULL;
    PlayerGetDestCoords(&px, &py);
    if (AbsDistance(px - x) + AbsDistance(py - y) != 1)
        return NULL;
    targetElevation = MapGridGetElevationAt(x, y);
    if (elevation != ELEVATION_TRANSITION && targetElevation != ELEVATION_TRANSITION
     && targetElevation != ELEVATION_MULTI_LEVEL && elevation != targetElevation)
        return NULL;
    sTree.armed = TRUE;
    return EventScript_TreeEncounter;
}

void TreeEncounter_CreateMon(void)
{
    const struct TreeEncounterSlot *chosen = NULL;
    u32 i, roll, total;
    u8 level;
    gSpecialVar_Result = FALSE;
    SyncMap();
    if (!sTree.armed || !sTree.active || !PlayerCanUseTrees()
     || !TreeMatches(sTree.x, sTree.y, sTree.shape))
        return;
    sTree.armed = FALSE;
    total = TableWeight(sTree.profile);
    if (total == 0)
        return;
    roll = Random() % total;
    for (i = 0; i < sTree.profile->count; i++)
    {
        const struct TreeEncounterSlot *slot = &sTree.profile->slots[i];
        if (!SlotIsValid(slot))
            continue;
        if (roll < slot->weight)
        {
            chosen = slot;
            break;
        }
        roll -= slot->weight;
    }
    if (chosen == NULL)
        return;
    level = chosen->minLevel + Random() % (chosen->maxLevel - chosen->minLevel + 1);
    // Interacting with a visible tree is deliberate: Repel is not consulted.
    // Use the game's own generator, retaining its normal wild-mon rules.
    gIsFishingEncounter = FALSE;
    gIsSurfingEncounter = FALSE;
    CreateWildMon(chosen->species, level);
    sTree.active = FALSE;
    sTree.burst = TRUE;
    sTree.leafFrame = 126;
    sTree.steps = 0;
    sTree.targetSteps = NextStepTarget();
    gSpecialVar_Result = TRUE;
}

void TreeEncounter_EndEffect(void)
{
    sTree.burst = FALSE;
    TreeEncounter_ReleaseVisuals();
}

void TreeEncounter_DebugActivate(void)
{
    s16 x, y;
    u8 shape;
    SyncMap();
    gSpecialVar_Result = 5;
    if (!sTree.starterUnlocked)
        return;
    gSpecialVar_Result = 0;
    if (sTree.profile == NULL)
        return;
    gSpecialVar_Result = 2;
    if (!PlayerCanUseTrees())
        return;
    gSpecialVar_Result = 3;
    if (!FindNearbyTree(TRUE, &x, &y, &shape))
        return;
    gSpecialVar_Result = ActivateTree(x, y, shape) ? 1 : 4;
}

void TreeEncounter_DebugClear(void)
{
    SyncMap();
    DeactivateTree();
    gSpecialVar_Result = TRUE;
}

static void TreeLeafCallback(struct Sprite *sprite)
{
    // Positions are written AFTER CameraUpdate, before BuildOamBuffer.
    // A unique callback also lets cleanup identify only our own sprites.
    (void)sprite;
}

static bool8 EnsureLeafSprites(void)
{
    u32 i;
    bool8 valid = sVisualsAllocated
        && GetSpriteTileStartByTag(TREE_ENCOUNTER_LEAF_TAG) != 0xFFFF
        && IndexOfSpritePaletteTag(TREE_ENCOUNTER_LEAF_TAG) != 0xFF;
    for (i = 0; i < TREE_ENCOUNTER_LEAF_COUNT; i++)
        if (!OwnLeafSprite(i))
            valid = FALSE;
    if (valid)
        return TRUE;
    TreeEncounter_ReleaseVisuals();
    for (i = 0; i < TREE_ENCOUNTER_LEAF_COUNT; i++)
        sLeafSpriteIds[i] = MAX_SPRITES;
    sVisualsAllocated = TRUE;
    if (LoadSpriteSheet(&sTreeLeafSheet) == 0xFFFF
     || AllocSpritePalette(TREE_ENCOUNTER_LEAF_TAG) == 0xFF)
    {
        TreeEncounter_ReleaseVisuals();
        return FALSE;
    }
    for (i = 0; i < TREE_ENCOUNTER_LEAF_COUNT; i++)
    {
        u8 id = CreateSpriteAtEnd(&sTreeLeafTemplate, 0, 0, 0);
        if (id == MAX_SPRITES)
        {
            TreeEncounter_ReleaseVisuals();
            return FALSE;
        }
        sLeafSpriteIds[i] = id;
        gSprites[id].data[7] = TREE_SPRITE_COOKIE;
        gSprites[id].data[6] = i;
        gSprites[id].coordOffsetEnabled = TRUE;
        gSprites[id].animPaused = TRUE;
        gSprites[id].invisible = TRUE;
    }
    return TRUE;
}

static u32 TreePaletteBank(void)
{
    if (MapPalettesActive())
        return MapPalettesGetPhysicalBank(TREE_ENCOUNTER_TREE_PALETTE);
    return TREE_ENCOUNTER_TREE_PALETTE;
}

// TREE_ENCOUNTER_PLAYER_FRONT_V4_1_BEGIN
// Keep the original leaf layer except where it intersects the player.
// Change only the leaf's OAM sort key; never move/recolour the player or tree.
static void TreeEncounter_SetLeafDrawOrder(struct Sprite *sprite)
{
    u8 playerId = GetPlayerAvatarSpriteId();
    const struct Sprite *player;
    s32 leafX, leafY, playerX, playerY;

    // Restore the existing leaf layer every frame, including after overlap ends.
    sprite->oam.priority = 1;
    sprite->subpriority = 0;
    if (playerId >= MAX_SPRITES)
        return;
    player = &gSprites[playerId];
    if (player == sprite || !player->inUse || player->invisible
     || player->centerToCornerVecX >= 0 || player->centerToCornerVecY >= 0)
        return;

    // Screen-space bounds include camera offsets and walking/jump offsets.
    // The normal field sprites are centred; negative corner vectors give
    // their half-width/height (and already include an affine double-size box).
    leafX = sprite->x + sprite->x2 + sprite->centerToCornerVecX;
    leafY = sprite->y + sprite->y2 + sprite->centerToCornerVecY;
    playerX = player->x + player->x2 + player->centerToCornerVecX;
    playerY = player->y + player->y2 + player->centerToCornerVecY;
    if (sprite->coordOffsetEnabled)
    {
        leafX += gSpriteCoordOffsetX;
        leafY += gSpriteCoordOffsetY;
    }
    if (player->coordOffsetEnabled)
    {
        playerX += gSpriteCoordOffsetX;
        playerY += gSpriteCoordOffsetY;
    }
    if (leafX >= playerX - 2 * player->centerToCornerVecX
     || playerX >= leafX - 2 * sprite->centerToCornerVecX
     || leafY >= playerY - 2 * player->centerToCornerVecY
     || playerY >= leafY - 2 * sprite->centerToCornerVecY)
        return;

    // BuildOamBuffer sorts by priority, then subpriority, then Y and index.
    // A strictly later key keeps the player in front regardless of walking Y.
    if (sprite->oam.priority > player->oam.priority)
        return; // The default leaf layer is already behind this player layer.
    sprite->oam.priority = player->oam.priority;
    if (player->subpriority < 255)
        sprite->subpriority = player->subpriority + 1;
    else if (player->oam.priority < 3)
        sprite->oam.priority = player->oam.priority + 1;
    else
        // No later sort slot exists. Suppress just this intersecting leaf
        // for this frame; the normal update restores visibility next frame.
        sprite->invisible = TRUE;
}
// TREE_ENCOUNTER_PLAYER_FRONT_V4_1_END

void TreeEncounter_UpdateSprites(void)
{
    s16 sx, sy;
    u32 i, tileStart, bank;
    SyncMap();
    if (!sTree.active && !sTree.burst)
    {
        TreeEncounter_ReleaseVisuals();
        return;
    }
    if (sTree.profile == NULL || !TreeMatches(sTree.x, sTree.y, sTree.shape))
    {
        DeactivateTree();
        return;
    }
    if (!EnsureLeafSprites())
        return;
    SetSpritePosToMapCoords(sTree.x, sTree.y, &sx, &sy);
    tileStart = GetSpriteTileStartByTag(TREE_ENCOUNTER_LEAF_TAG);
    bank = TreePaletteBank();
    for (i = 0; i < TREE_ENCOUNTER_LEAF_COUNT; i++)
    {
        const struct TreeLeafMotion *motion = &sLeafMotions[i];
        struct Sprite *sprite = &gSprites[sLeafSpriteIds[i]];
        s32 age = sTree.leafFrame - motion->start;
        sprite->invisible = TRUE;
        if (age < 0 || age >= motion->duration || bank == MAP_PALETTE_NONE)
            continue;
        sprite->x = sx + motion->x
            + motion->direction * (age * 7 / (motion->duration - 1))
            + sLeafWobble[age * 16 / motion->duration];
        sprite->y = sy + motion->y + age * 39 / (motion->duration - 1);
        sprite->oam.tileNum = tileStart + ((age / 21) % 4);
        // Copy the current palette slot: menus can invalidate its old number.
        sprite->oam.paletteNum = IndexOfSpritePaletteTag(TREE_ENCOUNTER_LEAF_TAG);
        sprite->invisible = sprite->x + gSpriteCoordOffsetX < -8
            || sprite->x + gSpriteCoordOffsetX > 248
            || sprite->y + gSpriteCoordOffsetY < -8
            || sprite->y + gSpriteCoordOffsetY > 168;
        // TREE_ENCOUNTER_PLAYER_FRONT_V4_1_UPDATE
        if (!sprite->invisible)
            TreeEncounter_SetLeafDrawOrder(sprite);
    }
    sTree.leafFrame = (sTree.leafFrame + 1) % TREE_ENCOUNTER_LEAF_CYCLE;
    // TREE_ENCOUNTER_CANOPY_SWAY_V4_UPDATE
    TreeEncounterSway_Update(sTree.x, sTree.y, sTree.shape, sTree.leafFrame);
}

void TreeEncounter_UpdatePalette(void)
{
    u32 bank, dest, i;
    u8 palette;
    if (!sVisualsAllocated)
        return;
    bank = TreePaletteBank();
    palette = IndexOfSpritePaletteTag(TREE_ENCOUNTER_LEAF_TAG);
    if (bank == MAP_PALETTE_NONE || palette == 0xFF)
        return;
    dest = OBJ_PLTT_ID(palette);
    // Called AFTER UpdatePaletteFade and time/weather updates. Copy BOTH
    // buffers so weather, day/night and fades cannot flash bright leaves.
    for (i = 0; i < 16; i++)
    {
        gPlttBufferUnfaded[dest + i] = gPlttBufferUnfaded[bank * 16 + i];
        gPlttBufferFaded[dest + i] = gPlttBufferFaded[bank * 16 + i];
    }
}

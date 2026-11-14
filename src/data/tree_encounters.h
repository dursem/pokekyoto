#ifndef GUARD_DATA_TREE_ENCOUNTERS_H
#define GUARD_DATA_TREE_ENCOUNTERS_H

// Bottom-left and bottom-right belong to ONE tree. All six metatiles must
// match, so isolated trunk fragments and other tilesets cannot activate.
// Verified against the supplied Kyoto ROM. Coordinates are relative to the
// bottom-left metatile: top row y-2, middle row y-1, bottom row y.
static const struct TreeEncounterShape sTreeEncounterShapes[] =
{
    { { 0x003, 0x004, 0x00B, 0x00C, 0x013, 0x014 } }, // broad green tree
    { { 0x005, 0x006, 0x00D, 0x00E, 0x015, 0x016 } }, // tall green tree
};

// Starter TEST tables. These are independent of the normal grass encounters.
// Entry order: species, minimum level, maximum level, relative weight.
// Replace these species/levels to set the intended game balance.
static const struct TreeEncounterSlot sTreeMons_Sendai[] =
{
    { SPECIES_HOOTHOOT,   5, 7, 30 },
    { SPECIES_SPINARAK,   4, 6, 25 },
    { SPECIES_PINECO,     5, 7, 20 },
    { SPECIES_AIPOM,      5, 8, 15 },
    { SPECIES_EXEGGCUTE,  5, 7,  8 },
    { SPECIES_HERACROSS,  6, 9,  2 },
};

static const struct TreeEncounterSlot sTreeMons_Route101[] =
{
    { SPECIES_HOOTHOOT,  3, 5, 35 },
    { SPECIES_SPINARAK,  3, 5, 30 },
    { SPECIES_PINECO,    4, 6, 20 },
    { SPECIES_AIPOM,     4, 6, 13 },
    { SPECIES_HERACROSS, 5, 7,  2 },
};

static const struct TreeEncounterSlot sTreeMons_Route103[] =
{
    { SPECIES_HOOTHOOT,  5, 7, 30 },
    { SPECIES_PINECO,    5, 7, 30 },
    { SPECIES_EXEGGCUTE, 5, 8, 20 },
    { SPECIES_AIPOM,     6, 8, 15 },
    { SPECIES_HERACROSS, 7, 9,  5 },
};

#define TREE_MAP(map, mons) { MAP_GROUP(map), MAP_NUM(map), mons, ARRAY_COUNT(mons) }
static const struct TreeEncounterMap sTreeEncounterMaps[] =
{
    // Sendai still uses this internal map ID in the project.
    TREE_MAP(MAP_LITTLEROOT_TOWN, sTreeMons_Sendai),
    TREE_MAP(MAP_ROUTE101, sTreeMons_Route101),
    TREE_MAP(MAP_ROUTE103, sTreeMons_Route103),
};
#undef TREE_MAP

#endif

#include "global.h"
#include "event_data.h"
#include "field_move.h"
#include "fldeff.h"
#include "item.h"
#include "pokemon.h"
#include "constants/items.h"
#include "fldeff_misc.h"
#include "party_menu.h"
#include "strings.h"
#include "constants/field_move.h"
#include "constants/moves.h"
#include "constants/party_menu.h"

static bool32 IsAlwaysFalse(enum FieldMove fieldMove)
{
    return FALSE;
}

static bool32 IsAlwaysTrue(enum FieldMove fieldMove)
{
    return TRUE;
}

static bool32 HasBadgeForFieldMove(enum FieldMove fieldMove)
{
    return FlagGet(gFieldMoveInfo[fieldMove].arg + FLAG_BADGE01_GET);
}

const struct FieldMoveUnlock gFieldMoveUnlocks[FIELD_MOVE_UNLOCK_COUNT] =
{
    [CANT_UNLOCK] =
    {
        .isUnlockedFunc = IsAlwaysFalse,
        .lockedMessage = gText_EmptyString2,
    },
    [ALWAYS_UNLOCKED] =
    {
        .isUnlockedFunc = IsAlwaysTrue,
        .lockedMessage = gText_EmptyString2,
    },
    [BADGE_UNLOCK] =
    {
        .isUnlockedFunc = HasBadgeForFieldMove,
        .lockedMessage = gText_CantUseUntilNewBadge,
    },
};

#define FLAG_TO_BADGE(flag) flag - FLAG_BADGE01_GET

const struct FieldMoveInfo gFieldMoveInfo[FIELD_MOVES_COUNT] =
{
    [FIELD_MOVE_CUT] =
    {
        .fieldMoveFunc = SetUpFieldMove_Cut,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_CUT,
        .partyMsgID = PARTY_MSG_NOTHING_TO_CUT,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE02_GET) : FLAG_TO_BADGE(FLAG_BADGE01_GET),
    },

    [FIELD_MOVE_FLASH] =
    {
        .fieldMoveFunc = SetUpFieldMove_Flash,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_FLASH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE01_GET) : FLAG_TO_BADGE(FLAG_BADGE02_GET),
    },

    [FIELD_MOVE_ROCK_SMASH] =
    {
        .fieldMoveFunc = SetUpFieldMove_RockSmash,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_ROCK_SMASH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE06_GET) : FLAG_TO_BADGE(FLAG_BADGE03_GET),
    },

    [FIELD_MOVE_STRENGTH] =
    {
        .fieldMoveFunc = SetUpFieldMove_Strength,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_STRENGTH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE04_GET),
    },

    [FIELD_MOVE_SURF] =
    {
        .fieldMoveFunc = SetUpFieldMove_Surf,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_SURF,
        .partyMsgID = PARTY_MSG_CANT_SURF_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE05_GET),
    },

    [FIELD_MOVE_FLY] =
    {
        .fieldMoveFunc = SetUpFieldMove_Fly,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_FLY,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE03_GET) : FLAG_TO_BADGE(FLAG_BADGE06_GET),
    },

    [FIELD_MOVE_DIVE] =
    {
        .fieldMoveFunc = SetUpFieldMove_Dive,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_DIVE,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE07_GET),
    },

    [FIELD_MOVE_WATERFALL] =
    {
        .fieldMoveFunc = SetUpFieldMove_Waterfall,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_WATERFALL,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE07_GET) : FLAG_TO_BADGE(FLAG_BADGE08_GET),
    },

    [FIELD_MOVE_TELEPORT] =
    {
        .fieldMoveFunc = SetUpFieldMove_Teleport,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_TELEPORT,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_DIG] =
    {
        .fieldMoveFunc = SetUpFieldMove_Dig,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_DIG,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_SECRET_POWER] =
    {
        .fieldMoveFunc = SetUpFieldMove_SecretPower,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SECRET_POWER,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_MILK_DRINK] =
    {
        .fieldMoveFunc = SetUpFieldMove_SoftBoiled,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_MILK_DRINK,
        .partyMsgID = PARTY_MSG_NOT_ENOUGH_HP,
    },

    [FIELD_MOVE_SOFT_BOILED] =
    {
        .fieldMoveFunc = SetUpFieldMove_SoftBoiled,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SOFT_BOILED,
        .partyMsgID = PARTY_MSG_NOT_ENOUGH_HP,
    },

    [FIELD_MOVE_SWEET_SCENT] =
    {
        .fieldMoveFunc = SetUpFieldMove_SweetScent,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SWEET_SCENT,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },
    [FIELD_MOVE_ROCK_CLIMB] =
    {
        .fieldMoveFunc = SetUpFieldMove_RockClimb,
#if OW_ROCK_CLIMB_FIELD_MOVE
        .unlockType = ALWAYS_UNLOCKED,
#else
        .unlockType = CANT_UNLOCK,
#endif
        .moveID = MOVE_ROCK_CLIMB,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .hideIfLocked = TRUE,
    },
    [FIELD_MOVE_DEFOG] =
    {
        .fieldMoveFunc = SetUpFieldMove_Defog,
#if OW_DEFOG_FIELD_MOVE
        .unlockType = ALWAYS_UNLOCKED,
#else
        .unlockType = CANT_UNLOCK,
#endif
        .moveID = MOVE_DEFOG,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .hideIfLocked = TRUE,
    },
};

// These HMs remain usable as ordinary attacks. Field use does not teach a move.
static const struct
{
    enum Item item;
    u16 receivedFlag;
} sFieldHms[] =
{
    [FIELD_MOVE_CUT]        = {ITEM_HM01, FLAG_RECEIVED_HM_CUT},
    [FIELD_MOVE_FLASH]      = {ITEM_HM05, FLAG_RECEIVED_HM_FLASH},
    [FIELD_MOVE_ROCK_SMASH] = {ITEM_HM06, FLAG_RECEIVED_HM_ROCK_SMASH},
    [FIELD_MOVE_STRENGTH]   = {ITEM_HM04, FLAG_RECEIVED_HM_STRENGTH},
    [FIELD_MOVE_SURF]       = {ITEM_HM03, FLAG_RECEIVED_HM_SURF},
    [FIELD_MOVE_FLY]        = {ITEM_HM02, FLAG_RECEIVED_HM_FLY},
    [FIELD_MOVE_DIVE]       = {ITEM_HM08, FLAG_RECEIVED_HM_DIVE},
    [FIELD_MOVE_WATERFALL]  = {ITEM_HM07, 0},
};

bool32 FieldMove_IsHM(enum FieldMove fieldMove)
{
    return (u32)fieldMove < ARRAY_COUNT(sFieldHms);
}

bool32 FieldMove_HasHM(enum FieldMove fieldMove)
{
    if (!FieldMove_IsHM(fieldMove))
        return FALSE;
    // Inventory also covers custom item-giver scripts and imported FRLG maps.
    return CheckBagHasItem(sFieldHms[fieldMove].item, 1)
        || CheckPCHasItem(sFieldHms[fieldMove].item, 1)
        || (!IS_FRLG && sFieldHms[fieldMove].receivedFlag != 0
            && FlagGet(sFieldHms[fieldMove].receivedFlag));
}

bool32 FieldMove_CanUseMon(struct Pokemon *mon, enum FieldMove fieldMove)
{
    enum Species species;
    enum Move move;
    if ((u32)fieldMove >= FIELD_MOVES_COUNT)
        return FALSE;
    species = GetMonData(mon, MON_DATA_SPECIES);
    if (species == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
        return FALSE;
    move = FieldMove_GetMoveId(fieldMove);
    if (!FieldMove_IsHM(fieldMove))
        return MonKnowsMove(mon, move);
    return FieldMove_HasHM(fieldMove)
        && (MonKnowsMove(mon, move) || CanLearnTeachableMove(species, move));
}

u8 FieldMove_FindPartyUser(enum FieldMove fieldMove)
{
    u8 i;
    for (i = 0; i < PARTY_SIZE; i++)
        if (FieldMove_CanUseMon(&gParties[B_TRAINER_PLAYER][i], fieldMove))
            return i;
    return PARTY_SIZE;
}

/* Player item PC ported from pret/pokefirered src/item_pc.c. */
#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "palette.h"
#include "sprite.h"
#include "text.h"
#include "window.h"
#include "string_util.h"
#include "malloc.h"
#include "data.h"
#include "decompress.h"
#include "item.h"
#include "list_menu.h"
#include "menu.h"
#include "menu_helpers.h"
#include "party_menu.h"
#include "scanline_effect.h"
#include "sound.h"
#include "strings.h"
#include "task.h"
#include "text_window.h"
#include "frlg_pc.h"
#include "constants/items.h"
#include "constants/songs.h"
#include "constants/rgb.h"

static const u32 sItemPcTiles[] = INCBIN_U32("graphics/frlg_pc/storage.4bpp.lz");
static const u32 sItemPcTilemap[] = INCBIN_U32("graphics/frlg_pc/storage.bin.lz");
static const u16 sItemPcPalette[] = INCBIN_U16("graphics/frlg_pc/storage.gbapal");
static const u8 sText_Quantity[] = _("×{STR_VAR_1}");
static const u8 sText_Withdraw[] = _("Withdraw");
static const u8 sText_Give[] = _("Give");
static const u8 sText_WithdrawTitle[] = _("WITHDRAW\nITEM");
static const u8 sText_DepositTitle[] = _("DEPOSIT ITEM");
static const u8 sText_CannotHold[] = _("That item can't be held.");
static const u8 sText_ReturnToPC[] = _("Return to the PC.");
static const u8 sText_WherePlaceItem[] = _("Where should the {STR_VAR_1}\nbe placed?");
static const u8 sText_ItemSelected[] = _("{STR_VAR_1} is selected.");
static const u8 sText_WithdrewItem[] = _("Withdrew {STR_VAR_2}\n{STR_VAR_1}(s).");
static const u8 sText_BagFull[] = _("There is no more room in the Bag.");
static const u8 sText_WithdrawHowMany[] = _("Withdraw how many\n{STR_VAR_1}(s)?");
static const u8 sText_NoPokemon[] = _("There is no POKéMON.");
static const u32 sDepositTiles[] = INCBIN_U32("graphics/frlg_pc/bag_bg.4bpp.lz");
static const u32 sDepositTilemap[] = INCBIN_U32("graphics/frlg_pc/bag_bg.bin.lz");
static const u16 sDepositPalette[] = INCBIN_U16("graphics/frlg_pc/bag_bg.gbapal");
static const u16 sDepositFemalePalette[] = INCBIN_U16("graphics/frlg_pc/bag_bg_female.gbapal");
static const u16 sDepositListTilemap[] = INCBIN_U16("graphics/frlg_pc/bag_list.bin");
static const u8 sText_DepositHowMany[] = _("Deposit how many\n{STR_VAR_1}(s)?");
static const u8 sText_DepositedItem[] = _("Deposited {STR_VAR_2}\n{STR_VAR_1}(s).");
static const u8 sText_PcFull[] = _("There is no room left to store items.");
static const u8 sText_ImportantItem[] = _("That's much too important to store!");

static u16 FrlgPcGetQuantity(u16 *quantity)
{
    return *quantity;
}

struct ItemPcResources
{
    MainCallback savedCallback;
    u8 moveModeOrigPos;
    u8 itemMenuIconSlot;
    u8 maxShowed;
    u8 nItems;
    u8 scrollIndicatorArrowPairId;
    u16 withdrawQuantitySubmenuCursorPos;
    s16 data[3];
    u16 pocketSwitchPosition;
    u8 pocketArrowsId;
};

struct ItemPcStaticResources
{
    MainCallback savedCallback;
    u16 scroll;
    u16 row;
    u8 initialized;
    bool8 deposit;
    u8 pocket;
    u16 pocketScroll[POCKETS_COUNT];
    u16 pocketRow[POCKETS_COUNT];
};

static EWRAM_DATA struct ItemPcResources * sItemPcState = NULL;
static EWRAM_DATA u8 * sItemPcBgTilemap = NULL;
static EWRAM_DATA struct ListMenuItem * sItemPcList = NULL;

static EWRAM_DATA struct ItemPcStaticResources sItemPcPosition = {};
static EWRAM_DATA u8 sItemPcSubwindows[3] = {};




static void ItemPc_RunSetup(void);
static bool8 ItemPc_DoGfxSetup(void);
static void BeginFrlgBagCurtain(bool8);
static void Task_FrlgBagCurtain(u8);
static void ItemPc_StartPocketSwitch(u8, s8);
static void Task_ItemPcSwitchPocket(u8);
static void ItemPc_FadeAndBail(void);
static void Task_ItemPcWaitFadeAndBail(u8 taskId);
static bool8 ItemPc_InitBgs(void);
static bool8 ItemPc_LoadGraphics(void);
static bool8 ItemPc_AllocateResourcesForListMenu(void);
static void ItemPc_BuildListMenuTemplate(void);
static void ItemPc_MoveCursorFunc(s32 itemIndex, bool8 onInit, struct ListMenu * list);
static void ItemPc_ItemPrintFunc(u8 windowId, u32 itemId, u8 y);
static void ItemPc_PrintOrRemoveCursorAt(u8 y, u8 state);
static void ItemPc_PrintWithdrawItem(void);
static void ItemPc_PlaceTopMenuScrollIndicatorArrows(void);
static void ItemPc_SetCursorPosition(void);
static void ItemPc_FreeResources(void);
static void Task_ItemPcTurnOff2(u8 taskId);
static u16 ItemPc_GetItemIdBySlotId(u16 itemIndex);
static u16 ItemPc_GetItemQuantityBySlotId(u16 itemIndex);
static void ItemPc_CountPcItems(void);
static void ItemPc_SetScrollPosition(void);
static void Task_ItemPcMain(u8 taskId);
static void ItemPc_MoveItemModeInit(u8 taskId, s16 pos);
static void Task_ItemPcMoveItemModeRun(u8 taskId);
static void ItemPc_InsertItemIntoNewSlot(u8 taskId, u32 pos);
static void ItemPc_MoveItemModeCancel(u8 taskId, u32 pos);
static void Task_ItemPcSubmenuInit(u8 taskId);
static void Task_ItemPcSubmenuRun(u8 taskId);
static void Task_ItemPcWithdraw(u8 taskId);
static void ItemPc_DoWithdraw(u8 taskId);
static void Task_ItemPcWaitButtonAndFinishWithdrawMultiple(u8 taskId);
static void Task_ItemPcWaitButtonWithdrawMultipleFailed(u8 taskId);
static void Task_ItemPcCleanUpWithdraw(u8 taskId);
static void ItemPc_WithdrawMultipleInitWindow(u16 slotId);
static void Task_ItemPcHandleWithdrawMultiple(u8 taskId);
static void Task_ItemPcGive(u8 taskId);
static void ItemPc_CB2_SwitchToPartyMenu(void);
static void ItemPc_CB2_ReturnFromPartyMenu(void);
static void gTask_ItemPcWaitButtonAndExitSubmenu(u8 taskId);
static void Task_ItemPcCancel(u8 taskId);
static void ItemPc_InitWindows(void);
static void ItemPc_AddTextPrinterParameterized(u8 windowId, u8 fontId, const u8 * str, u8 x, u8 y, u8 letterSpacing, u8 lineSpacing, u8 speed, u8 colorIdx);
static void ItemPc_SetBorderStyleOnWindow(u8 windowId);
static u8 ItemPc_GetOrCreateSubwindow(u8 idx);
static void ItemPc_DestroySubwindow(u8 idx);
static void ItemPc_PrintOnWindow5WithContinueTask(u8 taskId, const u8 * str, TaskFunc taskFunc);

static const struct BgTemplate sBgTemplates[2] = {
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 0
    }, {
        .bg = 1,
        .charBaseIndex = 3,
        .mapBaseIndex = 30,
        .priority = 1
    }
};

static const struct MenuAction sItemPcSubmenuOptions[] = {
    {sText_Withdraw,          {.void_u8 = Task_ItemPcWithdraw}},
    {sText_Give,         {.void_u8 = Task_ItemPcGive}},
    {gText_Cancel, {.void_u8 = Task_ItemPcCancel}}
};

static const u8 sTextColors[][3] = {
    {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY},
    {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY},
    {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_GRAY, TEXT_COLOR_DARK_GRAY},
    {TEXT_COLOR_TRANSPARENT, TEXT_DYNAMIC_COLOR_1, TEXT_COLOR_DARK_GRAY}
};

static const struct WindowTemplate sWindowTemplates[] = {
    {
        .bg = 0,
        .tilemapLeft = 7,
        .tilemapTop = 1,
        .width = 19,
        .height = 12,
        .paletteNum = 15,
        .baseBlock = 0x02bf
    }, {
        .bg = 0,
        .tilemapLeft = 5,
        .tilemapTop = 14,
        .width = 25,
        .height = 6,
        .paletteNum = 13,
        .baseBlock = 0x0229
    }, {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 1,
        .width = 5,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x0215
    }, {
        .bg = 0,
        .tilemapLeft = 24,
        .tilemapTop = 15,
        .width = 5,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x0201
    }, {
        .bg = 0,
        .tilemapLeft = 22,
        .tilemapTop = 13,
        .width = 7,
        .height = 6,
        .paletteNum = 15,
        .baseBlock = 0x01d7
    }, {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 26,
        .height = 4,
        .paletteNum = 11,
        .baseBlock = 0x016f
    }, DUMMY_WIN_TEMPLATE
};

static const struct WindowTemplate sSubwindowTemplates[] = {
    {
        .bg = 0,
        .tilemapLeft = 6,
        .tilemapTop = 15,
        .width = 14,
        .height = 4,
        .paletteNum = 12,
        .baseBlock = 0x0137
    }, {
        .bg = 0,
        .tilemapLeft = 6,
        .tilemapTop = 15,
        .width = 16,
        .height = 4,
        .paletteNum = 12,
        .baseBlock = 0x0137
    }, {
        .bg = 0,
        .tilemapLeft = 6,
        .tilemapTop = 15,
        .width = 23,
        .height = 4,
        .paletteNum = 12,
        .baseBlock = 0x009b
    }
};

void ItemPc_Init(u8 kind, MainCallback callback)
{
    u8 i;

    if (kind >= 3)
    {
        SetMainCallback2(callback);
        return;
    }
    if ((sItemPcState = AllocZeroed(sizeof(struct ItemPcResources))) == NULL)
    {
        SetMainCallback2(kind == 1 ? sItemPcPosition.savedCallback : callback);
        return;
    }
    if (kind != 1)
    {
        sItemPcPosition.savedCallback = callback;
        sItemPcPosition.scroll = sItemPcPosition.row = 0;
        sItemPcPosition.deposit = kind == 2;
        sItemPcPosition.pocket = POCKET_ITEMS;
        memset(sItemPcPosition.pocketScroll, 0, sizeof(sItemPcPosition.pocketScroll));
        memset(sItemPcPosition.pocketRow, 0, sizeof(sItemPcPosition.pocketRow));
    }
    sItemPcState->moveModeOrigPos = 0xFF;
    sItemPcState->itemMenuIconSlot = 0;
    sItemPcState->scrollIndicatorArrowPairId = 0xFF;
    sItemPcState->pocketArrowsId = 0xFF;
    sItemPcState->savedCallback = 0;
    for (i = 0; i < 3; i++)
    {
        sItemPcState->data[i] = 0;
    }
    SetMainCallback2(ItemPc_RunSetup);
}

static void ItemPc_MainCB(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void ItemPc_VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void ItemPc_RunSetup(void)
{
    while (1)
    {
        if (ItemPc_DoGfxSetup() == TRUE)
            break;
        if (MenuHelpers_IsLinkActive() == TRUE)
            break;
    }
}

static bool8 ItemPc_DoGfxSetup(void)
{
    u8 taskId;
    switch (gMain.state)
    {
    case 0:
        SetVBlankHBlankCallbacksToNull();
        ClearScheduledBgCopiesToVram();
        gMain.state++;
        break;
    case 1:
        ScanlineEffect_Stop();
        gMain.state++;
        break;
    case 2:
        FreeAllSpritePalettes();
        gMain.state++;
        break;
    case 3:
        ResetPaletteFade();
        gMain.state++;
        break;
    case 4:
        ResetSpriteData();
        gMain.state++;
        break;
    case 5:
        FrlgPcResetIcons();
        gMain.state++;
        break;
    case 6:
        ResetTasks();
        gMain.state++;
        break;
    case 7:
        if (ItemPc_InitBgs())
        {
            sItemPcState->data[0] = 0;
            gMain.state++;
        }
        else
        {
            ItemPc_FadeAndBail();
            return TRUE;
        }
        break;
    case 8:
        if (ItemPc_LoadGraphics() == TRUE)
            gMain.state++;
        break;
    case 9:
        ItemPc_InitWindows();
        gMain.state++;
        break;
    case 10:
        ItemPc_CountPcItems();
        ItemPc_SetCursorPosition();
        ItemPc_SetScrollPosition();
        gMain.state++;
        break;
    case 11:
        if (ItemPc_AllocateResourcesForListMenu())
            gMain.state++;
        else
        {
            ItemPc_FadeAndBail();
            return TRUE;
        }
        break;
    case 12:
        ItemPc_BuildListMenuTemplate();
        gMain.state++;
        break;
    case 13:
        ItemPc_PrintWithdrawItem();
        gMain.state++;
        break;
    case 14:
        if (sItemPcPosition.deposit)
            FrlgPcCreateBag(0);
        else
            FrlgPcCreateSwapLine();
        gMain.state++;
        break;
    case 15:
        taskId = CreateTask(Task_ItemPcMain, 0);
        gTasks[taskId].data[0] = ListMenuInit(&gMultiuseListMenuTemplate, sItemPcPosition.scroll, sItemPcPosition.row);
        gMain.state++;
        break;
    case 16:
        ItemPc_PlaceTopMenuScrollIndicatorArrows();
        gMain.state++;
        break;
    case 17:

        gMain.state++;
        break;
    case 18:
        if (sItemPcPosition.initialized == 1)
        {
            BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
        }
        gMain.state++;
        break;
    case 19:
        if (sItemPcPosition.deposit)
        {
            BeginFrlgBagCurtain(TRUE);
            BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        }
        else if (sItemPcPosition.initialized == 1)
        {
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        }
        else
        {
            BeginPCScreenEffect_TurnOn(0, 0, 0);
            ItemPc_SetInitializedFlag(TRUE);
            PlaySE(SE_PC_LOGIN);
        }
        gMain.state++;
        break;
    case 20:
        if (MenuHelpers_ShouldWaitForLinkRecv() != TRUE)
            gMain.state++;
        break;
    default:
        SetVBlankCallback(ItemPc_VBlankCB);
        SetMainCallback2(ItemPc_MainCB);
        return TRUE;
    }
    return FALSE;
}

static void ItemPc_FadeAndBail(void)
{
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    CreateTask(Task_ItemPcWaitFadeAndBail, 0);
    SetVBlankCallback(ItemPc_VBlankCB);
    SetMainCallback2(ItemPc_MainCB);
}

static void Task_ItemPcWaitFadeAndBail(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetMainCallback2(sItemPcPosition.savedCallback);
        ItemPc_FreeResources();
        DestroyTask(taskId);
    }
}

static bool8 ItemPc_InitBgs(void)
{
    ResetVramOamAndBgCntRegs();
    sItemPcBgTilemap = Alloc(0x800);
    if (sItemPcBgTilemap == NULL)
        return FALSE;
    memset(sItemPcBgTilemap, 0, 0x800);
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    ResetAllBgsCoordinates();
    SetBgTilemapBuffer(1, sItemPcBgTilemap);
    ScheduleBgCopyTilemapToVram(1);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
    SetGpuReg(REG_OFFSET_BLDCNT , 0);
    ShowBg(0);
    ShowBg(1);
    return TRUE;
}

static bool8 ItemPc_LoadGraphics(void)
{
    switch (sItemPcState->data[0])
    {
    case 0:
        ResetTempTileDataBuffers();
        DecompressAndCopyTileDataToVram(1, sItemPcPosition.deposit ? sDepositTiles : sItemPcTiles, 0, 0, 0);
        sItemPcState->data[0]++;
        break;
    case 1:
        if (FreeTempTileDataBuffersIfPossible() != TRUE)
        {
            LZ77UnCompWram(sItemPcPosition.deposit ? sDepositTilemap : sItemPcTilemap, sItemPcBgTilemap);
            sItemPcState->data[0]++;
        }
        break;
    case 2:
        LoadPalette(sItemPcPosition.deposit ? sDepositPalette : sItemPcPalette, BG_PLTT_ID(0), 3 * 32);
        if (sItemPcPosition.deposit && gSaveBlock2Ptr->playerGender == FEMALE)
            LoadPalette(sDepositFemalePalette, BG_PLTT_ID(0), 32);
        sItemPcState->data[0]++;
        break;
    case 3:
        LoadCompressedSpriteSheet(&gFrlgPcSwapSheet);
        sItemPcState->data[0]++;
        break;
    default:
        LoadSpritePalette(&gFrlgPcSwapPalette);
        if (sItemPcPosition.deposit)
        {
            LoadCompressedSpriteSheet(gSaveBlock2Ptr->playerGender == FEMALE ? &gFrlgPcBagFemaleSheet : &gFrlgPcBagMaleSheet);
            LoadSpritePalette(&gFrlgPcBagPalette);
        }
        sItemPcState->data[0] = 0;
        return TRUE;
    }
    return FALSE;
}

#define try_alloc(ptr__, size) ({ \
    void ** ptr = (void **)&(ptr__);             \
    *ptr = Alloc(size);                 \
    if (*ptr == NULL)                   \
    {                                   \
        return FALSE;                   \
    }                                   \
})

static bool8 ItemPc_AllocateResourcesForListMenu(void)
{
    try_alloc(sItemPcList, sizeof(struct ListMenuItem) * (max(PC_ITEMS_COUNT, BAG_TMHM_COUNT) + 1));
    return TRUE;
}

static void ItemPc_BuildListMenuTemplate(void)
{
    u16 i;

    for (i = 0; i < sItemPcState->nItems; i++)
    {
        sItemPcList[i].name = GetItemName(ItemPc_GetItemIdBySlotId(i));
        sItemPcList[i].id = i;
    }
    sItemPcList[i].name = gText_Cancel;
    sItemPcList[i].id = -2;

    gMultiuseListMenuTemplate.items = sItemPcList;
    gMultiuseListMenuTemplate.totalItems = sItemPcState->nItems + 1;
    gMultiuseListMenuTemplate.windowId = 0;
    gMultiuseListMenuTemplate.header_X = 0;
    gMultiuseListMenuTemplate.item_X = 9;
    gMultiuseListMenuTemplate.cursor_X = 1;
    gMultiuseListMenuTemplate.lettersSpacing = 1;
    gMultiuseListMenuTemplate.itemVerticalPadding = 2;
    gMultiuseListMenuTemplate.upText_Y = 2;
    gMultiuseListMenuTemplate.maxShowed = sItemPcState->maxShowed;
    gMultiuseListMenuTemplate.fontId = FONT_NORMAL;
    gMultiuseListMenuTemplate.cursorPal = 2;
    gMultiuseListMenuTemplate.fillValue = 0;
    gMultiuseListMenuTemplate.cursorShadowPal = 3;
    gMultiuseListMenuTemplate.moveCursorFunc = ItemPc_MoveCursorFunc;
    gMultiuseListMenuTemplate.itemPrintFunc = ItemPc_ItemPrintFunc;
    gMultiuseListMenuTemplate.scrollMultiple = 0;
    gMultiuseListMenuTemplate.cursorKind = 0;
}

static void ItemPc_MoveCursorFunc(s32 itemIndex, bool8 onInit, struct ListMenu * list)
{
    u16 itemId;
    const u8 * desc;

    if (!onInit && sItemPcPosition.deposit)
        FrlgPcShakeBag();
    if (onInit != TRUE)
        PlaySE(SE_SELECT);

    if (sItemPcState->moveModeOrigPos == 0xFF)
    {
        FrlgPcDestroyItemIcon(sItemPcState->itemMenuIconSlot ^ 1);
        if (itemIndex != -2)
        {
            itemId = ItemPc_GetItemIdBySlotId(itemIndex);
            FrlgPcCreateItemIcon(itemId, sItemPcState->itemMenuIconSlot);
            desc = GetItemDescription(itemId);
        }
        else
        {
            FrlgPcCreateItemIcon(ITEM_LIST_END, sItemPcState->itemMenuIconSlot);
            desc = sText_ReturnToPC;
        }
        sItemPcState->itemMenuIconSlot ^= 1;
        FillWindowPixelBuffer(1, 0);
        ItemPc_AddTextPrinterParameterized(1, FONT_NORMAL, desc, 0, 3, 2, 0, 0, 3);
    }
}

static void ItemPc_ItemPrintFunc(u8 windowId, u32 itemId, u8 y)
{
    if (sItemPcState->moveModeOrigPos != 0xFF)
    {
        if (sItemPcState->moveModeOrigPos == (u8)itemId)
            ItemPc_PrintOrRemoveCursorAt(y, 2);
        else
            ItemPc_PrintOrRemoveCursorAt(y, 0xFF);
    }
    if (itemId != -2)
    {
        u16 quantity = ItemPc_GetItemQuantityBySlotId(itemId);
        ConvertIntToDecimalStringN(gStringVar1, quantity, STR_CONV_MODE_RIGHT_ALIGN, 3);
        StringExpandPlaceholders(gStringVar4, sText_Quantity);
        ItemPc_AddTextPrinterParameterized(windowId, FONT_SMALL, gStringVar4, 110, y, 0, 0, 0xFF, 1);
    }
}

static void ItemPc_PrintOrRemoveCursor(u8 listMenuId, u8 colorIdx)
{
    ItemPc_PrintOrRemoveCursorAt(ListMenuGetYCoordForPrintingArrowCursor(listMenuId), colorIdx);
}

static void ItemPc_PrintOrRemoveCursorAt(u8 y, u8 colorIdx)
{
    if (colorIdx == 0xFF)
    {
        u8 maxWidth = GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_WIDTH);
        u8 maxHeight = GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_HEIGHT);
        FillWindowPixelRect(0, 0, 0, y, maxWidth, maxHeight);
    }
    else
    {
        ItemPc_AddTextPrinterParameterized(0, FONT_NORMAL, gText_SelectorArrow2, 0, y, 0, 0, 0, colorIdx);
    }
}

static void ItemPc_PrintWithdrawItem(void)
{
    if (sItemPcPosition.deposit)
        DrawStdFrameWithCustomTileAndPalette(2, FALSE, 0x3A3, 12);
    ItemPc_AddTextPrinterParameterized(2, FONT_SMALL,
        sItemPcPosition.deposit ? sText_DepositTitle : sText_WithdrawTitle,
        0, 1, 0, 1, 0, sItemPcPosition.deposit ? 1 : 0);
}

static void ItemPc_PlaceTopMenuScrollIndicatorArrows(void)
{
    static const struct ScrollArrowsTemplate pocketArrows = {
        .firstArrowType = SCROLL_ARROW_LEFT, .firstX = 8, .firstY = 72,
        .secondArrowType = SCROLL_ARROW_RIGHT, .secondX = 72, .secondY = 72,
        .fullyUpThreshold = 0, .fullyDownThreshold = POCKETS_COUNT - 1,
        .tileTag = 111, .palTag = 111,
    };

    sItemPcState->scrollIndicatorArrowPairId = AddScrollIndicatorArrowPairParameterized(2, sItemPcPosition.deposit ? 160 : 128, 8, 104, sItemPcState->nItems - sItemPcState->maxShowed + 1, 110, 110, &sItemPcPosition.scroll);
    if (sItemPcPosition.deposit && sItemPcState->pocketArrowsId == 0xFF)
    {
        sItemPcState->pocketSwitchPosition = sItemPcPosition.pocket;
        sItemPcState->pocketArrowsId = AddScrollIndicatorArrowPair(&pocketArrows, &sItemPcState->pocketSwitchPosition);
    }
}

static void ItemPc_PlaceWithdrawQuantityScrollIndicatorArrows(void)
{
    sItemPcState->withdrawQuantitySubmenuCursorPos = 1;
    sItemPcState->scrollIndicatorArrowPairId = AddScrollIndicatorArrowPairParameterized(2, 212, 120, 152, 2, 110, 110, &sItemPcState->withdrawQuantitySubmenuCursorPos);
}

static void ItemPc_RemoveScrollIndicatorArrowPair(void)
{
    if (sItemPcState->scrollIndicatorArrowPairId != 0xFF)
    {
        RemoveScrollIndicatorArrowPair(sItemPcState->scrollIndicatorArrowPairId);
        sItemPcState->scrollIndicatorArrowPairId = 0xFF;
    }
    if (sItemPcState->pocketArrowsId != 0xFF)
    {
        RemoveScrollIndicatorArrowPair(sItemPcState->pocketArrowsId);
        sItemPcState->pocketArrowsId = 0xFF;
    }
}

static void ItemPc_SetCursorPosition(void)
{
    SetCursorWithinListBounds(&sItemPcPosition.scroll, &sItemPcPosition.row,
        sItemPcState->maxShowed, sItemPcState->nItems + 1);
}

#define try_free(ptr) ({        \
    void ** ptr__ = (void **)&(ptr);   \
    if (*ptr__ != NULL)                \
    { Free(*ptr__); *ptr__ = NULL; }    \
})

static void ItemPc_FreeResources(void)
{
    try_free(sItemPcState);
    try_free(sItemPcBgTilemap);
    try_free(sItemPcList);

    FreeAllWindowBuffers();
}

static void Task_ItemPcTurnOff1(u8 taskId)
{
    if (sItemPcPosition.deposit)
    {
        BeginFrlgBagCurtain(FALSE);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    }
    else if (sItemPcPosition.initialized == 1)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    }
    else
    {
        BeginPCScreenEffect_TurnOff(0, 0, 0);
        PlaySE(SE_PC_OFF);
    }
    gTasks[taskId].func = Task_ItemPcTurnOff2;
}

static void Task_ItemPcTurnOff2(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    if (!gPaletteFade.active && !IsPCScreenEffectRunning_TurnOff() && !FuncIsActiveTask(Task_FrlgBagCurtain))
    {
        DestroyListMenuTask(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
        if (sItemPcState->savedCallback != NULL)
            SetMainCallback2(sItemPcState->savedCallback);
        else
            SetMainCallback2(sItemPcPosition.savedCallback);
        ItemPc_RemoveScrollIndicatorArrowPair();
        ItemPc_FreeResources();
        DestroyTask(taskId);
    }
}

static u8 ItemPc_GetCursorPosition(void)
{
    return sItemPcPosition.scroll + sItemPcPosition.row;
}

static u16 ItemPc_GetItemIdBySlotId(u16 idx)
{
    if (sItemPcPosition.deposit)
        return GetBagItemId(sItemPcPosition.pocket, idx);
    return gSaveBlock1Ptr->pcItems[idx].itemId;
}

static u16 ItemPc_GetItemQuantityBySlotId(u16 idx)
{
    if (sItemPcPosition.deposit)
        return GetBagItemQuantity(sItemPcPosition.pocket, idx);
    return FrlgPcGetQuantity(&gSaveBlock1Ptr->pcItems[idx].quantity);
}

void FrlgPcOpenDeposit(MainCallback callback)
{
    SetBagItemsPointers();
    ItemPc_Init(2, callback);
}

void FrlgPcTakeGivenItem(u16 item)
{
    u8 slot = ItemPc_GetCursorPosition();
    if (item == ITEM_NONE)
        return;
    if (slot < PC_ITEMS_COUNT && gSaveBlock1Ptr->pcItems[slot].itemId == item)
    {
        RemovePCItem(slot, 1);
        return;
    }
    // Mail cancellation can return a different item from the selected stack.
    for (slot = 0; slot < PC_ITEMS_COUNT; slot++)
        if (gSaveBlock1Ptr->pcItems[slot].itemId == item)
        {
            RemovePCItem(slot, 1);
            return;
        }
}

static void ItemPc_CountPcItems(void)
{
    u16 i;

    if (sItemPcPosition.deposit)
        CompactItemsInBagPocket(sItemPcPosition.pocket);
    else
        CompactPCItems();
    sItemPcState->nItems = 0;
    for (i = 0; i < (sItemPcPosition.deposit ? gBagPockets[sItemPcPosition.pocket].capacity : PC_ITEMS_COUNT); sItemPcState->nItems++, i++)
    {
        if (ItemPc_GetItemIdBySlotId(i) == ITEM_NONE)
            break;
    }
    sItemPcState->maxShowed = sItemPcState->nItems + 1 <= 6 ? sItemPcState->nItems + 1 : 6;
}

static void ItemPc_SetScrollPosition(void)
{
    u8 i;

    if (sItemPcPosition.row > 3)
    {
        for (i = 0; i <= sItemPcPosition.row - 3; sItemPcPosition.row--, sItemPcPosition.scroll++, i++)
        {
            if (sItemPcPosition.scroll + sItemPcState->maxShowed == sItemPcState->nItems + 1)
                break;
        }
    }
}

static void ItemPc_SetMessageWindowPalette(int palIdx)
{
    SetBgTilemapPalette(1, 0, 14, 30, 6, palIdx + 1);
    ScheduleBgCopyTilemapToVram(1);
}

void ItemPc_SetInitializedFlag(bool8 flag)
{
    sItemPcPosition.initialized = flag;
}

static void Task_ItemPcMain(u8 taskId)
{
    s16 * data = gTasks[taskId].data;
    u16 scroll;
    u16 row;
    s32 input;

    if (!gPaletteFade.active && !IsPCScreenEffectRunning_TurnOn() && !FuncIsActiveTask(Task_FrlgBagCurtain))
    {
        if (sItemPcPosition.deposit)
        {
            u8 lr = GetLRKeysPressed();
            if ((JOY_NEW(DPAD_LEFT) || lr == MENU_L_PRESSED) && sItemPcPosition.pocket > 0)
            { ItemPc_StartPocketSwitch(taskId, -1); return; }
            if ((JOY_NEW(DPAD_RIGHT) || lr == MENU_R_PRESSED) && sItemPcPosition.pocket + 1 < POCKETS_COUNT)
            { ItemPc_StartPocketSwitch(taskId, 1); return; }
        }
        if (!sItemPcPosition.deposit && JOY_NEW(SELECT_BUTTON))
        {
            ListMenuGetScrollAndRow(data[0], &scroll, &row);
            if (scroll + row != sItemPcState->nItems)
            {
                PlaySE(SE_SELECT);
                ItemPc_MoveItemModeInit(taskId, scroll + row);
                return;
            }
        }
        input = ListMenu_ProcessInput(data[0]);
        ListMenuGetScrollAndRow(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
        switch (input)
        {
        case -1:
            break;
        case -2:
            PlaySE(SE_SELECT);
            ItemPc_SetInitializedFlag(FALSE);
            gTasks[taskId].func = Task_ItemPcTurnOff1;
            break;
        default:
            PlaySE(SE_SELECT);
            ItemPc_SetMessageWindowPalette(1);
            ItemPc_RemoveScrollIndicatorArrowPair();
            data[1] = input;
            data[2] = ItemPc_GetItemQuantityBySlotId(input);
            ItemPc_PrintOrRemoveCursor(data[0], 2);
            gTasks[taskId].func = sItemPcPosition.deposit ? Task_ItemPcWithdraw : Task_ItemPcSubmenuInit;
            break;
        }
    }
}

static void ItemPc_ReturnFromSubmenu(u8 taskId)
{
    ItemPc_SetMessageWindowPalette(0);
    ItemPc_PlaceTopMenuScrollIndicatorArrows();
    gTasks[taskId].func = Task_ItemPcMain;
}

static void ItemPc_MoveItemModeInit(u8 taskId, s16 pos)
{
    s16 * data = gTasks[taskId].data;

    ListMenuSetTemplateField(data[0], 16, 1);
    data[1] = pos;
    sItemPcState->moveModeOrigPos = pos;
    StringCopy(gStringVar1, GetItemName(ItemPc_GetItemIdBySlotId(data[1])));
    StringExpandPlaceholders(gStringVar4, sText_WherePlaceItem);
    FillWindowPixelBuffer(1, 0x00);
    ItemPc_AddTextPrinterParameterized(1, FONT_NORMAL, gStringVar4, 0, 3, 2, 3, 0, 0);
    FrlgPcUpdateSwapLinePos(-32, ListMenuGetYCoordForPrintingArrowCursor(data[0]));
    FrlgPcSetSwapLineInvisibility(FALSE);
    ItemPc_PrintOrRemoveCursor(data[0], 2);
    gTasks[taskId].func = Task_ItemPcMoveItemModeRun;
}

static void Task_ItemPcMoveItemModeRun(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    ListMenu_ProcessInput(data[0]);
    ListMenuGetScrollAndRow(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
    FrlgPcUpdateSwapLinePos(-32, ListMenuGetYCoordForPrintingArrowCursor(data[0]));
    if (JOY_NEW(A_BUTTON | SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        sItemPcState->moveModeOrigPos = 0xFF;
        ItemPc_InsertItemIntoNewSlot(taskId, sItemPcPosition.scroll + sItemPcPosition.row);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        sItemPcState->moveModeOrigPos = 0xFF;
        ItemPc_MoveItemModeCancel(taskId, sItemPcPosition.scroll + sItemPcPosition.row);
    }
}

static void ItemPc_InsertItemIntoNewSlot(u8 taskId, u32 pos)
{
    s16 * data = gTasks[taskId].data;
    if (data[1] == pos || data[1] == pos - 1)
        ItemPc_MoveItemModeCancel(taskId, pos);
    else
    {
        MoveItemSlotInPC(gSaveBlock1Ptr->pcItems, data[1], pos);
        DestroyListMenuTask(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
        if (data[1] < pos)
        {
            if (sItemPcPosition.row != 0)
                sItemPcPosition.row--;
            else if (sItemPcPosition.scroll != 0)
                sItemPcPosition.scroll--;
        }
        ItemPc_BuildListMenuTemplate();
        data[0] = ListMenuInit(&gMultiuseListMenuTemplate, sItemPcPosition.scroll, sItemPcPosition.row);
        FrlgPcSetSwapLineInvisibility(TRUE);
        gTasks[taskId].func = Task_ItemPcMain;
    }
}

static void ItemPc_MoveItemModeCancel(u8 taskId, u32 pos)
{
    s16 * data = gTasks[taskId].data;

    DestroyListMenuTask(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
    if (data[1] < pos)
    {
        if (sItemPcPosition.row != 0)
            sItemPcPosition.row--;
        else if (sItemPcPosition.scroll != 0)
            sItemPcPosition.scroll--;
    }
    ItemPc_BuildListMenuTemplate();
    data[0] = ListMenuInit(&gMultiuseListMenuTemplate, sItemPcPosition.scroll, sItemPcPosition.row);
    FrlgPcSetSwapLineInvisibility(TRUE);
    gTasks[taskId].func = Task_ItemPcMain;
}

static void Task_ItemPcSubmenuInit(u8 taskId)
{
    s16 * data = gTasks[taskId].data;
    u8 windowId;

    ItemPc_SetBorderStyleOnWindow(4);
    windowId = ItemPc_GetOrCreateSubwindow(0);
    PrintMenuActionTextsAtPos(4, FONT_NORMAL, 8, 2, GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_HEIGHT) + 2, 3, sItemPcSubmenuOptions);
    InitMenuNormal(4, FONT_NORMAL, 0, 2, GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_HEIGHT) + 2, 3, 0);
    CopyItemName(ItemPc_GetItemIdBySlotId(data[1]), gStringVar1);
    StringExpandPlaceholders(gStringVar4, sText_ItemSelected);
    ItemPc_AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4, 0, 2, 1, 0, 0, 1);
    ScheduleBgCopyTilemapToVram(0);
    gTasks[taskId].func = Task_ItemPcSubmenuRun;
}

static void Task_ItemPcSubmenuRun(u8 taskId)
{
    s8 input = Menu_ProcessInputNoWrap();
    switch (input)
    {
    case -1:
        PlaySE(SE_SELECT);
        Task_ItemPcCancel(taskId);
        break;
    case -2:
        break;
    default:
        PlaySE(SE_SELECT);
        sItemPcSubmenuOptions[input].func.void_u8(taskId);
    }
}

static void Task_ItemPcWithdraw(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    if (!sItemPcPosition.deposit)
    {
        ClearStdWindowAndFrameToTransparent(4, FALSE);
        ItemPc_DestroySubwindow(0);
        ClearWindowTilemap(4);
    }
    else if (GetItemImportance(ItemPc_GetItemIdBySlotId(data[1])))
    {
        AddTextPrinterParameterized(ItemPc_GetOrCreateSubwindow(2), FONT_NORMAL, sText_ImportantItem, 0, 2, 0, NULL);
        gTasks[taskId].func = Task_ItemPcWaitButtonWithdrawMultipleFailed;
        return;
    }
    data[8] = 1;
    if (ItemPc_GetItemQuantityBySlotId(data[1]) == 1)
    {
        PutWindowTilemap(0);
        ScheduleBgCopyTilemapToVram(0);
        ItemPc_DoWithdraw(taskId);
    }
    else
    {
        PutWindowTilemap(0);
        ItemPc_WithdrawMultipleInitWindow(data[1]);
        ItemPc_PlaceWithdrawQuantityScrollIndicatorArrows();
        gTasks[taskId].func = Task_ItemPcHandleWithdrawMultiple;
    }
}

static void ItemPc_DoWithdraw(u8 taskId)
{
    s16 * data = gTasks[taskId].data;
    u16 itemId = ItemPc_GetItemIdBySlotId(data[1]);
    u8 windowId;

    if (sItemPcPosition.deposit ? AddPCItem(itemId, data[8]) : AddBagItem(itemId, data[8]))
    {

        CopyItemName(itemId, gStringVar1);
        ConvertIntToDecimalStringN(gStringVar2, data[8], STR_CONV_MODE_LEFT_ALIGN, 3);
        StringExpandPlaceholders(gStringVar4, sItemPcPosition.deposit ? sText_DepositedItem : sText_WithdrewItem);
        windowId = ItemPc_GetOrCreateSubwindow(2);
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4, 0, 2, 0, NULL);
        gTasks[taskId].func = Task_ItemPcWaitButtonAndFinishWithdrawMultiple;
    }
    else
    {
        windowId = ItemPc_GetOrCreateSubwindow(2);
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sItemPcPosition.deposit ? sText_PcFull : sText_BagFull, 0, 2, 0, NULL);
        gTasks[taskId].func = Task_ItemPcWaitButtonWithdrawMultipleFailed;
    }
}

static void Task_ItemPcWaitButtonAndFinishWithdrawMultiple(u8 taskId)
{
    s16 * data = gTasks[taskId].data;
    u16 itemId;

    if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        itemId = ItemPc_GetItemIdBySlotId(data[1]);
        if (sItemPcPosition.deposit)
            RemoveBagItem(itemId, data[8]);
        else
            RemovePCItem(data[1], data[8]);
        Task_ItemPcCleanUpWithdraw(taskId);
    }
}

static void Task_ItemPcWaitButtonWithdrawMultipleFailed(u8 taskId)
{
    if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        Task_ItemPcCleanUpWithdraw(taskId);
    }
}

static void Task_ItemPcCleanUpWithdraw(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    ItemPc_DestroySubwindow(2);
    PutWindowTilemap(1);
    DestroyListMenuTask(data[0], &sItemPcPosition.scroll, &sItemPcPosition.row);
    ItemPc_CountPcItems();
    ItemPc_SetCursorPosition();
    ItemPc_BuildListMenuTemplate();
    data[0] = ListMenuInit(&gMultiuseListMenuTemplate, sItemPcPosition.scroll, sItemPcPosition.row);
    ScheduleBgCopyTilemapToVram(0);
    ItemPc_ReturnFromSubmenu(taskId);
}

static void ItemPc_WithdrawMultipleInitWindow(u16 slotId)
{
    u16 itemId = ItemPc_GetItemIdBySlotId(slotId);

    CopyItemName(itemId, gStringVar1);
    StringExpandPlaceholders(gStringVar4, sItemPcPosition.deposit ? sText_DepositHowMany : sText_WithdrawHowMany);
    AddTextPrinterParameterized(ItemPc_GetOrCreateSubwindow(1), FONT_NORMAL, gStringVar4, 0, 2, 0, NULL);
    ConvertIntToDecimalStringN(gStringVar1, 1, STR_CONV_MODE_LEADING_ZEROS, 3);
    StringExpandPlaceholders(gStringVar4, sText_Quantity);
    ItemPc_SetBorderStyleOnWindow(3);
    ItemPc_AddTextPrinterParameterized(3, FONT_SMALL, gStringVar4, 8, 10, 1, 0, 0, 1);
    ScheduleBgCopyTilemapToVram(0);
}

static void UpdateWithdrawQuantityDisplay(s16 quantity)
{
    FillWindowPixelRect(3, PIXEL_FILL(1), 10, 10, 28, 12);
    ConvertIntToDecimalStringN(gStringVar1, quantity, STR_CONV_MODE_LEADING_ZEROS, 3);
    StringExpandPlaceholders(gStringVar4, sText_Quantity);
    ItemPc_AddTextPrinterParameterized(3, FONT_SMALL, gStringVar4, 8, 10, 1, 0, 0, 1);
}

static void Task_ItemPcHandleWithdrawMultiple(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    if (AdjustQuantityAccordingToDPadInput(&data[8], data[2]) == TRUE)
        UpdateWithdrawQuantityDisplay(data[8]);
    else if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        ItemPc_DestroySubwindow(1);
        ClearWindowTilemap(3);
        PutWindowTilemap(0);
        ItemPc_PrintOrRemoveCursor(data[0], 1);
        ScheduleBgCopyTilemapToVram(0);
        ItemPc_RemoveScrollIndicatorArrowPair();
        ItemPc_DoWithdraw(taskId);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        ClearStdWindowAndFrameToTransparent(3, FALSE);
        ItemPc_DestroySubwindow(1);
        ClearWindowTilemap(3);
        PutWindowTilemap(0);
        PutWindowTilemap(1);
        ItemPc_PrintOrRemoveCursor(data[0], 1);
        ScheduleBgCopyTilemapToVram(0);
        ItemPc_RemoveScrollIndicatorArrowPair();
        ItemPc_ReturnFromSubmenu(taskId);
    }
}

static void Task_ItemPcGive(u8 taskId)
{
    if (!IsHoldingItemAllowed(ItemPc_GetItemIdBySlotId(ItemPc_GetCursorPosition())))
    {
        ClearStdWindowAndFrameToTransparent(4, FALSE);
        ItemPc_DestroySubwindow(0);
        ClearWindowTilemap(4);
        PutWindowTilemap(0);
        ItemPc_PrintOnWindow5WithContinueTask(taskId, sText_CannotHold, gTask_ItemPcWaitButtonAndExitSubmenu);
    }
    else if (CalculatePlayerPartyCount() == 0)
    {
        ClearStdWindowAndFrameToTransparent(4, FALSE);
        ItemPc_DestroySubwindow(0);
        ClearWindowTilemap(4);
        PutWindowTilemap(0);
        ItemPc_PrintOnWindow5WithContinueTask(taskId, sText_NoPokemon, gTask_ItemPcWaitButtonAndExitSubmenu);
    }
    else
    {
        sItemPcState->savedCallback = ItemPc_CB2_SwitchToPartyMenu;
        Task_ItemPcTurnOff1(taskId);
    }
}

static void ItemPc_CB2_SwitchToPartyMenu(void)
{
    ChooseMonToGiveItemFromPC(ItemPc_GetItemIdBySlotId(ItemPc_GetCursorPosition()), ItemPc_CB2_ReturnFromPartyMenu);
}

static void ItemPc_CB2_ReturnFromPartyMenu(void)
{
    ItemPc_Init(1, NULL);
}

static void gTask_ItemPcWaitButtonAndExitSubmenu(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        ClearDialogWindowAndFrameToTransparent(5, FALSE);
        ClearWindowTilemap(5);
        PutWindowTilemap(1);
        ItemPc_PrintOrRemoveCursor(data[0], 1);
        ScheduleBgCopyTilemapToVram(0);
        ItemPc_ReturnFromSubmenu(taskId);
    }
}

static void Task_ItemPcCancel(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    ClearStdWindowAndFrameToTransparent(4, FALSE);
    ItemPc_DestroySubwindow(0);
    ClearWindowTilemap(4);
    PutWindowTilemap(0);
    PutWindowTilemap(1);
    ItemPc_PrintOrRemoveCursor(data[0], 1);
    ScheduleBgCopyTilemapToVram(0);
    ItemPc_ReturnFromSubmenu(taskId);
}

static void ItemPc_InitWindows(void)
{
    u8 i;
    struct WindowTemplate templates[ARRAY_COUNT(sWindowTemplates)];
    memcpy(templates, sWindowTemplates, sizeof(templates));
    if (sItemPcPosition.deposit)
    {
        templates[0].tilemapLeft = 11;
        templates[0].width = 18;
        templates[2].width = 8;
        templates[2].height = 2;
    }
    InitWindows(templates);
    DeactivateAllTextPrinters();
    LoadUserWindowBorderGfx(0, 0x3C0, BG_PLTT_ID(14));
    LoadStdWindowGfx(0, 0x3A3, BG_PLTT_ID(12));
    LoadMessageBoxGfx(0, 0x3AC, BG_PLTT_ID(11));
    LoadPalette(gFrlgPcDescriptionPalette, BG_PLTT_ID(13), 32);
    LoadPalette(gFrlgPcMenuPalette, BG_PLTT_ID(15), 32);
    for (i = 0; i < 3; i++)
    {
        FillWindowPixelBuffer(i, 0x00);
        PutWindowTilemap(i);
    }
    ScheduleBgCopyTilemapToVram(0);
    for (i = 0; i < 3; i++)
        sItemPcSubwindows[i] = 0xFF;
}

static void ItemPc_AddTextPrinterParameterized(u8 windowId, u8 fontId, const u8 * str, u8 x, u8 y, u8 letterSpacing, u8 lineSpacing, u8 speed, u8 colorIdx)
{
    AddTextPrinterParameterized4(windowId, fontId, x, y, letterSpacing, lineSpacing, sTextColors[colorIdx], speed, str);
}

static void ItemPc_SetBorderStyleOnWindow(u8 windowId)
{
    DrawStdFrameWithCustomTileAndPalette(windowId, FALSE, 0x3C0, 14);
}

static u8 ItemPc_GetOrCreateSubwindow(u8 idx)
{
    if (sItemPcSubwindows[idx] == 0xFF)
    {
        sItemPcSubwindows[idx] = AddWindow(&sSubwindowTemplates[idx]);
        DrawStdFrameWithCustomTileAndPalette(sItemPcSubwindows[idx], TRUE, 0x3A3, 12);
    }

    return sItemPcSubwindows[idx];
}

static void ItemPc_DestroySubwindow(u8 idx)
{
    ClearStdWindowAndFrameToTransparent(sItemPcSubwindows[idx], FALSE);
    ClearWindowTilemap(sItemPcSubwindows[idx]); // redundant
    RemoveWindow(sItemPcSubwindows[idx]);
    sItemPcSubwindows[idx] = 0xFF;
}

static u8 ItemPc_GetSubwindow(u8 idx)
{
    return sItemPcSubwindows[idx];
}

static void ItemPc_PrintOnWindow5WithContinueTask(u8 taskId, const u8 * str, TaskFunc taskFunc)
{
    DisplayMessageAndContinueTask(taskId, 5, 0x3AC, 0x0B, FONT_NORMAL, GetPlayerTextSpeedDelay(), str, taskFunc);
    ScheduleBgCopyTilemapToVram(0);
}

// FRLG's Bag opens and closes with a vertical window reveal.
static void BeginFrlgBagCurtain(bool8 opening)
{
    u8 taskId = CreateTask(Task_FrlgBagCurtain, 0);
    SetGpuRegBits(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON);
    SetGpuReg(REG_OFFSET_WININ, 0);
    SetGpuReg(REG_OFFSET_WINOUT, WININ_WIN0_ALL);
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(0, DISPLAY_WIDTH));
    SetGpuReg(REG_OFFSET_WIN0V, opening ? WIN_RANGE(0, DISPLAY_HEIGHT) : 0);
    gTasks[taskId].data[0] = opening ? 192 : -16;
    gTasks[taskId].data[1] = opening ? -16 : 16;
}

static void Task_FrlgBagCurtain(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    data[0] += data[1];
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(0, min(DISPLAY_HEIGHT, data[0])));
    if ((data[1] > 0 && data[0] == DISPLAY_HEIGHT) || (data[1] < 0 && data[0] == 0))
        DestroyTask(taskId);
}

static void ItemPc_StartPocketSwitch(u8 taskId, s8 direction)
{
    u8 pocket = sItemPcPosition.pocket;
    s16 *data = gTasks[taskId].data;

    PlaySE(SE_RG_BAG_POCKET);
    DestroyListMenuTask(data[0], &sItemPcPosition.pocketScroll[pocket], &sItemPcPosition.pocketRow[pocket]);
    ItemPc_RemoveScrollIndicatorArrowPair();
    FrlgPcDestroyItemIcon(sItemPcState->itemMenuIconSlot ^ 1);
    ClearWindowTilemap(0);
    ClearWindowTilemap(1);
    FillBgTilemapBufferRect_Palette0(1, 0x02D, 11, 1, 18, 12);
    ScheduleBgCopyTilemapToVram(0);
    ScheduleBgCopyTilemapToVram(1);
    sItemPcPosition.pocket += direction;
    FrlgPcSetBagPocket(sItemPcPosition.pocket < 3 ? sItemPcPosition.pocket : 0);
    data[10] = 0;
    gTasks[taskId].func = Task_ItemPcSwitchPocket;
}

static void Task_ItemPcSwitchPocket(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 row = ++data[10];

    if (row <= 12)
    {
        CopyToBgTilemapBufferRect(1, &sDepositListTilemap[(12 - row) * 18], 11, 13 - row, 18, 1);
        ScheduleBgCopyTilemapToVram(1);
        return;
    }
    sItemPcPosition.scroll = sItemPcPosition.pocketScroll[sItemPcPosition.pocket];
    sItemPcPosition.row = sItemPcPosition.pocketRow[sItemPcPosition.pocket];
    ItemPc_CountPcItems();
    ItemPc_SetCursorPosition();
    ItemPc_BuildListMenuTemplate();
    data[0] = ListMenuInit(&gMultiuseListMenuTemplate, sItemPcPosition.scroll, sItemPcPosition.row);
    PutWindowTilemap(1);
    ItemPc_PlaceTopMenuScrollIndicatorArrows();
    ScheduleBgCopyTilemapToVram(0);
    gTasks[taskId].func = Task_ItemPcMain;
}

#include "global.h"
#include "bg.h"
#include "decompress.h"
#include "event_object_movement.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "gpu_regs.h"
#include "m4a.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "random.h"
#include "save.h"
#include "scanline_effect.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "strings.h"
#include "string_util.h"
#include "international_string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "trainer_pokemon_sprites.h"
#include "util.h"
#include "window.h"
#include "constants/event_objects.h"
#include "constants/pokedex.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/species.h"

// constants
#define TAG_POKEDEX_ENTRY   2000 // 2000-2006 used for each box
#define TAG_NUMBER          2007
#define TAG_CAUGHT_BALL     2008
#define MAX_ENTRY_BOXES     6

enum
{
    BOX_0,
    BOX_1,
    BOX_CENTER,
    BOX_3,
    BOX_4,
    BOX_HIDDEN,
};

// structs
struct EntryBoxData
{
    u16 dexIndex;
    u16 species;
    u8 leftSpriteId;
    u8 middleSpriteId;
    u8 rightSpriteId;
    u8 iconSpriteId;
    u8 numberSpriteIds[3];
    u8 ballSpriteId;
    u8 index;
    u8 arrIndex;
};

struct PokedexListItem
{
    u16 dexNum;
    u16 seen:1;
    u16 owned:1;
};

struct PokedexView
{
    u16 selectedSpecies;
    u8 selectedMonSpriteId;
    struct EntryBoxData entryBoxes[MAX_ENTRY_BOXES];
    struct PokedexListItem pokedexList[NATIONAL_DEX_COUNT + 1];
    u16 pokedexListCount;
};

// windows and bgs
enum Windows
{
    WINDOW_SEEN,
    WINDOW_OWN,
    WINDOW_COUNT,
};

static const struct WindowTemplate sPokedexEntryWinTemplate =
{
    .bg = 0,
    .tilemapLeft = 0,
    .tilemapTop = 0,
    .width = 8,
    .height = 3,
    .paletteNum = 0,
    .baseBlock = 1,
};

static const struct WindowTemplate sPokedexWinTemplates[WINDOW_COUNT + 1] =
{
    [WINDOW_SEEN] =
    {
        .bg = 1,
        .tilemapLeft = 3,
        .tilemapTop = 12,
        .width = 5,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 1 + 16,
    },
    [WINDOW_OWN] =
    {
        .bg = 1,
        .tilemapLeft = 3,
        .tilemapTop = 15,
        .width = 5,
        .height = 3,
        .paletteNum = 15,
        .baseBlock = 1 + 16 + 10,
    },
    DUMMY_WIN_TEMPLATE,
};

static const struct BgTemplate sPokedexBgTemplates[] =
{
    { // Sprites
        .bg = 3,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 2,
        .paletteMode = 0,
        .priority = 2,
        .baseTile = 0,
    },
    { // Background
        .bg = 2,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 2,
        .baseTile = 0,
    },
    { // Text
        .bg = 1,
        .charBaseIndex = 2,
        .mapBaseIndex = 6,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0,
    },
};

// graphics data
static const u16 sPokedexPalette[] = INCBIN_U16("graphics/pokedex_custom/tiles.gbapal");
static const u32 sPokedexTiles[] = INCBIN_U32("graphics/pokedex_custom/tiles.4bpp.lz");
static const u32 sPokedexTilemap[] = INCBIN_U32("graphics/pokedex_custom/tiles.bin.lz");

static const u32 sPokedexEntryGfx[] = INCBIN_U32("graphics/pokedex_custom/entry.4bpp.lz");
static const u16 sPokedexEntryPalette[] = INCBIN_U16("graphics/pokedex_custom/entry.gbapal");

static const u8 sNumberGfx[] = INCBIN_U8("graphics/pokedex_custom/number.4bpp");
static const u8 sCaughtBallGfx[] = INCBIN_U8("graphics/pokedex_custom/ball.4bpp");

static u32 sBlackPal = RGB(0, 0, 0);

static const struct SpritePalette sSpritePalette_PokedexEntry = {sPokedexEntryPalette, TAG_POKEDEX_ENTRY};

static const struct CompressedSpriteSheet sSpriteSheet_PokedexEntryBoxes[] =
{
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY},
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY + 1},
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY + 2},
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY + 3},
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY + 4},
    {sPokedexEntryGfx, 0x1000, TAG_POKEDEX_ENTRY + 5},
};

static const struct SpriteSheet sSpriteSheet_Number =
{
    sNumberGfx, sizeof(sNumberGfx), TAG_NUMBER
};

static const struct SpriteSheet sSpriteSheet_CaughtBall =
{
    sCaughtBallGfx, sizeof(sCaughtBallGfx), TAG_CAUGHT_BALL
};

static const struct OamData sOamData_8x8 =
{
    .x = 0,
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x8),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct OamData sOamData_16x16 =
{
    .x = 0,
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x16),
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x16),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct OamData sOamData_64x32 =
{
    .x = 0,
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x32),
    .matrixNum = 0,
    .size = SPRITE_SIZE(64x32),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct SpriteTemplate sPokedexEntrySpriteTemplates[] =
{
    {
        .tileTag = TAG_POKEDEX_ENTRY,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
    {
        .tileTag = TAG_POKEDEX_ENTRY + 1,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
    {
        .tileTag = TAG_POKEDEX_ENTRY + 2,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
    {
        .tileTag = TAG_POKEDEX_ENTRY + 3,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
    {
        .tileTag = TAG_POKEDEX_ENTRY + 4,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
    {
        .tileTag = TAG_POKEDEX_ENTRY + 5,
        .paletteTag = TAG_POKEDEX_ENTRY,
        .oam = &sOamData_64x32,
        .anims = gDummySpriteAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy,
    },
};

static void SpriteCB_EntryNumber(struct Sprite*);
static const struct SpriteTemplate sNumberSpriteTemplate =
{
    .tileTag = TAG_NUMBER,
    .paletteTag = TAG_POKEDEX_ENTRY, // shares entry box pal
    .oam = &sOamData_8x8,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_EntryNumber,
};

static void SpriteCB_CaughtBall(struct Sprite*);
static const struct SpriteTemplate sCaughtBallSpriteTemplate =
{
    .tileTag = TAG_CAUGHT_BALL,
    .paletteTag = TAG_POKEDEX_ENTRY, // shares entry box pal
    .oam = &sOamData_16x16,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_CaughtBall,
};

// constant data
static const s8 sEntryBoxPositions[MAX_ENTRY_BOXES + 1][2] =
{
    [6] = {126, -23}, // used as negative index
    [0] = {126, 11},
    [1] = {118, 43},
    [2] = {110, 75},
    [3] = {118, 107},
    [4] = {126, 139},
    [5] = {126, 171}, // farther than x=127 wraps around
};

// ewram data
EWRAM_DATA static u32 *sPokedexTilemapPtr = NULL;
EWRAM_DATA static struct PokedexView sPokedexViewData = {0};

// forward declarations
static void MainCB2_Pokedex(void);
static void VBlankCB_Pokedex(void);
static void Task_OpenPokedex(u8 taskId);
static void Task_ClosePokedex(u8 taskId);
static void Task_PokedexScrollUp(u8 taskId);
static void Task_PokedexScrollDown(u8 taskId);
static void Task_PokedexFinishScrollUp(u8 taskId);
static void Task_PokedexFinishScrollDown(u8 taskId);
static void Task_PokedexWaitForKeypress(u8 taskId);
static void Task_UpdateSelectedMonFrontSprite(u8 taskId);
static void Task_UpdateSelectedMonFrontSprite_Finish(u8 taskId);
static void LoadPokedexMainPageGfx(void);
static void PrintSeenOwnCount(void);
static void CreatePokedexWindows(void);
static void CreateSelectedMonFrontSprite(u32 species);
static void CreatePokedexEntryBox(struct EntryBoxData *box, u32 species);
static void DestroyPokedexEntryBox(struct EntryBoxData *box);
static void PrintNameOntoPokedexEntryBox(struct EntryBoxData *box);
static void PrintNumberOntoPokedexEntryBox(struct EntryBoxData *box);
static void CreateMonIconOnPokedexEntryBox(struct EntryBoxData *box);
static void CreateCaughtBallOnPokedexEntryBox(struct EntryBoxData *box);
static void InitPokedexEntryBoxData(void);
static struct EntryBoxData * GetFirstEmptyPokedexEntryBox(void);
static struct EntryBoxData * GetPokedexEntryBoxByIndex(u32 index);
static void CreatePokedexList(void);

// UI functions
static void MainCB2_Pokedex(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void VBlankCB_Pokedex(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_OpenPokedexCustom(void)
{
    switch (gMain.state) {
        default:
        case 0:
            UpdatePaletteFade();
            if (!gPaletteFade.active)
                gMain.state++;
            break;
        case 1:
            SetVBlankCallback(NULL); 
            ClearVramOamPlttRegs();
            SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
            gMain.state++;
            break;
        case 2:
            ClearTasksAndGraphicalStructs();
            gMain.state++;
            break;
        case 3:
            sPokedexTilemapPtr = AllocZeroed(BG_SCREEN_SIZE);
            ResetBgsAndClearDma3BusyFlags(0);
            InitBgsFromTemplates(0, sPokedexBgTemplates, ARRAY_COUNT(sPokedexBgTemplates));
            SetBgTilemapBuffer(2, sPokedexTilemapPtr);
            gMain.state++;
            break;
        case 4:
            DecompressAndCopyTileDataToVram(2, sPokedexTiles, 0, 0, 0);
            LZDecompressWram(sPokedexTilemap, sPokedexTilemapPtr);
            LoadPalette(sPokedexPalette, BG_PLTT_ID(0), PLTT_SIZE_4BPP);
            Menu_LoadStdPalAt(BG_PLTT_ID(15));
            LoadPalette(&sBlackPal, BG_PLTT_ID(0), PLTT_SIZEOF(1)); // black BG transp color
            gMain.state++;
            break;
        case 5:
            if (IsDma3ManagerBusyWithBgCopy() != TRUE)
            {
                HideBg(0);
                ShowBg(1);
                ShowBg(2);
                CopyBgTilemapBufferToVram(2);
                gMain.state++;
            }
            break;
        case 6:
            InitWindows(sPokedexWinTemplates);
            DeactivateAllTextPrinters();
            gMain.state++;
            break;
        case 7:
            m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x80);
            SetVBlankCallback(VBlankCB_Pokedex);
            CreateTask(Task_OpenPokedex, 0);
            SetMainCallback2(MainCB2_Pokedex);
            break;
    }
}

static void Task_OpenPokedex(u8 taskId)
{
    CreatePokedexList();
    InitPokedexEntryBoxData();
    LoadPokedexMainPageGfx();
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_PokedexWaitForKeypress;
}

#define tConsecutiveScrolls data[0]
#define tFrameCount         data[0] // for update front pic task

static void Task_PokedexScrollUp(u8 taskId)
{
    u32 i, scrollMult;
    struct EntryBoxData *box;

    if (gTasks[taskId].tConsecutiveScrolls >= 3)
        scrollMult = 2;
    else
        scrollMult = 1;

    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        if (box->dexIndex == NATIONAL_DEX_COUNT)
            continue;
        gSprites[box->leftSpriteId].y2 += 4 * scrollMult;
        if (box->index < BOX_CENTER)
            gSprites[box->leftSpriteId].x2 -= 1 * scrollMult;
        else
            gSprites[box->leftSpriteId].x2 += 1 * scrollMult;
    }

    gSprites[box->leftSpriteId].animDelayCounter += 1 * scrollMult;
    if (gSprites[box->leftSpriteId].animDelayCounter >= 8)
    {
        gSprites[box->leftSpriteId].animDelayCounter = 0;
        gTasks[taskId].func = Task_PokedexFinishScrollUp;
    }
}

static void Task_PokedexScrollDown(u8 taskId)
{
    u32 i, scrollMult;
    struct EntryBoxData *box;

    if (gTasks[taskId].tConsecutiveScrolls >= 3)
        scrollMult = 2;
    else
        scrollMult = 1;

    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        if (box->dexIndex == NATIONAL_DEX_COUNT)
            continue;
        gSprites[box->leftSpriteId].y2 -= 4 * scrollMult;
        if (box->index <= BOX_CENTER)
            gSprites[box->leftSpriteId].x2 += 1 * scrollMult;
        else if (box->index < BOX_HIDDEN)
            gSprites[box->leftSpriteId].x2 -= 1 * scrollMult;
    }

    gSprites[box->leftSpriteId].animDelayCounter += 1 * scrollMult;
    if (gSprites[box->leftSpriteId].animDelayCounter >= 8)
    {
        gSprites[box->leftSpriteId].animDelayCounter = 0;
        gTasks[taskId].func = Task_PokedexFinishScrollDown;
    }
}

static void Task_PokedexFinishScrollUp(u8 taskId)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        // Change indices.
        if (box->index == BOX_HIDDEN)
            box->index = BOX_0;
        else if (box->index != 0xFF)
            box->index += 1;

        // Update positions.
        gSprites[box->leftSpriteId].x = sEntryBoxPositions[box->index][0];
        gSprites[box->leftSpriteId].y = sEntryBoxPositions[box->index][1];
        gSprites[box->leftSpriteId].x2 = 0;
        gSprites[box->leftSpriteId].y2 = 0;

        // Update front pic.
        if (box->index == BOX_CENTER)
        {
            gSprites[box->leftSpriteId].oam.objMode = ST_OAM_OBJ_NORMAL;
            CreateTask(Task_UpdateSelectedMonFrontSprite, 1);
            sPokedexViewData.selectedSpecies = box->species;
        }
        else
        {
            gSprites[box->leftSpriteId].oam.objMode = ST_OAM_OBJ_BLEND;
        }
    }

    if (sPokedexViewData.pokedexListCount > 4)
    {
    // Create or update the top hidden entry box.
    box = GetPokedexEntryBoxByIndex(BOX_0);
    if (box == NULL)
    {
        box = GetFirstEmptyPokedexEntryBox();
        box->index = BOX_0;
        CreatePokedexEntryBox(box, GetPokedexEntryBoxByIndex(BOX_0 + 1)->dexIndex - 1);
    }
    else if (box->dexIndex >= 0 + MAX_ENTRY_BOXES)
    {
        box->dexIndex -= MAX_ENTRY_BOXES;
        DestroyPokedexEntryBox(box);
        CreatePokedexEntryBox(box, box->dexIndex);
    }
    else // Or destroy it if there's nothing left in the list.
    {
        DestroyPokedexEntryBox(box);
        box->dexIndex = NATIONAL_DEX_COUNT;
        box->species = SPECIES_NONE;
        box->index = 0xFF;
        box->leftSpriteId = 0xFF;
    }
}

    // Allow exit or continue scrolling until the next seen species.
    if (gMain.heldKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Task_ClosePokedex;
        PlaySE(SE_PC_OFF);
    }
    else if (!GetSetPokedexFlag(SpeciesToNationalPokedexNum(sPokedexViewData.selectedSpecies), FLAG_GET_SEEN)
        || (gMain.heldKeys & DPAD_UP && GetPokedexEntryBoxByIndex(BOX_CENTER)->dexIndex > 0))
    {
        if (gTasks[taskId].tConsecutiveScrolls < 3)
            ++gTasks[taskId].tConsecutiveScrolls;
        PlaySE(SE_DEX_SCROLL);
        gTasks[taskId].func = Task_PokedexScrollUp;
    }
    else
    {
        gTasks[taskId].tConsecutiveScrolls = 0;
        gTasks[taskId].func = Task_PokedexWaitForKeypress;
    }
}

static void Task_PokedexFinishScrollDown(u8 taskId)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        // Change indices.
        if (box->index == BOX_0)
            box->index = BOX_HIDDEN;
        else if (box->index != 0xFF)
            box->index -= 1;

        // Update positions.
        gSprites[box->leftSpriteId].x = sEntryBoxPositions[box->index][0];
        gSprites[box->leftSpriteId].y = sEntryBoxPositions[box->index][1];
        gSprites[box->leftSpriteId].x2 = 0;
        gSprites[box->leftSpriteId].y2 = 0;

        // Update front pic.
        if (box->index == BOX_CENTER)
        {
            gSprites[box->leftSpriteId].oam.objMode = ST_OAM_OBJ_NORMAL;
            CreateTask(Task_UpdateSelectedMonFrontSprite, 1);
            sPokedexViewData.selectedSpecies = box->species;
        }
        else
        {
            gSprites[box->leftSpriteId].oam.objMode = ST_OAM_OBJ_BLEND;
        }
    }

    // Create or update the bottom hidden entry box, if there are enough species seen.
    if (sPokedexViewData.pokedexListCount > 4)
    {
        box = GetPokedexEntryBoxByIndex(BOX_HIDDEN);
        if (box == NULL)
        {
            box = GetFirstEmptyPokedexEntryBox();
            box->index = BOX_HIDDEN;
            CreatePokedexEntryBox(box, GetPokedexEntryBoxByIndex(BOX_HIDDEN-1)->dexIndex + 1);
        }
        else if (box->dexIndex + MAX_ENTRY_BOXES < sPokedexViewData.pokedexListCount)
        {
            box->dexIndex += MAX_ENTRY_BOXES;
            DestroyPokedexEntryBox(box);
            CreatePokedexEntryBox(box, box->dexIndex);
        }
        else // Or destroy it if there is nothing left in the list.
        {
            DestroyPokedexEntryBox(box);
            box->species = SPECIES_NONE;
            box->dexIndex = NATIONAL_DEX_COUNT;
            box->index = 0xFF;
            box->leftSpriteId = 0xFF;
        }
    }

    // Allow exit or continue scrolling until the next seen species.
    if (gMain.heldKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Task_ClosePokedex;
        PlaySE(SE_PC_OFF);
    }
    else if (!GetSetPokedexFlag(SpeciesToNationalPokedexNum(sPokedexViewData.selectedSpecies), FLAG_GET_SEEN)
        || (gMain.heldKeys & DPAD_DOWN && GetPokedexEntryBoxByIndex(BOX_CENTER)->dexIndex < sPokedexViewData.pokedexListCount - 1))
    {
        if (gTasks[taskId].tConsecutiveScrolls < 3)
            ++gTasks[taskId].tConsecutiveScrolls;
        PlaySE(SE_DEX_SCROLL);
        gTasks[taskId].func = Task_PokedexScrollDown;
    }
    else
    {
        gTasks[taskId].tConsecutiveScrolls = 0;
        gTasks[taskId].func = Task_PokedexWaitForKeypress;
    }
}

static void Task_PokedexWaitForKeypress(u8 taskId)
{
    if (gMain.heldKeys & DPAD_UP && GetPokedexEntryBoxByIndex(BOX_CENTER)->dexIndex > 0)
    {
        PlaySE(SE_DEX_SCROLL);
        gTasks[taskId].func = Task_PokedexScrollUp;
    }
    if (gMain.heldKeys & DPAD_DOWN && GetPokedexEntryBoxByIndex(BOX_CENTER)->dexIndex < sPokedexViewData.pokedexListCount - 1)
    {
        PlaySE(SE_DEX_SCROLL);
        gTasks[taskId].func = Task_PokedexScrollDown;        
    }
    if (gMain.newKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Task_ClosePokedex;
        PlaySE(SE_PC_OFF);
    }
}

static void Task_ClosePokedex(u8 taskId)
{
    // Free tilemap pointer and data.
    Free(sPokedexTilemapPtr);
    sPokedexTilemapPtr = NULL;
    CpuFill32(0, &sPokedexViewData, sizeof(sPokedexViewData));

    // Reset data and destroy task.
    FreeAllWindowBuffers();
    ResetSpriteData();
    UnlockPlayerFieldControls();
    UnfreezeObjectEvents();
    DestroyTask(taskId);

    // Return to overworld.
    SetMainCallback2(CB2_ReturnToFieldWithOpenMenu);
    m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x100);
}

// Update the front sprite by destroying and creating the new one on the following frame.
static void Task_UpdateSelectedMonFrontSprite(u8 taskId)
{
    if (gTasks[taskId].tFrameCount == 0)
    {
        FreeAndDestroyMonPicSprite(sPokedexViewData.selectedMonSpriteId);
        ++gTasks[taskId].tFrameCount;
    }
    else
    {
        gTasks[taskId].func = Task_UpdateSelectedMonFrontSprite_Finish;
    }
}

static void Task_UpdateSelectedMonFrontSprite_Finish(u8 taskId)
{
    if (GetSetPokedexFlag(SpeciesToNationalPokedexNum(sPokedexViewData.selectedSpecies), FLAG_GET_SEEN))
        sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(sPokedexViewData.selectedSpecies, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
    else
        sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(SPECIES_NONE, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
    DestroyTask(taskId);
}

#undef tConsecutiveScrolls
#undef tFrameCount

static void LoadPokedexMainPageGfx(void)
{
    LoadSpritePalette(&sSpritePalette_PokedexEntry);
    LoadSpriteSheet(&sSpriteSheet_Number);
    LoadSpriteSheet(&sSpriteSheet_CaughtBall);

    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT2_ALL);
    SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(8, 10));

    u32 i = BOX_CENTER;
    do
    {
        sPokedexViewData.entryBoxes[i].index = i;
        CreatePokedexEntryBox(&sPokedexViewData.entryBoxes[i], i - BOX_CENTER);
    } while (++i < MAX_ENTRY_BOXES && (i - BOX_CENTER) < sPokedexViewData.pokedexListCount);

    CreateSelectedMonFrontSprite(GetPokedexEntryBoxByIndex(BOX_CENTER)->species);
    gSprites[GetPokedexEntryBoxByIndex(BOX_CENTER)->leftSpriteId].oam.objMode = ST_OAM_OBJ_NORMAL;
    CreatePokedexWindows();
    PrintSeenOwnCount();
}

static void CreatePokedexWindows(void)
{
    u32 i, windowId;
    for (i = 0; i < WINDOW_COUNT; ++i)
    {
        windowId = AddWindow(&sPokedexWinTemplates[i]);
        PutWindowTilemap(windowId);
        CopyWindowToVram(i, COPYWIN_FULL);
    }
}

static const u8 sTextColor_Normal[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_TRANSPARENT};

static void PrintSeenOwnCount(void)
{
    // Print SEEN count.
    ConvertIntToDecimalStringN(gStringVar4, GetNationalPokedexCount(FLAG_GET_SEEN), STR_CONV_MODE_RIGHT_ALIGN, 4);
    AddTextPrinterParameterized3(WINDOW_SEEN, FONT_NORMAL, 4, 3, sTextColor_Normal, TEXT_SKIP_DRAW, gStringVar4);
    CopyWindowToVram(WINDOW_SEEN, COPYWIN_FULL);

    // Print OWN count.
    ConvertIntToDecimalStringN(gStringVar4, GetNationalPokedexCount(FLAG_GET_CAUGHT), STR_CONV_MODE_RIGHT_ALIGN, 4);
    AddTextPrinterParameterized3(WINDOW_OWN, FONT_NORMAL, 4, 5, sTextColor_Normal, TEXT_SKIP_DRAW, gStringVar4);
    CopyWindowToVram(WINDOW_OWN, COPYWIN_FULL);
}

static void CreateSelectedMonFrontSprite(u32 species)
{
    sPokedexViewData.selectedSpecies = species;
    if (GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_SEEN))
        sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(species, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
    else
        sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(SPECIES_NONE, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
}

#define sLeftSpriteId   data[0] // for middle, right, icon, number, and ball sprites
#define sDigitId        data[1] // for numbers

#define VRAM_OFFSET_LEFT_SPRITE (512 + 32 * 4)     // each increment of 32 pushes text forward one tile
#define VRAM_OFFSET_RIGHT_SPRITE (512)

static void SpriteCB_EntryBoxMiddle(struct Sprite *sprite)
{
    sprite->x = gSprites[sprite->sLeftSpriteId].x + 64;
    sprite->y = gSprites[sprite->sLeftSpriteId].y;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
    sprite->oam.objMode = gSprites[sprite->sLeftSpriteId].oam.objMode;
}

static void SpriteCB_EntryBoxRight(struct Sprite *sprite)
{
    sprite->x = gSprites[sprite->sLeftSpriteId].x + 128;
    sprite->y = gSprites[sprite->sLeftSpriteId].y;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
    sprite->oam.objMode = gSprites[sprite->sLeftSpriteId].oam.objMode;
}

static void CreatePokedexEntryBox(struct EntryBoxData *box, u32 dexIndex)
{
    box->dexIndex = dexIndex;
    box->species = NationalPokedexNumToSpecies(sPokedexViewData.pokedexList[dexIndex].dexNum);

    FreeSpriteTilesByTag(TAG_POKEDEX_ENTRY + box->arrIndex);
    LoadCompressedSpriteSheet(&sSpriteSheet_PokedexEntryBoxes[box->arrIndex]);
    box->leftSpriteId = CreateSprite(&sPokedexEntrySpriteTemplates[box->arrIndex], sEntryBoxPositions[box->index][0], sEntryBoxPositions[box->index][1], 16);

    box->middleSpriteId = CreateSprite(&sPokedexEntrySpriteTemplates[box->arrIndex], 0, 0, 16);
    gSprites[box->middleSpriteId].sLeftSpriteId = box->leftSpriteId;
    gSprites[box->middleSpriteId].oam.tileNum += 32;
    gSprites[box->middleSpriteId].callback = SpriteCB_EntryBoxMiddle;

    box->rightSpriteId = CreateSprite(&sPokedexEntrySpriteTemplates[box->arrIndex], 0, 0, 16);
    gSprites[box->rightSpriteId].sLeftSpriteId = box->leftSpriteId;
    gSprites[box->rightSpriteId].oam.tileNum += 64;
    gSprites[box->rightSpriteId].callback = SpriteCB_EntryBoxRight;

    PrintNameOntoPokedexEntryBox(box);
    CreateMonIconOnPokedexEntryBox(box);
    PrintNumberOntoPokedexEntryBox(box);
    CreateCaughtBallOnPokedexEntryBox(box);
}

static void DestroyPokedexEntryBox(struct EntryBoxData *box)
{
    FreeAndDestroyMonIconSprite(&gSprites[box->iconSpriteId]);
    DestroySprite(&gSprites[box->numberSpriteIds[0]]);
    DestroySprite(&gSprites[box->numberSpriteIds[1]]);
    DestroySprite(&gSprites[box->numberSpriteIds[2]]);
    DestroySprite(&gSprites[box->leftSpriteId]);
    DestroySprite(&gSprites[box->middleSpriteId]);
    DestroySprite(&gSprites[box->rightSpriteId]);
    DestroySprite(&gSprites[box->ballSpriteId]);
}

static const u8 sTextColor_Name[] = {2, 1, 0}; // black bg, white text

static void PrintNameOntoPokedexEntryBox(struct EntryBoxData *box)
{
    u8 *windowTileData;
    void *objVram;
    u32 windowId, length;
    struct WindowTemplate winTemplate = sPokedexEntryWinTemplate;
    u8 *txtPtr = NULL;
    const u8 *speciesName;

    // Set up text.
    if (!sPokedexViewData.pokedexList[box->dexIndex].seen)
    {
        StringCopy(gStringVar3, COMPOUND_STRING("----"));
        StringCopy(gStringVar4, COMPOUND_STRING("------"));
        txtPtr = gStringVar4;
        length = 10;
    }
    else
    {
        speciesName = GetSpeciesName(box->species);
        StringCopy(gStringVar3, speciesName);
        length = StringLength(speciesName);
        if (length > 4)
        {
            StringCopy(gStringVar4, speciesName);
            txtPtr = &gStringVar4[4];
        }
    }

    // Print first four characters onto left sprite.
    windowId = AddWindow(&winTemplate);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
    AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, sTextColor_Name, TEXT_SKIP_DRAW, gStringVar3);

    objVram = (void *)(OBJ_VRAM0) + gSprites[box->leftSpriteId].oam.tileNum * TILE_SIZE_4BPP;
    windowTileData = (u8 *)(GetWindowAttribute(windowId, WINDOW_TILE_DATA));
    CpuCopy32(windowTileData + 256, objVram + VRAM_OFFSET_LEFT_SPRITE, 4 * TILE_SIZE_4BPP); // assumes min length of 4
    RemoveWindow(windowId);

    // Print remaining characters onto right sprite.
    if (length > 4)
    {
        windowId = AddWindow(&winTemplate);
        FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
        AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, sTextColor_Name, TEXT_SKIP_DRAW, txtPtr);

        objVram = (void *)(OBJ_VRAM0) + gSprites[box->middleSpriteId].oam.tileNum * TILE_SIZE_4BPP;
        windowTileData = (u8 *)(GetWindowAttribute(windowId, WINDOW_TILE_DATA));
        CpuCopy32(windowTileData + 256, objVram + VRAM_OFFSET_RIGHT_SPRITE, (length - 4) * TILE_SIZE_4BPP);
        RemoveWindow(windowId);
    }
}

static void SpriteCB_EntryNumber(struct Sprite* sprite)
{
    sprite->x = gSprites[sprite->sLeftSpriteId].x + 20 + 7 * sprite->sDigitId;
    sprite->y = gSprites[sprite->sLeftSpriteId].y - 8;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
    sprite->oam.objMode = gSprites[sprite->sLeftSpriteId].oam.objMode;
}

static void PrintNumberOntoPokedexEntryBox(struct EntryBoxData *box)
{
    u32 leftId, middleId, rightId;
    u32 num = gSpeciesInfo[box->species].natDexNum;

    // Store each digit.
    u32 hundreds = 0, tens = 0, ones = 0;
    while (num >= 100)
    {
        hundreds++;
        num -= 100;
    }
    while (num >= 10)
    {
        tens++;
        num -= 10;
    }
    ones = num;

    // Create sprites.
    leftId = box->numberSpriteIds[0] = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[leftId].oam.tileNum += 1 * hundreds;
    gSprites[leftId].sLeftSpriteId = box->leftSpriteId;
    gSprites[leftId].sDigitId = 0;

    middleId = box->numberSpriteIds[1] = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[middleId].oam.tileNum += 1 * tens;
    gSprites[middleId].sLeftSpriteId = box->leftSpriteId;
    gSprites[middleId].sDigitId = 1;

    rightId = box->numberSpriteIds[2] = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[rightId].oam.tileNum += 1 * ones;
    gSprites[rightId].sLeftSpriteId = box->leftSpriteId;
    gSprites[rightId].sDigitId = 2;
}

static void SpriteCB_MonIconDex(struct Sprite *sprite)
{
    UpdateMonIconFrame(sprite);
    sprite->x = gSprites[sprite->sLeftSpriteId].x - 18;
    sprite->y = gSprites[sprite->sLeftSpriteId].y - 2;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
    sprite->oam.objMode = gSprites[sprite->sLeftSpriteId].oam.objMode;
}

static void CreateMonIconOnPokedexEntryBox(struct EntryBoxData *box)
{
    if (sPokedexViewData.pokedexList[box->dexIndex].seen)
    {
        LoadMonIconPalette(box->species);
        box->iconSpriteId = CreateMonIconNoPersonality(GetIconSpeciesNoPersonality(box->species), SpriteCB_MonIconDex, 0, 0, 0);
        gSprites[box->iconSpriteId].oam.priority = 3;
        gSprites[box->iconSpriteId].sLeftSpriteId = box->leftSpriteId;
    }
}

static void SpriteCB_CaughtBall(struct Sprite* sprite)
{
    sprite->x = gSprites[sprite->sLeftSpriteId].x + 95;
    sprite->y = gSprites[sprite->sLeftSpriteId].y + 1;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
    sprite->oam.objMode = gSprites[sprite->sLeftSpriteId].oam.objMode;
}

static void CreateCaughtBallOnPokedexEntryBox(struct EntryBoxData *box)
{
    if (sPokedexViewData.pokedexList[box->dexIndex].owned)
    {
        box->ballSpriteId = CreateSprite(&sCaughtBallSpriteTemplate, 0, 0, 0);
        gSprites[box->ballSpriteId].sLeftSpriteId = box->leftSpriteId;
    }
}

#undef sLeftSpriteId
#undef sDigitId

static void InitPokedexEntryBoxData(void)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        box->arrIndex = i;
        box->index = 0xFF;
        box->dexIndex = NATIONAL_DEX_COUNT;
        box->species = SPECIES_NONE;
        box->leftSpriteId = SPRITE_NONE;
        box->middleSpriteId = SPRITE_NONE;
        box->rightSpriteId = SPRITE_NONE;
        box->iconSpriteId = SPRITE_NONE;
        box->numberSpriteIds[0] = SPRITE_NONE;
        box->numberSpriteIds[1] = SPRITE_NONE;
        box->numberSpriteIds[2] = SPRITE_NONE;
    }
}

static struct EntryBoxData * GetFirstEmptyPokedexEntryBox(void)
{
    u32 i;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        if (sPokedexViewData.entryBoxes[i].index == 0xFF)
            return &sPokedexViewData.entryBoxes[i];
    }
    return NULL;
}

static struct EntryBoxData * GetPokedexEntryBoxByIndex(u32 index)
{
    u32 i;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        if (sPokedexViewData.entryBoxes[i].index == index)
            return &sPokedexViewData.entryBoxes[i];
    }
    return NULL;
}

static void CreatePokedexList(void)
{
    u32 i, dexNum, start = 0;
    for (i = 0; i < NATIONAL_DEX_COUNT; ++i)
    {
        dexNum = i + 1;
        if (GetSetPokedexFlag(i, FLAG_GET_SEEN))
        {
            start = i - 1;
            break;
        }
    }
    for (i = start; i < NATIONAL_DEX_COUNT; ++i)
    {
        dexNum = i + 1;
        sPokedexViewData.pokedexList[i - start].dexNum = dexNum; 
        sPokedexViewData.pokedexList[i - start].seen = GetSetPokedexFlag(dexNum, FLAG_GET_SEEN);
        sPokedexViewData.pokedexList[i - start].owned = GetSetPokedexFlag(dexNum, FLAG_GET_CAUGHT);
        if (sPokedexViewData.pokedexList[i - start].seen)
            sPokedexViewData.pokedexListCount = dexNum - start;
    }
}

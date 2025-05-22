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
#define MAX_ENTRY_BOXES     6

enum
{
    BOX_0,
    BOX_1,
    BOX_CENTER,
    BOX_3,
    BOX_4,
    BOX_HIDDEN,
    BOX_DESTROY,
};

// structs
struct EntryBoxData
{
    u16 species;
    u8 leftSpriteId;
    u8 middleSpriteId;
    u8 rightSpriteId;
    u8 iconSpriteId;
    u8 numberSpriteIds[3];
    u8 index;
    u8 arrIndex;
};

struct PokedexView
{
    u16 selectedSpecies;
    u8 selectedMonSpriteId;
    struct EntryBoxData entryBoxes[MAX_ENTRY_BOXES]; // circular array
    u8 entryBoxTop;
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
        .tilemapLeft = 4,
        .tilemapTop = 12,
        .width = 4,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 1 + 16,
    },
    [WINDOW_OWN] =
    {
        .bg = 1,
        .tilemapLeft = 4,
        .tilemapTop = 15,
        .width = 4,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 1 + 16 + 8,
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

static u32 sBlackPal = RGB(0, 0, 0);

static const struct SpritePalette sSpritePalette_PokedexEntry = {sPokedexEntryPalette, TAG_POKEDEX_ENTRY};

static const struct CompressedSpriteSheet sSpriteSheet_PokedexEntryBoxes[] =
{
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY},
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY + 1},
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY + 2},
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY + 3},
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY + 4},
    {sPokedexEntryGfx, 0x1080, TAG_POKEDEX_ENTRY + 5},
};

static const struct SpriteSheet sSpriteSheet_Number =
{
    sNumberGfx, sizeof(sNumberGfx), TAG_NUMBER
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

static const struct OamData sOamData_64x32 =
{
    .x = 0,
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
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
static void LoadPokedexMainPageGfx(void);
static void PrintSeenOwnCount(void);
static void CreatePokedexWindows(void);
static void CreateSelectedMonFrontSprite(u32 species);
static void UpdateSelectedMonFrontSprite(u32 species);
static void CreatePokedexEntryBox(struct EntryBoxData *box, u32 species);
static void DestroyPokedexEntryBox(struct EntryBoxData *box);
static void PrintNameOntoPokedexEntryBox(struct EntryBoxData *box);
static void PrintNumberOntoPokedexEntryBox(struct EntryBoxData *box);
static void CreateMonIconOnPokedexEntryBox(struct EntryBoxData *box);
static void InitPokedexEntryBoxData(void);
static struct EntryBoxData * GetFirstEmptyPokedexEntryBox(void);
static struct EntryBoxData * GetPokedexEntryBoxByIndex(u32 index);

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
            // BlendPalettes(PALETTES_ALL, 16, 0);
            // BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            gMain.state++;
            break;
        case 8:
            m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x80);
            SetVBlankCallback(VBlankCB_Pokedex);
            CreateTask(Task_OpenPokedex, 0);
            SetMainCallback2(MainCB2_Pokedex);
            break;
    }
}

static void Task_OpenPokedex(u8 taskId)
{
    InitPokedexEntryBoxData();
    LoadPokedexMainPageGfx();
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_PokedexWaitForKeypress;
}

static void Task_PokedexScrollUp(u8 taskId)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        if (box->species == SPECIES_NONE)
            continue;
        gSprites[box->leftSpriteId].y2 += 4;
        if (box->index < BOX_CENTER)
            gSprites[box->leftSpriteId].x2 -= 1;
        else
            gSprites[box->leftSpriteId].x2 += 1;
    }

    if (++gSprites[box->leftSpriteId].animDelayCounter >= 8)
    {
        gSprites[box->leftSpriteId].animDelayCounter = 0;
        gTasks[taskId].func = Task_PokedexFinishScrollUp;
    }
}

static void Task_PokedexScrollDown(u8 taskId)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        if (box->species == SPECIES_NONE)
            continue;
        gSprites[box->leftSpriteId].y2 -= 4;
        if (box->index <= BOX_CENTER)
            gSprites[box->leftSpriteId].x2 += 1;
        else if (box->index < BOX_HIDDEN)
            gSprites[box->leftSpriteId].x2 -= 1;
    }

    if (++gSprites[box->leftSpriteId].animDelayCounter >= 8)
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
            UpdateSelectedMonFrontSprite(box->species);
    }

    box = GetPokedexEntryBoxByIndex(BOX_0);
    if (box == NULL)
    {
        box = GetFirstEmptyPokedexEntryBox();
        box->index = BOX_0;
        CreatePokedexEntryBox(box, GetPokedexEntryBoxByIndex(BOX_0 + 1)->species - 1);
    }
    else if (box->species >= SPECIES_BULBASAUR + MAX_ENTRY_BOXES)
    {
        box->species -= MAX_ENTRY_BOXES;
        DestroyPokedexEntryBox(box);
        CreatePokedexEntryBox(box, box->species);
    }
    else
    {
        DestroyPokedexEntryBox(box);
        box->species = SPECIES_NONE;
        box->index = 0xFF;
        box->leftSpriteId = 0xFF;
    }

    gTasks[taskId].func = Task_PokedexWaitForKeypress;
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
            UpdateSelectedMonFrontSprite(box->species);
    }

    box = GetPokedexEntryBoxByIndex(BOX_HIDDEN);
    if (box == NULL)
    {
        box = GetFirstEmptyPokedexEntryBox();
        box->index = BOX_HIDDEN;
        CreatePokedexEntryBox(box, GetPokedexEntryBoxByIndex(BOX_HIDDEN-1)->species + 1);
    }
    else if (box->species <= SPECIES_NIDORAN_F - MAX_ENTRY_BOXES)
    {
        box->species += MAX_ENTRY_BOXES;
        DestroyPokedexEntryBox(box);
        CreatePokedexEntryBox(box, box->species);
    }
    else
    {
        DestroyPokedexEntryBox(box);
        box->species = SPECIES_NONE;
        box->index = 0xFF;
        box->leftSpriteId = 0xFF;
    }

    gTasks[taskId].func = Task_PokedexWaitForKeypress;
}

static void Task_PokedexWaitForKeypress(u8 taskId)
{
    if (gMain.heldKeys & DPAD_UP && sPokedexViewData.selectedSpecies != SPECIES_BULBASAUR)
    {
        gTasks[taskId].func = Task_PokedexScrollUp;
    }
    if (gMain.heldKeys & DPAD_DOWN && sPokedexViewData.selectedSpecies != SPECIES_NIDORAN_F)
    {
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

static void LoadPokedexMainPageGfx(void)
{
    LoadSpritePalette(&sSpritePalette_PokedexEntry);
    LoadSpriteSheet(&sSpriteSheet_Number);

    sPokedexViewData.entryBoxes[2].index = 2;
    sPokedexViewData.entryBoxes[3].index = 3;
    sPokedexViewData.entryBoxes[4].index = 4;
    sPokedexViewData.entryBoxes[5].index = 5;

    CreatePokedexEntryBox(&sPokedexViewData.entryBoxes[2], SPECIES_BULBASAUR);
    CreatePokedexEntryBox(&sPokedexViewData.entryBoxes[3], SPECIES_BULBASAUR + 1);
    CreatePokedexEntryBox(&sPokedexViewData.entryBoxes[4], SPECIES_BULBASAUR + 2);
    CreatePokedexEntryBox(&sPokedexViewData.entryBoxes[5], SPECIES_BULBASAUR + 3);

    CreateSelectedMonFrontSprite(GetPokedexEntryBoxByIndex(BOX_CENTER)->species);
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
    ConvertIntToDecimalStringN(gStringVar4, GetNationalPokedexCount(FLAG_GET_SEEN), STR_CONV_MODE_LEFT_ALIGN, 3);
    AddTextPrinterParameterized3(WINDOW_SEEN, FONT_NORMAL, 4, 3, sTextColor_Normal, TEXT_SKIP_DRAW, gStringVar4);
    CopyWindowToVram(WINDOW_SEEN, COPYWIN_FULL);

    // Print OWN count.
    ConvertIntToDecimalStringN(gStringVar4, GetNationalPokedexCount(FLAG_GET_CAUGHT), STR_CONV_MODE_LEFT_ALIGN, 3);
    AddTextPrinterParameterized3(WINDOW_OWN, FONT_NORMAL, 4, 4, sTextColor_Normal, TEXT_SKIP_DRAW, gStringVar4);
    CopyWindowToVram(WINDOW_OWN, COPYWIN_FULL);
}

static void CreateSelectedMonFrontSprite(u32 species)
{
    sPokedexViewData.selectedSpecies = species;
    sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(species, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
}

static void UpdateSelectedMonFrontSprite(u32 species)
{
    sPokedexViewData.selectedSpecies = species;
    FreeAndDestroyMonPicSprite(sPokedexViewData.selectedMonSpriteId); // sprite sometimes destroyed frame after palette
    sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(species, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
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
}

static void SpriteCB_EntryBoxRight(struct Sprite *sprite)
{
    sprite->x = gSprites[sprite->sLeftSpriteId].x + 128;
    sprite->y = gSprites[sprite->sLeftSpriteId].y;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
}

static void CreatePokedexEntryBox(struct EntryBoxData *box, u32 species)
{
    box->species = species;

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
}

static void DestroyPokedexEntryBox(struct EntryBoxData *box)
{
    FreeAndDestroyMonIconSprite(&gSprites[box->iconSpriteId]);
    DestroySprite(&gSprites[box->numberSpriteIds[0]]);
    DestroySprite(&gSprites[box->numberSpriteIds[1]]);
    DestroySprite(&gSprites[box->numberSpriteIds[2]]);
    FreeSpriteTiles(&gSprites[box->leftSpriteId]);
    DestroySprite(&gSprites[box->leftSpriteId]);
    DestroySprite(&gSprites[box->middleSpriteId]);
    DestroySprite(&gSprites[box->rightSpriteId]);
}

static void PrintNameOntoPokedexEntryBox(struct EntryBoxData *box)
{
    u8 *windowTileData;
    void *objVram;
    u32 windowId;
    u8 color[] = {2, 1, 0}; // black bg, white text
    struct WindowTemplate winTemplate = sPokedexEntryWinTemplate;

    // Set up text.
    u8 *txtPtr = NULL;
    const u8 *speciesName = GetSpeciesName(box->species);
    u32 length = StringLength(speciesName);
    StringCopy(gStringVar3, speciesName);
    if (length > 4)
    {
        StringCopy(gStringVar4, speciesName);
        txtPtr = &gStringVar4[4];
    }

    // Print first four characters onto left sprite.
    windowId = AddWindow(&winTemplate);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
    AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, color, TEXT_SKIP_DRAW, gStringVar3);

    objVram = (void *)(OBJ_VRAM0) + gSprites[box->leftSpriteId].oam.tileNum * TILE_SIZE_4BPP;
    windowTileData = (u8 *)(GetWindowAttribute(windowId, WINDOW_TILE_DATA));
    CpuCopy32(windowTileData + 256, objVram + VRAM_OFFSET_LEFT_SPRITE, 4 * TILE_SIZE_4BPP); // assumes min length of 4
    RemoveWindow(windowId);

    // Print remaining characters onto right sprite.
    if (length > 4)
    {
        windowId = AddWindow(&winTemplate);
        FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
        AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, color, TEXT_SKIP_DRAW, txtPtr);

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
    sprite->y = gSprites[sprite->sLeftSpriteId].y - 1;
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
}

static void CreateMonIconOnPokedexEntryBox(struct EntryBoxData *box)
{
    LoadMonIconPalette(box->species);
    box->iconSpriteId = CreateMonIconNoPersonality(GetIconSpeciesNoPersonality(box->species), SpriteCB_MonIconDex, 0, 0, 0);
    gSprites[box->iconSpriteId].oam.priority = 3;
    gSprites[box->iconSpriteId].sLeftSpriteId = box->leftSpriteId;
}

static void InitPokedexEntryBoxData(void)
{
    u32 i;
    struct EntryBoxData *box;
    for (i = 0; i < MAX_ENTRY_BOXES; ++i)
    {
        box = &sPokedexViewData.entryBoxes[i];
        box->arrIndex = i;
        box->index = 0xFF;
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
        DebugPrintf("box %d: %d", i, sPokedexViewData.entryBoxes[i].index);
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

#undef sLeftSpriteId
#undef sDigitId

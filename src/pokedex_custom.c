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

#define TAG_POKEDEX_ENTRY   2000
#define TAG_NUMBER          2001

#define MAX_ENTRY_BOXES     7

struct EntryBoxData
{
    u16 species;
    u8 leftSpriteId;
    u8 middleSpriteId;
    u8 rightSpriteId;
    u8 iconSpriteId;
    u8 numberSpriteIds[3];
    u8 index;
};
struct PokedexView
{
    u16 selectedSpecies;
    u8 selectedMonSpriteId;
    struct EntryBoxData entryBoxes[MAX_ENTRY_BOXES];
};

EWRAM_DATA static u32 *sPokedexTilemapPtr = NULL;
EWRAM_DATA static struct PokedexView sPokedexViewData = {0};

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

static const u16 sPokedexPalette[] = INCBIN_U16("graphics/pokedex_custom/tiles.gbapal");
static const u32 sPokedexTiles[] = INCBIN_U32("graphics/pokedex_custom/tiles.4bpp.lz");
static const u32 sPokedexTilemap[] = INCBIN_U32("graphics/pokedex_custom/tiles.bin.lz");

static const u32 sPokedexEntryGfx[] = INCBIN_U32("graphics/pokedex_custom/entry.4bpp.lz");
static const u16 sPokedexEntryPalette[] = INCBIN_U16("graphics/pokedex_custom/entry.gbapal");

static const u8 sNumberGfx[] = INCBIN_U8("graphics/pokedex_custom/number.4bpp");

static u32 sBlackPal = RGB(0, 0, 0);

static const struct SpritePalette sSpritePalette_PokedexEntry = {sPokedexEntryPalette, TAG_POKEDEX_ENTRY};

static const struct CompressedSpriteSheet sSpriteSheet_PokedexEntry =
{
    sPokedexEntryGfx, 0x2000, TAG_POKEDEX_ENTRY
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

static const struct OamData sOamData_64x64 =
{
    .x = 0,
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .matrixNum = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct SpriteTemplate sPokedexEntrySpriteTemplate =
{
    .tileTag = TAG_POKEDEX_ENTRY,
    .paletteTag = TAG_POKEDEX_ENTRY,
    .oam = &sOamData_64x64,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
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

static void MainCB2_Pokedex(void);
static void VBlankCB_Pokedex(void);
static void Task_OpenPokedex(u8 taskId);
static void Task_ClosePokedex(u8 taskId);
static void Task_PokedexWaitForKeypress(u8 taskId);
static void LoadPokedexMainPageGfx(void);
static void PrintSeenOwnCount(void);
static void DrawWindows(void);
static void CreateSelectedMonFrontSprite(u32 species);
static void CreatePokedexEntryBoxSprite(void);
static void PrintNameOntoPokedexEntryBox(u32 leftSpriteId, u32 species);
static void PrintNumberOntoPokedexEntryBox(u32 leftSpriteId, u32 species);
static void CreateMonIconOnPokedexEntryBox(u32 leftSpriteId, u32 species);

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
    LoadPokedexMainPageGfx();
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_PokedexWaitForKeypress;
}

static void Task_PokedexWaitForKeypress(u8 taskId)
{
    if (gMain.newKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Task_ClosePokedex;
        PlaySE(SE_PC_OFF);
    }
}

static void Task_ClosePokedex(u8 taskId)
{
    // Free tilemap pointer.
    Free(sPokedexTilemapPtr);
    sPokedexTilemapPtr = NULL;

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
    CreatePokedexEntryBoxSprite();
    CreateSelectedMonFrontSprite(1);
    DrawWindows();
    PrintSeenOwnCount();
}

static void DrawWindows(void)
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
    sPokedexViewData.selectedMonSpriteId = CreateMonPicSprite(species, FALSE, 0xFE, TRUE, 34, 40, 15, TAG_NONE);
}

#define sLeftSpriteId   data[0] // for middle, right, icon, number, and ball sprites
#define sMiddleSpriteId data[0] // for left sprite
#define sRightSpriteId  data[1] // for left sprite

#define sDigitId        data[1] // for numbers

#define VRAM_OFFSET_LEFT_SPRITE (1024 + 32 * 4)     // each increment of 32 pushes text forward one tile
#define VRAM_OFFSET_RIGHT_SPRITE (1024)

static void SpriteCB_EntryBox(struct Sprite *sprite)
{
    sprite->x2 = gSprites[sprite->sLeftSpriteId].x2;
    sprite->y2 = gSprites[sprite->sLeftSpriteId].y2;
}

static void CreatePokedexEntryBoxSprite(void)
{
    u32 leftId, middleId, rightId;

    LoadSpritePalette(&sSpritePalette_PokedexEntry);
    LoadCompressedSpriteSheet(&sSpriteSheet_PokedexEntry);
    leftId = CreateSprite(&sPokedexEntrySpriteTemplate, 110, 75, 0);

    middleId = CreateSprite(&sPokedexEntrySpriteTemplate, 174, 75, 0);
    gSprites[leftId].sMiddleSpriteId = middleId;
    gSprites[middleId].sLeftSpriteId = leftId;
    gSprites[middleId].oam.tileNum += 64;
    gSprites[middleId].callback = SpriteCB_EntryBox;

    rightId = CreateSprite(&sPokedexEntrySpriteTemplate, 238, 75, 0);
    gSprites[leftId].sRightSpriteId = rightId;
    gSprites[rightId].sLeftSpriteId = leftId;
    gSprites[rightId].oam.tileNum += 128;
    gSprites[rightId].callback = SpriteCB_EntryBox;

    PrintNameOntoPokedexEntryBox(leftId, 1);
    CreateMonIconOnPokedexEntryBox(leftId, 1);
    PrintNumberOntoPokedexEntryBox(leftId, 1);
}

static void PrintNameOntoPokedexEntryBox(u32 leftSpriteId, u32 species)
{
    u8 *windowTileData;
    void *objVram;
    u32 windowId;
    u8 color[] = {2, 1, 0}; // black bg, white text
    struct WindowTemplate winTemplate = sPokedexEntryWinTemplate;

    // Set up text.
    u8 *txtPtr = NULL;
    const u8 *speciesName = GetSpeciesName(species);
    u32 length = StringLength(speciesName);
    StringCopy(gStringVar3, GetSpeciesName(species));
    if (length > 4)
    {
        StringCopy(gStringVar4, GetSpeciesName(species));
        txtPtr = &gStringVar4[4];
    }

    // Print first four characters onto left sprite.
    windowId = AddWindow(&winTemplate);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
    AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, color, TEXT_SKIP_DRAW, gStringVar3);

    objVram = (void *)(OBJ_VRAM0) + gSprites[leftSpriteId].oam.tileNum * TILE_SIZE_4BPP;
    windowTileData = (u8 *)(GetWindowAttribute(windowId, WINDOW_TILE_DATA));
    CpuCopy32(windowTileData + 256, objVram + VRAM_OFFSET_LEFT_SPRITE, 4 * TILE_SIZE_4BPP); // assumes min length of 4
    RemoveWindow(windowId);

    // Print remaining characters onto right sprite.
    if (length > 4)
    {
        windowId = AddWindow(&winTemplate);
        FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
        AddTextPrinterParameterized4(windowId, FONT_NORMAL, 0, 4, 0, 0, color, TEXT_SKIP_DRAW, txtPtr);

        objVram = (void *)(OBJ_VRAM0) + gSprites[gSprites[leftSpriteId].sMiddleSpriteId].oam.tileNum * TILE_SIZE_4BPP;
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

static void PrintNumberOntoPokedexEntryBox(u32 leftSpriteId, u32 species)
{
    u32 leftId, middleId, rightId;
    u32 num = gSpeciesInfo[species].natDexNum;

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
    LoadSpriteSheet(&sSpriteSheet_Number);
    leftId = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[leftId].oam.tileNum += 1 * hundreds;
    gSprites[leftId].sLeftSpriteId = leftSpriteId;
    gSprites[leftId].sDigitId = 0;

    middleId = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[middleId].oam.tileNum += 1 * tens;
    gSprites[middleId].sLeftSpriteId = leftSpriteId;
    gSprites[middleId].sDigitId = 1;

    rightId = CreateSprite(&sNumberSpriteTemplate, 0, 0, 0);
    gSprites[rightId].oam.tileNum += 1 * ones;
    gSprites[rightId].sLeftSpriteId = leftSpriteId;
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

static void CreateMonIconOnPokedexEntryBox(u32 leftSpriteId, u32 species)
{
    u32 spriteId;
    LoadMonIconPalette(species);
    spriteId = CreateMonIconNoPersonality(GetIconSpeciesNoPersonality(species), SpriteCB_MonIconDex, 0, 0, 0);
    gSprites[spriteId].oam.priority = 3;
    gSprites[spriteId].sLeftSpriteId = leftSpriteId;
}

#undef sLeftSpriteId
#undef sMiddleSpriteId
#undef sRightSpriteId

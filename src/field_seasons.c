#include "global.h"
#include "bg.h"
#include "event_data.h"
#include "field_camera.h"
#include "field_seasons.h"
#include "global.fieldmap.h"
#include "fieldmap.h"
#include "fldeff.h"
#include "fldeff_misc.h"
#include "malloc.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "task.h"
#include "constants/rgb.h"

EWRAM_DATA u16 *gTempOverworldTilemapBuffer_Bg1 = NULL;
EWRAM_DATA u16 *gTempOverworldTilemapBuffer_Bg2 = NULL;
EWRAM_DATA u16 *gTempOverworldTilemapBuffer_Bg3 = NULL;
EWRAM_DATA u8 gCurrentSeason = 0;
EWRAM_DATA static const struct Tileset *sPrimaryTilesetOverride = NULL;

static void DrawMetatileInTempBuffer(s32 metatileLayerType, const u16 *tiles, u16 offset);
static void DrawMetatileAtInTempBuffer(u16 offset, int x, int y);
static void LoadMapViewToTempBuffer(void);
static void Task_ReloadTilesetAndMetatiles(u8 taskId);
static void Task_ExecuteSeasonTransition(u8 taskId);
static const struct Tileset *GetTilesetSeasonalVariant(const struct Tileset *tileset, u32 season);

#define tState          data[0]
#define tChildId        data[1]
#define tFrameCounter   data[2]
#define tAnimIndex      data[3]

const struct Tileset *GetMapLayoutPrimaryTileset(struct MapLayout const *mapLayout)
{
    if (sPrimaryTilesetOverride != NULL)
        return sPrimaryTilesetOverride;
    else
        return GetTilesetSeasonalVariant(mapLayout->primaryTileset, gCurrentSeason);
}

static void Task_ReloadTilesetAndMetatiles(u8 taskId)
{
    switch (gTasks[taskId].tState)
    {
        case 0:
            gTempOverworldTilemapBuffer_Bg1 = AllocZeroed(BG_SCREEN_SIZE);
            gTempOverworldTilemapBuffer_Bg2 = AllocZeroed(BG_SCREEN_SIZE);
            gTempOverworldTilemapBuffer_Bg3 = AllocZeroed(BG_SCREEN_SIZE);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg1, gTempOverworldTilemapBuffer_Bg1, BG_SCREEN_SIZE);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg2, gTempOverworldTilemapBuffer_Bg2, BG_SCREEN_SIZE);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg3, gTempOverworldTilemapBuffer_Bg3, BG_SCREEN_SIZE);
            ++gTasks[taskId].tState;
            break;
        case 1:
            LoadMapViewToTempBuffer();
            ++gTasks[taskId].tState;
            break;
        case 2:
            CopyMapTilesetsToVram(gMapHeader.mapLayout);
            LoadMapTilesetPalettes(gMapHeader.mapLayout);
            UpdateAltBgPalettes(PALETTES_BG);
            UpdatePalettesWithTime(PALETTES_BG);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg1, gOverworldTilemapBuffer_Bg1, BG_SCREEN_SIZE);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg2, gOverworldTilemapBuffer_Bg2, BG_SCREEN_SIZE);
            CpuCopy16(gTempOverworldTilemapBuffer_Bg3, gOverworldTilemapBuffer_Bg3, BG_SCREEN_SIZE);
            ScheduleBgCopyTilemapToVram(1);
            ScheduleBgCopyTilemapToVram(2);
            ScheduleBgCopyTilemapToVram(3);
            ++gTasks[taskId].tState;
        case 3:
            Free(gTempOverworldTilemapBuffer_Bg1);
            Free(gTempOverworldTilemapBuffer_Bg2);
            Free(gTempOverworldTilemapBuffer_Bg3);
            gTempOverworldTilemapBuffer_Bg1 = NULL;
            gTempOverworldTilemapBuffer_Bg2 = NULL;
            gTempOverworldTilemapBuffer_Bg3 = NULL;
            ++gTasks[taskId].tState;
            break;
        default:
            DestroyTask(taskId);
            break;
    }
}

static void Task_ExecuteSeasonTransition(u8 taskId)
{
    const struct Tileset *tileset = GetTilesetSeasonalVariant(gMapHeader.mapLayout->primaryTileset, gCurrentSeason);
    switch (gTasks[taskId].tState)
    {
        // If no transition anim, just update the current tileset and end.
        case 0:
            if (tileset->transitionAnim != NULL)
            {
                ++gTasks[taskId].tState;
            }
            else
            {
                CreateTask(Task_ReloadTilesetAndMetatiles, 0);
                DestroyTask(taskId);
            }
            break;
        // Load next transition frame.
        case 1:
            if (tileset->transitionAnim[gTasks[taskId].tAnimIndex] != NULL)
                sPrimaryTilesetOverride = tileset->transitionAnim[gTasks[taskId].tAnimIndex];
            else
                sPrimaryTilesetOverride = NULL;
            gTasks[taskId].tChildId = CreateTask(Task_ReloadTilesetAndMetatiles, 0);
            ++gTasks[taskId].tAnimIndex;
            ++gTasks[taskId].tState;
            break;
        // Wait for load to finish.
        case 2:
            if (gTasks[gTasks[taskId].tChildId].tState > 3)
                ++gTasks[taskId].tState;
            break;
        // Wait to load next frame.
        case 3:
            if (++gTasks[taskId].tFrameCounter >= 15)
            {
                gTasks[taskId].tFrameCounter = 0;
                if (sPrimaryTilesetOverride == NULL) // anim done!
                    ++gTasks[taskId].tState;
                else
                    gTasks[taskId].tState = 1;
            }
            break;
        // Remove override, load tileset, and destroy task.
        default:
        case 4:
            DestroyTask(taskId);
            break;
    }
}

#undef tState
#undef tChildId
#undef tFrameCounter
#undef tAnimIndex

static const struct Tileset *GetTilesetSeasonalVariant(const struct Tileset *tileset, u32 season)
{
    if (tileset->seasons != NULL && tileset->seasons[season] != NULL)
        return tileset->seasons[season];
    else
        return tileset;
}

void IncrementSeason(void)
{
    ++gCurrentSeason;
    if (gCurrentSeason >= SEASONS_COUNT)
        gCurrentSeason = SEASON_SPRING;
    CreateTask(Task_ExecuteSeasonTransition, 0);
}

static void DrawMetatileInTempBuffer(s32 metatileLayerType, const u16 *tiles, u16 offset)
{
    switch (metatileLayerType)
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gTempOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gTempOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gTempOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gTempOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gTempOverworldTilemapBuffer_Bg2[offset] = 0;
        gTempOverworldTilemapBuffer_Bg2[offset + 1] = 0;
        gTempOverworldTilemapBuffer_Bg2[offset + 0x20] = 0;
        gTempOverworldTilemapBuffer_Bg2[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gTempOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gTempOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gTempOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gTempOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gTempOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gTempOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gTempOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gTempOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gTempOverworldTilemapBuffer_Bg2[offset] = tiles[4];
        gTempOverworldTilemapBuffer_Bg2[offset + 1] = tiles[5];
        gTempOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[6];
        gTempOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gTempOverworldTilemapBuffer_Bg1[offset] = 0;
        gTempOverworldTilemapBuffer_Bg1[offset + 1] = 0;
        gTempOverworldTilemapBuffer_Bg1[offset + 0x20] = 0;
        gTempOverworldTilemapBuffer_Bg1[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gTempOverworldTilemapBuffer_Bg3[offset] = 0x3014;
        gTempOverworldTilemapBuffer_Bg3[offset + 1] = 0x3014;
        gTempOverworldTilemapBuffer_Bg3[offset + 0x20] = 0x3014;
        gTempOverworldTilemapBuffer_Bg3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gTempOverworldTilemapBuffer_Bg2[offset] = tiles[0];
        gTempOverworldTilemapBuffer_Bg2[offset + 1] = tiles[1];
        gTempOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[2];
        gTempOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gTempOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gTempOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gTempOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gTempOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    }
}

static void DrawMetatileAtInTempBuffer(u16 offset, int x, int y)
{
    u16 metatileId = MapGridGetMetatileIdAt(x, y);
    const u16 *metatiles;

    if (metatileId > NUM_METATILES_TOTAL)
        metatileId = 0;
    if (metatileId < NUM_METATILES_IN_PRIMARY)
    {
        metatiles = GetMapLayoutPrimaryTileset(gMapHeader.mapLayout)->metatiles;
    }
    else
    {
        metatiles = gMapHeader.mapLayout->secondaryTileset->metatiles;
        metatileId -= NUM_METATILES_IN_PRIMARY;
    }
    DrawMetatileInTempBuffer(MapGridGetMetatileLayerTypeAt(x, y), metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
}

static void LoadMapViewToTempBuffer(void)
{
    u8 i;
    u8 j;
    u32 r6;
    u8 temp;
    u8 xOffset = 0;
    u8 yOffset = 0;

    GetCameraTileOffset(&xOffset, &yOffset);

    for (i = 0; i < 32; i += 2)
    {
        temp = yOffset + i;
        if (temp >= 32)
            temp -= 32;
        r6 = temp * 32;
        for (j = 0; j < 32; j += 2)
        {
            temp = xOffset + j;
            if (temp >= 32)
                temp -= 32;
            DrawMetatileAtInTempBuffer(r6 + temp, gSaveBlock1Ptr->pos.x + j / 2, gSaveBlock1Ptr->pos.y + i / 2);
        }
    }
}

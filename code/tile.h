#pragma once
#include "tufty.h"

static void
SetTileValue(s32 *TileValues, s32 IdxToChange, s32 NewValue)
{
    TileValues[IdxToChange] = NewValue;
}

static int
SearchAndReplaceTileValue(tile_map *pTileMap, s32 GetsReplaced, s32 ReplaceWith)
{
    int NumTilesInWorld = pTileMap->NumRows * pTileMap->NumCols;
    int NumValuesReplaced = 0;
    for (int NthTile = 0;
         NthTile < NumTilesInWorld;
         ++NthTile)
    {
        s32 *ThisTileValue = pTileMap->TileValues + NthTile;
        if (*ThisTileValue == GetsReplaced)
        {
            *ThisTileValue = ReplaceWith;
            ++NumValuesReplaced;
        }
    }
    return NumValuesReplaced;
}

static int
Get1DTileCoordFrom2DCoord(int NumCols, v2 CoordIn2D)
{
    int Result = CoordIn2D.Y * NumCols + CoordIn2D.X;
    return Result;
}

static v2
GetTileCoordsFromMouseCoords(f32 TileSideInPixels)
{
    v2 Result = {};
    Result.X = FloorF32ToS32(pGlobalMouse->X / (s32)TileSideInPixels);
    Result.Y = FloorF32ToS32(pGlobalMouse->Y / (s32)TileSideInPixels);
    return Result;
}

static s32
GetTileValueFrom1DCoord(s32 *pTileValues, int CoordIn1D)
{
    s32 Result = pTileValues[CoordIn1D];
    return Result;
}

static meta_bitmap *
GetTileTypePointer(const tile_map *pTileMap, int Slot)
{
    meta_bitmap *pResult = pTileMap->TileTypes.Get(Slot);
    return pResult;
}


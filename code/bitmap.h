#pragma once
#include "tufty.h"

static void
WriteFacingBitmapsDirpathToBuffer(player *pPlayer, facing_bitmaps *pFacingBitmaps, char *Buffer)
{
    // Subtract address of start of array containing all the facing bitmaps from the 
    //      address of this particular facing bitmaps array to determine which
    //      facing it is. e.g., East is the first element in the array, so calling
    //      this with the east facing array will produce a FacingIdx of 0, which
    //      will select "east" in the char *Facings array below
    mem_idx FacingIdx = pFacingBitmaps - pPlayer->FacingBitmaps;

    char *Facings[] = {"east", "north", "west", "south"};
    snprintf(Buffer, STRING_LEN, "player/%s", Facings[FacingIdx]);
}

static void
SetHeldBitmapToNil(cursor_state *pCursorState)
{
    // As a default, the cursor holds the nil bitmap of the east facing bitmaps array
    pCursorState->HeldFacingIdx = 0;
    pCursorState->HeldSlot = 0;
}

static arr_meta_bitmap *
GetPlayerArrMetaBitmapPtr(player *pPlayer, int FacingIdx)
{
    arr_meta_bitmap *pResult = nullptr;
    if (FacingIdx >= 0 && FacingIdx < 4) {
        pResult = &pPlayer->FacingBitmaps[FacingIdx].MetaBitmaps;
    }
    return pResult;
}

static void
ActuallyLoadBitmap(meta_bitmap *pIt)
{
    assert(pIt->IsPresent());
    ReadFileInto(pIt->Filepath, (u32)pIt->Bitmap.Buffer.Size, pIt->Bitmap.Buffer.Data);
    ParseBitmapHeader(&pIt->Bitmap);
    pIt->ReloadState.LastWriteTime = GetFileWriteTime(pIt->Filepath);
}

static reload_state
GetUpdatedReloadState(u64 const CheckOsWriteTime, const reload_state ReloadState)
{
    reload_state Result = ReloadState;
    if (ReloadState.WaitState == NOTHING) {
        if (CheckOsWriteTime && CheckOsWriteTime != ReloadState.LastWriteTime) {
            Result.WaitState = WAITING;
            Result.WaitedFrames = 0;
        }
    }
    else if (ReloadState.WaitState == WAITING) {
        ++Result.WaitedFrames;
        if (Result.WaitedFrames >= 3) {
            Result.WaitState = READY_TO_RELOAD;
        }
    }
    else if (ReloadState.WaitState == READY_TO_RELOAD) {
        if (CheckOsWriteTime && CheckOsWriteTime == ReloadState.LastWriteTime) {
            Result.WaitState = RELOADED;
        }
    }
    else {
        assert(ReloadState.WaitState == RELOADED);
        Result.WaitState = NOTHING;
    }
    return Result;
}


static void
ReloadBitmapIfChanged(arr_meta_bitmap *pMetaBitmaps, int Slot)
{
    meta_bitmap *pIt = pMetaBitmaps->Get(Slot);
    if (!pMetaBitmaps->IsNilBitmap(pIt) && pIt->IsPresent()) {
        u64 OsWriteTime = GetFileWriteTime(pIt->Filepath);
        pIt->ReloadState = GetUpdatedReloadState(OsWriteTime, pIt->ReloadState);
        if (pIt->ReloadState.WaitState == READY_TO_RELOAD) {
            mem_idx NewBitmapSize = GetFileSize(pIt->Filepath);
            bitmap *pItsBitmap = &pIt->Bitmap;
            if (NewBitmapSize != pItsBitmap->Buffer.Size) {
                pItsBitmap->Buffer.Data = PushArray(&pMetaBitmaps->PixelsArena, u8, NewBitmapSize);
                pItsBitmap->Buffer.Size = NewBitmapSize;
            }
            ActuallyLoadBitmap(pIt);
        }
    }
}

static found_filepath *
MakeOsFilepathsList(arena *pTransArena, buffer OsFilenames, int NumFilesFound, char *Dirname)
{
    // difference between "filenames", which are relative to the dir we passed to the OS, and "filepath",
    //      which is the path we constructed by appending the filename the OS gave us onto the end of a path,
    //      e.g., "tiles/flower.bmp": tiles is the dirname, flower is the filename, tiles/flower.bmp is a filepath

    found_filepath *pFoundFilepaths = PushArray(pTransArena, found_filepath, NumFilesFound);
    char *pOsFilenamesCursor = (char *)OsFilenames.Data;
    found_filepath *pFoundFilepathsCursor = pFoundFilepaths;
    for (int NthOsFilename = 0;
         NthOsFilename < NumFilesFound;
         ++NthOsFilename)
    {
        snprintf(pFoundFilepathsCursor->Filepath, STRING_LEN, "%s/%s", Dirname, pOsFilenamesCursor);
        ++pFoundFilepathsCursor;
        mem_idx FoundFilenameLen = strlen(pOsFilenamesCursor);
        ++FoundFilenameLen; // null terminator
        pOsFilenamesCursor += FoundFilenameLen;
    }

    return pFoundFilepaths;
}

static void
UpdateBitmapList(meta_bitmap *pMetaBitmapsList, 
                 int BitmapArrayLen, found_filepath *pOsFilepaths,
                 int NumOsFilepaths)
{
    // This function compares the filepaths we have in memory with the new filepaths 
    //      passed back by the OS (hereafter "OsFilepaths"). Any OsFilepaths
    //      that are already in memory are marked LoadedYet = true. Any
    //      that aren't in memory yet are appended on to the end of the list.
    int NumLoaded = 0;
    for (int Slot = 1;
         Slot <= BitmapArrayLen; // The full len of the array we allocate to store bitmaps, e.g.,
                                 //      10 in the case of player facing bitmaps, or
                                 //      200 in the case of tile bitmaps
         ++Slot)
    {
        meta_bitmap *It = pMetaBitmapsList + Slot;
        if (strlen(It->Filepath) > 0) 
        {
            for (int NthFoundFilepath = 0;
                 NthFoundFilepath < NumOsFilepaths;
                 ++NthFoundFilepath)
            {
                found_filepath *pCompareWith = pOsFilepaths + NthFoundFilepath;
                if (strncmp(It->Filepath, pCompareWith->Filepath, STRING_LEN) == 0)
                {
                    pCompareWith->LoadedYet = true;
                    break;
                }
            }
        }
    }

    for (int NthFoundFilepath = 0;
         NthFoundFilepath < NumOsFilepaths;
         ++NthFoundFilepath)
    {
        found_filepath *pCheckIfLoaded = pOsFilepaths + NthFoundFilepath;
        if (pCheckIfLoaded->LoadedYet == false) // If an OsFilepath
        {
            for (int Slot = 1;
                 Slot <= BitmapArrayLen;
                 ++Slot)
            {
                meta_bitmap *pThisMetaBitmap = pMetaBitmapsList + Slot;
                if (strlen(pThisMetaBitmap->Filepath) == 0)
                {
                    mem_idx FilepathLen = strlen(pCheckIfLoaded->Filepath);
                    // FilepathLen+1 below because of null terminator
                    snprintf(pThisMetaBitmap->Filepath, FilepathLen+1, "%s", pCheckIfLoaded->Filepath);
                    pCheckIfLoaded->LoadedYet = true;
                    break;
                }
            }
        }
    }
}

static void
LoadTileBitmapsDir(tile_map *pTileMap, arena *pTransArena)
{
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
    ResetArena(&pTileTypes->PixelsArena);

    // Clear bitmap structs in tile types array
    pTileTypes->ClearButLeaveFilepaths();

    buffer OsFilenames;
    OsFilenames.Size = MAX_TILE_TYPES * STRING_LEN;
    OsFilenames.Data = PushArray(pTransArena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, "tiles", &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if (NumFilesFound > MAX_TILE_TYPES || NumFilesFound == 0)
    {
        return;
    }

    found_filepath *pOsFilepaths = MakeOsFilepathsList(pTransArena, OsFilenames, NumFilesFound, "tiles");
    pTileTypes->UpdateBitmapList(pOsFilepaths, NumFilesFound);

    for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTileTypes->Get(Slot);
        if (pIt->IsPresent()) {
            ActuallyLoadBitmap(pIt);
        }
    }

    pTileMap->LastUpdateTime = GetDirWriteTime("tiles");
}


static void 
LoadPlayerBitmapsDir(player *pPlayer, facing_bitmaps *pFacingBitmaps, arena *pTransArena)
{
    arr_meta_bitmap *pMetaBitmaps = &pFacingBitmaps->MetaBitmaps;
    ResetArena(&pMetaBitmaps->PixelsArena);

    // Clear bitmap structs in meta bitmaps but leave the filepaths so that bitmaps
    //      staying in the game retain their array indices and therefore tile
    //      values referring to those indices don't get messed up
    pMetaBitmaps->ClearButLeaveFilepaths();

    char FacingDirpath[STRING_LEN];
    WriteFacingBitmapsDirpathToBuffer(pPlayer, pFacingBitmaps, FacingDirpath);

    buffer OsFilenames;
    OsFilenames.Size = MAX_FACING_BITMAPS * STRING_LEN;
    OsFilenames.Data = PushArray(pTransArena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, FacingDirpath, &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if ((NumFilesFound > MAX_FACING_BITMAPS) || (NumFilesFound == 0))
    {
        return;
    }

    found_filepath *pOsFilepaths = MakeOsFilepathsList(pTransArena, OsFilenames, NumFilesFound, FacingDirpath);
    pMetaBitmaps->UpdateBitmapList(pOsFilepaths, NumFilesFound);

    for (int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
        meta_bitmap *pIt = pMetaBitmaps->Get(Slot);
        if (pIt->IsPresent()) {
            ActuallyLoadBitmap(pIt);
        }
    }

    pFacingBitmaps->ReloadState.LastWriteTime = GetDirWriteTime(FacingDirpath);
}

static void
ScaleAndBlitBitmap(bitmap *pDestBitmap, v2 Min, v2 Max, bitmap *pSrcBitmap)
{
    if (!pSrcBitmap->Pixels)
    {
        return;
    }
    f32 XCoef = (f32)pSrcBitmap->Width / (Max.X - Min.X);
    f32 YCoef = (f32)pSrcBitmap->Height / (Max.Y - Min.Y);

    s32 DestMinY = RoundF32ToS32(Min.Y);
    s32 DestMaxY = RoundF32ToS32(Max.Y);
    s32 DestMinX = RoundF32ToS32(Min.X);
    s32 DestMaxX = RoundF32ToS32(Max.X);

    s32 SampledDestMinY = DestMinY;
    s32 SampledDestMaxY = DestMaxY;
    s32 SampledDestMinX = DestMinX;
    s32 SampledDestMaxX = DestMaxX;

    if (DestMinY < 0)
    {
        SampledDestMinY = 0;
    }
    if (DestMinX < 0)
    {
        SampledDestMinX = 0;
    }
    if (DestMaxY > pDestBitmap->Height)
    {
        SampledDestMaxY = pDestBitmap->Height;
    }
    if (DestMaxX > pDestBitmap->Width)
    {
        SampledDestMaxX = pDestBitmap->Width;
    }

    // Pixels are always 32 bits wide, memory order BB GG RR XX
    u8 *DestRow = (u8 *)pDestBitmap->Pixels + SampledDestMinY * pDestBitmap->Pitch + SampledDestMinX * BPP;
    for (int Y = SampledDestMinY; Y < SampledDestMaxY; ++Y)
    {
        s32 SrcRow = FloorF32ToS32(YCoef*((f32)(Y-DestMinY)+0.5f));
        u8 *DestPixel = DestRow;
        for (int X = SampledDestMinX; X < SampledDestMaxX; ++X)
        {
            s32 SrcCol = FloorF32ToS32(XCoef*((f32)(X-DestMinX)+0.5f));
            u8 *SrcPixel = pSrcBitmap->Pixels + SrcRow * pSrcBitmap->Pitch + SrcCol * BPP;

            // Get the alpha value
            f32 Alpha = ((f32)SrcPixel[3]) / 255.0f;

            //Blue
            DestPixel[0] = LerpS32(DestPixel[0], SrcPixel[0], Alpha);

            // Green
            DestPixel[1] = LerpS32(DestPixel[1], SrcPixel[1], Alpha);

            // Red
            DestPixel[2] = LerpS32(DestPixel[2], SrcPixel[2], Alpha);

            DestPixel += sizeof(u32);   
        }
        DestRow += pDestBitmap->Pitch;
    }
}

static void
ScaleAndBlitCel(bitmap *pDestBitmap, v2 Min, v2 Max, bitmap *pSrcBitmap)
{
    if (pSrcBitmap->Pixels == nullptr)
    {
        pSrcBitmap = &pGlobalBagel->Bitmap;
    }
    f32 XCoef = (f32)pSrcBitmap->Width / (Max.X - Min.X);
    f32 YCoef = (f32)pSrcBitmap->Height / (Max.Y - Min.Y);

    s32 DestMinY = RoundF32ToS32(Min.Y);
    s32 DestMaxY = RoundF32ToS32(Max.Y);
    s32 DestMinX = RoundF32ToS32(Min.X);
    s32 DestMaxX = RoundF32ToS32(Max.X);

    s32 SampledDestMinY = DestMinY;
    s32 SampledDestMaxY = DestMaxY;
    s32 SampledDestMinX = DestMinX;
    s32 SampledDestMaxX = DestMaxX;

    if (DestMinY < 0)
    {
        SampledDestMinY = 0;
    }
    if (DestMinX < 0)
    {
        SampledDestMinX = 0;
    }
    if (DestMaxY > pDestBitmap->Height)
    {
        SampledDestMaxY = pDestBitmap->Height;
    }
    if (DestMaxX > pDestBitmap->Width)
    {
        SampledDestMaxX = pDestBitmap->Width;
    }

    // Pixels are always 32 bits wide, memory order BB GG RR XX
    u8 *pDestRow = pDestBitmap->Pixels + SampledDestMinY * pDestBitmap->Pitch + SampledDestMinX * BPP;
    for (int Y = SampledDestMinY; Y < SampledDestMaxY; ++Y)
    {
        s32 SrcRow = FloorF32ToS32(YCoef*((f32)(Y-DestMinY)+0.5f));
        u8 *pDestPixel = pDestRow;
        for (int X = SampledDestMinX; X < SampledDestMaxX; ++X)
        {
            s32 SrcCol = FloorF32ToS32(XCoef*((f32)(X-DestMinX)+0.5f));
            u8 *SrcPixel = pSrcBitmap->Pixels + SrcRow * pSrcBitmap->Pitch + SrcCol * BPP;

            // Source is RGBA
            // Dest is BGRA

            // Get the alpha value
            f32 Alpha = ((f32)SrcPixel[3]) / 255.0f;

            //Blue
            pDestPixel[0] = LerpS32(pDestPixel[0], SrcPixel[2], Alpha);

            // Green
            pDestPixel[1] = LerpS32(pDestPixel[1], SrcPixel[1], Alpha);

            // Red
            pDestPixel[2] = LerpS32(pDestPixel[2], SrcPixel[0], Alpha);

            pDestPixel += sizeof(u32);   
        }
        pDestRow += pDestBitmap->Pitch;
    }
}
static void
DrawHeldTile(bitmap *pDestBitmap, v2 MouseCoords, bitmap *pSrcBitmap)
{
    v2 Min = MouseCoords + v2{20.0f, 20.0f};
    v2 Max = Min + v2{30.0f, 30.0f};
    ScaleAndBlitBitmap(pDestBitmap, Min, Max, pSrcBitmap);
}

static int
CountBitmaps(meta_bitmap *pMetaBitmapsList, mem_idx ListLen)
{
    int Count = 0;
    for (int Slot = 1;
         Slot <= ListLen;
         ++Slot)
    {
        if (strlen(pMetaBitmapsList[Slot].Filepath) > 0)
        {
            ++Count;
        }
    }
    return Count;
}

static void
ChangeHeldPlayerMetaBitmap(player *pPlayer, cursor_state *pCursorState,
                           int NewFacingIdx, int NewSlot)
{
    if (NewFacingIdx >= 0 && NewFacingIdx < 4) {
        // TODO: static analyzer flags us not using pNewFacingBitmaps, but since player
        //      editor is not working right now, cannot check to see if it's ok to remove this.
        // arr_meta_bitmap *pNewFacingBitmaps = &pPlayer->FacingBitmaps[NewFacingIdx].MetaBitmaps;
        pCursorState->HeldFacingIdx = NewFacingIdx;
        pCursorState->HeldSlot = NewSlot;
    }
}

static void
ChangeHeldTileType(tile_map *pTileMap, cursor_state *pCursorState, int NewSlot)
{
    pCursorState->HeldSlot = NewSlot;
}

static meta_bitmap *
GetPlayerMetaBitmapPointer(player *pPlayer, int FacingIdx, int Slot) 
{
    meta_bitmap *pResult;
    if (FacingIdx < 0 || FacingIdx >= 4) {
        pResult = &pPlayer->FacingBitmaps[0].MetaBitmaps.Data[0];
    }
    else {
        pResult = pPlayer->FacingBitmaps[FacingIdx].MetaBitmaps.Get(Slot);
    }
    return pResult;
}

static b32
IsBitmapPresent(bitmap *pBitmap)
{
    if (pBitmap->Pixels != nullptr) {
         return true;
    }
    else {
        return false;
    }
}

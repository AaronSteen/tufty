#include "tufty.h"
#include "stb_easy_font.h"

#define SCRATCH_SIZE Megabytes(1)

// DEBUG_PLATFORM_GET_FILE_SIZE(name) u32 name(char *Filename)
// DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory)
// DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename)
// DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(char *Filename, u33 MemorySize, void *Memory)
// DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename)
// DEBUG_PLATFORM_GET_DIR_WRITE_TIME(name) u64 name(char *SubDirName)
// DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(buffer *GamePackedFilenames, char *SubDirName, int *NumFilesFound)
//
// returns either the number of bytes read, or 0 if the file was missing, too big, or locked
// DEBUG_PLATFORM_READ_FILE_INTO(name) u32 name(char *Filename, u32 DestSize, void *Dest)
//
// IsSave = true if saving, false if loading
// #define DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(name) int name(char *Dest, int DestSize, b32 IsSave)
//

// GLOBALS
    // variables
    static meta_bitmap *pGlobalBagel;
    static game_controller_input *GlobalKeyboardController;
    static dev_keys *GlobalDevKeys;
    static game_mouse_input *pGlobalMouse;

struct vertex
{
    f32 X, Y, Z;
    u32 Color;
};

struct quad
{
    vertex TopLeft, TopRight, BottomRight, BottomLeft;
};

static void
ZeroArena(arena *Arena)
{
    memset(Arena->Data, 0, Arena->Size);
}

static void
ResetArena(arena *Arena)
{
    ZeroArena(Arena);
    Arena->Cursor = 0;
}

static scratch_arena *
GetScratchArena(scratch_header *ScratchHeader)
{
    for (int Idx = 0;
        Idx < ScratchHeader->Count;
        ++Idx)
    {
        scratch_arena *It = (scratch_arena *)ScratchHeader->ScratchArenas + Idx;
        if (It->IsFree)
        {
            It->IsFree = false;
            ResetArena(&It->Arena);
            return(It);
        }
    }

    // If we got here there were no free scratches and we need to make more
    Assert(!"Out of scratch arenas");
    return((scratch_arena *)0);
}

static void
FreeScratchArena(scratch_arena *Scratch)
{
    if (Scratch)
    {
        Scratch->IsFree = true;
    }
}

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

static b32
IsInRect(v2 Point, v2 RectMin, v2 RectMax)
{
    b32 Result = ( (Point.X >= RectMin.X) &&
                   (Point.Y >= RectMin.Y) &&
                   (Point.X < RectMax.X) &&
                   (Point.Y < RectMax.Y) );
    return(Result);
}

static void
SetHeldBitmapToNil(cursor_state *pCursorState)
{
    // As a default, the cursor holds the nil bitmap of the east facing bitmaps array
    pCursorState->HeldFacingIdx = 0;
    pCursorState->HeldSlot = 0;
}

static void
InitializeCursorState(cursor_state *pCursorState) 
{
    *pCursorState = {};
    SetHeldBitmapToNil(pCursorState);
}

static void
UpdateCursorStateHoveredFrames(cursor_state *pCursorState)
{
    pCursorState->LastFrameCoords = pCursorState->ThisFrameCoords;
    pCursorState->ThisFrameCoords = v2{(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    if (pCursorState->ThisFrameCoords == pCursorState->LastFrameCoords) {
        ++pCursorState->HoveredFrames;
    }
    else {
        pCursorState->HoveredFrames = 0;
    }
}

static arr_meta_bitmap *
GetPlayerArrMetaBitmapPtr(player *pPlayer, int FacingIdx)
{
    arr_meta_bitmap *pResult = nullptr;
    if (FacingIdx >= 0 && FacingIdx < 4) {
        pResult = &pPlayer->FacingBitmaps[FacingIdx].MetaBitmaps;
    }
    return(pResult);
}

static mem_idx
ComputeByteOffset(void *Start, void *Offset)
{
    // Compute how many bytes Offset is beyond Start, ignoring type.
    // Note that this is meant to be called in a serialization context, where you are packing bytes into a file
    //    contiguously (#pragma pack) and I am not sure how reliable or useful it is in other context because of 
    //    memory-alignment work done by the compiler
    // We ignore type because when serializing, it's important to think of offsets in terms of single bytes instead of 
        // typed offsets, because we are packing everything contiguously in the file. 
        // Just returns the raw count of single bytes between Offset and Start.
        // Caller is required to cast both arguments to void *, to emphasize that type information is disregarded.
        // e.g., Call this with int *Start and an Offset argument of any type that lives directly beyond Start in memory;
        //      the returned value will be 4.
    mem_idx Result = (u8 *)Offset - (u8 *)Start;
    return(Result);
}

static void
SetTileValue(tile_map *pTileMap, s32 IdxToChange, s32 NewValue)
{
    pTileMap->TileValues[IdxToChange] = NewValue;
    return;
}

static int
SearchAndReplaceTileValue(tile_map *pTileMap, s32 GetsReplaced, s32 ReplaceWith)
{
    int NumValuesReplaced = 0;
    for (int NthTile = 0;
        NthTile < pTileMap->NumTilesInWorld;
        ++NthTile)
    {
        s32 *ThisTileValue = pTileMap->TileValues + NthTile;
        if (*ThisTileValue == GetsReplaced)
        {
            *ThisTileValue = ReplaceWith;
            ++NumValuesReplaced;
        }
    }
    return(NumValuesReplaced);
}

static int
Get1DTileCoordFrom2DCoord(tile_map *pTileMap, v2 CoordIn2D)
{
    int Result = CoordIn2D.Y * pTileMap->NumCols + CoordIn2D.X;
    return(Result);
}

static v2
GetTileCoordsFromMouseCoords(f32 TileSideInPixels)
{
    v2 Result = {};
    Result.X = FloorF32ToS32(pGlobalMouse->X / (s32)TileSideInPixels);
    Result.Y = FloorF32ToS32(pGlobalMouse->Y / (s32)TileSideInPixels);
    return(Result);
}

static s32
GetTileValueFrom1DCoord(tile_map *pTileMap, int CoordIn1D)
{
    s32 Result = pTileMap->TileValues[CoordIn1D];
    return(Result);
}

static void
ActuallyLoadBitmap(meta_bitmap *pIt)
{
    Assert(pIt->IsPresent());
    ReadFileInto(pIt->Filepath, (u32)pIt->Bitmap.Buffer.Size, pIt->Bitmap.Buffer.Data);
    ParseBitmapHeader(&pIt->Bitmap);
    pIt->LastWriteTime = GetFileWriteTime(pIt->Filepath);
    pIt->WaitState = NOTHING;
    pIt->WaitedFrames = 0;
}

void
ReloadBitmapIfChanged(arr_meta_bitmap *pMetaBitmaps, int Slot)
{
    meta_bitmap *pIt = pMetaBitmaps->Get(Slot);
    if (!pMetaBitmaps->IsNilBitmap(pIt) && pIt->IsPresent()) {
        if (pIt->WaitState == READY_TO_RELOAD) {
            pIt->WaitState = NOTHING;
            pIt->LastWriteTime = GetFileWriteTime(pIt->Filepath);
            mem_idx NewBitmapSize = GetFileSize(pIt->Filepath);
            bitmap *pItsBitmap = &pIt->Bitmap;
            if (NewBitmapSize != pItsBitmap->Buffer.Size) {
                pItsBitmap->Buffer.Data = PushArray(&pMetaBitmaps->BitmapsArena, u8, NewBitmapSize);
                pItsBitmap->Buffer.Size = NewBitmapSize;
            }
            ActuallyLoadBitmap(pIt);
        }
        else if (pIt->WaitState == WAITING) {
            ++pIt->WaitedFrames;
            if (pIt->WaitedFrames >= 3) {
                pIt->WaitedFrames = 0;
                pIt->WaitState = READY_TO_RELOAD;
            }
        }
        else {
            u64 OsWriteTime = GetFileWriteTime(pIt->Filepath);
            if (OsWriteTime && OsWriteTime != pIt->LastWriteTime) {
                pIt->WaitState = WAITING;
                pIt->WaitedFrames = 0;
            }
        }
    }
}

static found_filepath *
MakeOsFilepathsList(arena *pScratchArena, buffer OsFilenames, int NumFilesFound, char *Dirname)
{
    // difference between "filenames", which are relative to the dir we passed to the OS, and "filepath",
    //      which is the path we constructed by appending the filename the OS gave us onto the end of a path,
    //      e.g., "tiles/flower.bmp": tiles is the dirname, flower is the filename, tiles/flower.bmp is a filepath

    found_filepath *pFoundFilepaths = PushArray(pScratchArena, found_filepath, NumFilesFound);
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

    return(pFoundFilepaths);
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
LoadTileBitmapsDir(tile_map *pTileMap, scratch_header *pScratchHeader)
{
    // TODO: Use CPP class constructor destructor setup to always 
    //      FreeScratchArena whenever any scope containing a 
    //      GetScratchArena call is exited
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
    ResetArena(&pTileTypes->BitmapsArena);
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);
    
    // Clear bitmap structs in tile types array
    pTileTypes->ClearButLeaveFilepaths();

    pTileMap->LastUpdateTime = GetDirWriteTime("tiles");
    pTileMap->WaitState = NOTHING;
    pTileMap->WaitedFrames = 0;

    buffer OsFilenames;
    OsFilenames.Size = MAX_TILE_TYPES * STRING_LEN;
    OsFilenames.Data = PushArray(&pScratch->Arena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, "tiles", &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if ((NumFilesFound > MAX_TILE_TYPES) || (NumFilesFound == 0))
    {
        FreeScratchArena(pScratch);
        return;
    }

    found_filepath *pOsFilepaths = MakeOsFilepathsList(&pScratch->Arena, OsFilenames, NumFilesFound, "tiles");
    pTileTypes->UpdateBitmapList(pOsFilepaths, NumFilesFound);

    for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTileTypes->Get(Slot);
        if (pIt->IsPresent()) {
            ActuallyLoadBitmap(pIt);
        }
    }

    FreeScratchArena(pScratch);
}


static void 
LoadPlayerBitmapsDir(player *pPlayer, facing_bitmaps *pFacingBitmaps, scratch_header *pScratchHeader)
{
    // TODO: Use CPP class constructor destructor setup to always 
    //      FreeScratchArena whenever any scope containing a 
    //      GetScratchArena call is exited
    arr_meta_bitmap *pMetaBitmaps = &pFacingBitmaps->MetaBitmaps;
    ResetArena(&pMetaBitmaps->BitmapsArena);
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);

    // Clear bitmap structs in meta bitmaps but leave the filepaths so that bitmaps
    //      staying in the game retain their array indices and therefore tile
    //      values referring to those indices don't get messed up
    pMetaBitmaps->ClearButLeaveFilepaths();

    char FacingDirpath[STRING_LEN];
    WriteFacingBitmapsDirpathToBuffer(pPlayer, pFacingBitmaps, FacingDirpath);
    pFacingBitmaps->LastUpdateTime = GetDirWriteTime(FacingDirpath);
    pFacingBitmaps->WaitState = NOTHING;
    pFacingBitmaps->WaitedFrames = 0;

    buffer OsFilenames;
    OsFilenames.Size = MAX_FACING_BITMAPS * STRING_LEN;
    OsFilenames.Data = PushArray(&pScratch->Arena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, FacingDirpath, &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if ((NumFilesFound > MAX_FACING_BITMAPS) || (NumFilesFound == 0))
    {
        FreeScratchArena(pScratch);
        return;
    }

    found_filepath *pOsFilepaths = MakeOsFilepathsList(&pScratch->Arena, OsFilenames, NumFilesFound, FacingDirpath);
    pMetaBitmaps->UpdateBitmapList(pOsFilepaths, NumFilesFound);

    for (int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
        meta_bitmap *pIt = pMetaBitmaps->Get(Slot);
        if (pIt->IsPresent()) {
            ActuallyLoadBitmap(pIt);
        }
    }

    FreeScratchArena(pScratch);
}

static void
DrawSimpleRect(game_offscreen_buffer *Buf,
               v2 Min, v2 Max,
               f32 R, f32 G, f32 B, f32 Alpha = 0.5f,
               b32 Outline = false)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    if (MinY < 0)
    {
        MinY = 0;
    }
    if (MaxY > Buf->Height)
    {
        MaxY = Buf->Height;
    }
    if (MinX < 0)
    {
        MinX = 0;
    }
    if (MaxX > Buf->Width)
    {
        MaxX = Buf->Width;
    }

    u32 Color = ((RoundF32ToU32(R * 255.0f) << 16) |
                 (RoundF32ToU32(G * 255.0f) << 8)  |
                 (RoundF32ToU32(B * 255.0f) << 0));

    u8 *Row = (u8 *)Buf->Memory + (MinY * Buf->Pitch) + (MinX * Buf->BytesPerPixel);
    for (int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for (int X = MinX;
            X < MaxX;
            ++X)
        {
            *Pixel++ = Color;
        }
        Row += Buf->Pitch;
    }
}

static void
DrawSpecialRect(game_offscreen_buffer *Buf,
                v2 Min, v2 Max,
                color RectColor, color OutlineColor)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    b32 HasOutline = ((OutlineColor.R > 0) || (OutlineColor.G > 0) || (OutlineColor.B > 0) || (OutlineColor.A > 0));

    if (MinY < 0)
    {
        MinY = 0;
    }
    if (MaxY > Buf->Height)
    {
        MaxY = Buf->Height;
    }
    if (MinX < 0)
    {
        MinX = 0;
    }
    if (MaxX > Buf->Width)
    {
        MaxX = Buf->Width;
    }

    u32 RectBlue = RoundF32ToU32(RectColor.B * 255.0f);
    u32 RectGreen = RoundF32ToU32(RectColor.G * 255.0f);
    u32 RectRed = RoundF32ToU32(RectColor.R * 255.0f);

    u32 PackedOutlineColor = 0;
    if (HasOutline)
    {
        PackedOutlineColor = ( (RoundF32ToU32(OutlineColor.R * 255.0f) << 16) |
                               (RoundF32ToU32(OutlineColor.G * 255.0f) << 8 ) | 
                               (RoundF32ToU32(OutlineColor.B * 255.0f) << 0 ) );
    }

    u8 *Row = (u8 *)Buf->Memory + (MinY * Buf->Pitch) + (MinX * Buf->BytesPerPixel);
    for (int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u8 *Pixel = Row;
        for (int X = MinX;
            X < MaxX;
            ++X)
        {
            if ( ((X == MinX) && HasOutline)   ||
                ((Y == MinY) && HasOutline)   ||
                ((X == MaxX-1) && HasOutline) ||
                ((Y == MaxY-1) && HasOutline) ) 
            {
                *(u32 *)Pixel = PackedOutlineColor;
            }
            else
            {
                // Blue
                Pixel[0] = LerpS32(Pixel[0], RectBlue, RectColor.A);

                // Green
                Pixel[1] = LerpS32(Pixel[1], RectGreen, RectColor.A);
                
                // Red
                Pixel[2] = LerpS32(Pixel[2], RectRed, RectColor.A);

            }
            Pixel += sizeof(u32);
        }
        Row += Buf->Pitch;
    }
}

static void
DEBUGDrawText(game_offscreen_buffer *Backbuf,
              f32 X, f32 Y,
              char *StringText, arena *DebugTextArena,
              color Color,
              f32 Scale = 3.5f)
{
    buffer QuadBuf = {};
    QuadBuf.Size = Kilobytes(10);
    QuadBuf.Data = PushArray(DebugTextArena, u8, QuadBuf.Size);
    int NumQuads = stb_easy_font_print(0, 0, StringText, NULL, QuadBuf.Data, QuadBuf.Size);
    for (int QuadIdx = 0;
        QuadIdx < NumQuads;
        ++QuadIdx)
    {
        quad *ThisQuad = (quad *)(QuadBuf.Data + QuadIdx * sizeof(quad));
        v2 Min = {(X + ThisQuad->TopLeft.X*Scale), (Y + ThisQuad->TopLeft.Y*Scale)};
        v2 Max = {(X + ThisQuad->BottomRight.X*Scale), (Y + ThisQuad->BottomRight.Y*Scale)};
        if (Color.A == 1.0f)
        {
            DrawSimpleRect(Backbuf, Min, Max, Color.R, Color.G, Color.B);
        }
        else
        {
            DrawSpecialRect(Backbuf, Min, Max, Color, color{0, 0, 0, 0});
        }
    }
}

static void
DEBUGPrintFps(game_offscreen_buffer *Backbuf, f32 NewFpsReading, arena *DebugTextArena)
{
#define FPS_SNAPS 30
    static f32 FpsSnaps[FPS_SNAPS] = {};
    static int FpsPrintCounter = 0;
    f32 FpsAvg = 0;
    FpsSnaps[FpsPrintCounter] = NewFpsReading;
    ++FpsPrintCounter;
    for (int SnapIdx = 0;
        SnapIdx < FPS_SNAPS;
        ++SnapIdx)
    {
        FpsAvg += FpsSnaps[SnapIdx];
    }
    FpsAvg /= FPS_SNAPS;

    if (FpsPrintCounter == FPS_SNAPS)
    {
        FpsPrintCounter = 0;
    }
    char Temp[256];
    snprintf(Temp, 256, "%d FPS", RoundF32ToS32(FpsAvg));
    DEBUGDrawText(Backbuf, (Backbuf->Width * 0.9f), 30, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
#undef FPS_SNAPS
}

static void
DEBUGPrintRowsCols(game_offscreen_buffer *Backbuf, arena *DebugTextArena, tile_map *TileMap)
{
    int OffsetFromGridX = 15;
    int OffsetFromGridY = 15;
    // 0, 0
    DEBUGDrawText(Backbuf, OffsetFromGridX, OffsetFromGridY, "0", DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});

    // Cols
    for (int Col = 1;
        Col < TileMap->NumCols;
        ++Col)
    {
        int PrintX = Col * TileMap->TileSideInPixels + OffsetFromGridX; 
        char Temp[3];
        snprintf(Temp, 3, "%d", Col);
        DEBUGDrawText(Backbuf, PrintX, OffsetFromGridY, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
    }

    // Rows
    for (int Row = 1;
        Row < TileMap->NumRows;
        ++Row)
    {
        int PrintY = Row * TileMap->TileSideInPixels + OffsetFromGridY; 
        char Temp[3];
        snprintf(Temp, 3, "%d", Row);
        DEBUGDrawText(Backbuf, OffsetFromGridX, PrintY, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
    }
}

static void
GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz)
{
    s16 ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    s16 *SampleOut = SoundBuffer->Samples;
    for (int SampleIdx = 0;
        SampleIdx < SoundBuffer->SampleCount;
        ++SampleIdx)
    {
#if 0
        f32 SineValue = sinf(GameState->tSine);
        s16 SampleValue = (s16)SineVavlue * ToneVolume;
#else
        s16 SampleValue = 0;

        // To test with square wave:
        // s16 SampleValue = ((SampleIdx / (WavePeriod/2)) %2) ? ToneVolume : -ToneVolume;
#endif
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;
#if 0
        GameState->tSine += 2.0f*Pi32*1.0f/(f32)WavePeriod;

        if (GameState->tSine > 2.0f*Pi32)
        {
            GameState->tSine -= 2.0f*Pi32;
        }
#endif
    }
}

// static void
// _ScaleAndBlitBitmap(game_offscreen_buffer *pBuf, v2 Min, v2 Max, _meta_bitmap *pToDraw)
// {
//     //  TODO(Aaron): Validate that this tolerates walking off the side of the screen
//     f32 XCoef = (f32)pBitmap->Width / (Max.X - Min.X);
//     f32 YCoef = (f32)pBitmap->Height / (Max.Y - Min.Y);
//
//     s32 ScreenMinY = RoundF32ToS32(Min.Y);
//     s32 ScreenMaxY = RoundF32ToS32(Max.Y);
//     s32 ScreenMinX = RoundF32ToS32(Min.X);
//     s32 ScreenMaxX = RoundF32ToS32(Max.X);
//
//     s32 SampledScreenMinY = ScreenMinY;
//     s32 SampledScreenMaxY = ScreenMaxY;
//     s32 SampledScreenMinX = ScreenMinX;
//     s32 SampledScreenMaxX = ScreenMaxX;
//
//     if (ScreenMinY < 0)
//     {
//         SampledScreenMinY = 0;
//     }
//     if (ScreenMinX < 0)
//     {
//         SampledScreenMinX = 0;
//     }
//     if (ScreenMaxY > pBuf->Height)
//     {
//         SampledScreenMaxY = pBuf->Height;
//     }
//     if (ScreenMaxX > pBuf->Width)
//     {
//         SampledScreenMaxX = pBuf->Width;
//     }
//
//     // Pixels are always 32 bits wide, memory order BB GG RR XX
//     u8 *DestRow = (u8 *)pBuf->Memory + SampledScreenMinY * pBuf->Pitch + SampledScreenMinX * pBuf->BytesPerPixel;
//     for (int Y = SampledScreenMinY; Y < SampledScreenMaxY; ++Y)
//     {
//         s32 SrcRowIdx = FloorF32ToS32(YCoef*((f32)(Y-ScreenMinY)+0.5f));
//         s32 SrcRow = pBitmap->Height - 1 - SrcRowIdx;
//         u8 *DestPixel = DestRow;
//         for (int X = SampledScreenMinX; X < SampledScreenMaxX; ++X)
//         {
//             s32 SrcCol = FloorF32ToS32(XCoef*((f32)(X-ScreenMinX)+0.5f));
//             u8 *SrcPixel = pBitmap->Pixels + SrcRow * pBitmap->Pitch + SrcCol * pBitmap->BytesPerPixel;
//
//             // Get the alpha value
//             f32 Alpha = ((f32)SrcPixel[3]) / 255.0f;
//
//             //Blue
//             DestPixel[0] = LerpS32(DestPixel[0], SrcPixel[0], Alpha);
//
//             // Green
//             DestPixel[1] = LerpS32(DestPixel[1], SrcPixel[1], Alpha);
//
//             // Red
//             DestPixel[2] = LerpS32(DestPixel[2], SrcPixel[2], Alpha);
//
//             DestPixel += sizeof(u32);   
//         }
//         DestRow += pBuf->Pitch;
//     }
// }

static void
ScaleAndBlitBitmap(game_offscreen_buffer *Buf, v2 Min, v2 Max, bitmap *Bitmap)
{
    if (!Bitmap->Pixels)
    {
        return;
    }
    //  TODO(Aaron): Validate that this tolerates walking off the side of the screen
    f32 XCoef = (f32)Bitmap->Width / (Max.X - Min.X);
    f32 YCoef = (f32)Bitmap->Height / (Max.Y - Min.Y);

    s32 ScreenMinY = RoundF32ToS32(Min.Y);
    s32 ScreenMaxY = RoundF32ToS32(Max.Y);
    s32 ScreenMinX = RoundF32ToS32(Min.X);
    s32 ScreenMaxX = RoundF32ToS32(Max.X);

    s32 SampledScreenMinY = ScreenMinY;
    s32 SampledScreenMaxY = ScreenMaxY;
    s32 SampledScreenMinX = ScreenMinX;
    s32 SampledScreenMaxX = ScreenMaxX;

    if (ScreenMinY < 0)
    {
        SampledScreenMinY = 0;
    }
    if (ScreenMinX < 0)
    {
        SampledScreenMinX = 0;
    }
    if (ScreenMaxY > Buf->Height)
    {
        SampledScreenMaxY = Buf->Height;
    }
    if (ScreenMaxX > Buf->Width)
    {
        SampledScreenMaxX = Buf->Width;
    }

    // Pixels are always 32 bits wide, memory order BB GG RR XX
    u8 *DestRow = (u8 *)Buf->Memory + SampledScreenMinY * Buf->Pitch + SampledScreenMinX * Buf->BytesPerPixel;
    for (int Y = SampledScreenMinY; Y < SampledScreenMaxY; ++Y)
    {
        s32 SrcRowIdx = FloorF32ToS32(YCoef*((f32)(Y-ScreenMinY)+0.5f));
        s32 SrcRow = Bitmap->Height - 1 - SrcRowIdx;
        u8 *DestPixel = DestRow;
        for (int X = SampledScreenMinX; X < SampledScreenMaxX; ++X)
        {
            s32 SrcCol = FloorF32ToS32(XCoef*((f32)(X-ScreenMinX)+0.5f));
            u8 *SrcPixel = Bitmap->Pixels + SrcRow * Bitmap->Pitch + SrcCol * Bitmap->BytesPerPixel;

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
        DestRow += Buf->Pitch;
    }
}

static void
DrawHeldTile(game_offscreen_buffer *pBackbuf, v2 MouseCoords, bitmap *pBitmap)
{
    v2 Min = MouseCoords + v2{20.0f, 20.0f};
    v2 Max = Min + v2{30.0f, 30.0f};
    ScaleAndBlitBitmap(pBackbuf, Min, Max, pBitmap);
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
    return(Count);
}

static mem_idx
SaveProject(tile_map *pTileMap, player *pPlayer, scratch_header *pScratchHeader)
{
    // If we successfully got a filepath from the OS file dialog and wrote the project to a file,
    //      this function returns the number of bytes written to that file. If we didn't have
    //      anything to write (e.g., empty project), or some failure occurred along the way,
    //      we return 0. The caller can interpret this in whichever way they choose.

    mem_idx Result = 0;

    // On Windows we call GetSaveFilenameA. It returns either nonzero or zero.
    //      The return value is NONZERO if: 
    //              - the user specifies a filepath, and 
    //              - clicks the OK button, and
    //              - the function is successful.
    //      The return value is ZERO if:
    //              - the user cancels, or
    //              - the user closes the dialog box, or
    //              - an error, such as the file name buffer being too small, occurred.
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);

    char *pSavepath = PushArray(&pScratch->Arena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(pSavepath, STRING_LEN, true);
    if (GetFilepathResult == 0) {FreeScratchArena(pScratch); return(0);}
 
    scratch_arena *pOutfileScratch = GetScratchArena(pScratchHeader);
    arena *pOutfileArena = &pOutfileScratch->Arena;
    saved_project *pHeader = PushStruct(pOutfileArena, saved_project);

// Magic number
    char *pMagicNumber = "TUFT";
    mem_idx MagicNumberSize = strnlen(pMagicNumber, STRING_LEN);
    Assert(MagicNumberSize == 4);
    for (int NthLetter = 0; NthLetter < MagicNumberSize; ++NthLetter) {
        pHeader->MagicNumber[NthLetter] = pMagicNumber[NthLetter];
    }

// Fields we can just copy    
    pHeader->NumTileRows = pTileMap->NumRows;
    pHeader->NumTileCols = pTileMap->NumCols;
    pHeader->NumTileTypes = pTileMap->TileTypes.CountNonEmptySlots();

// NumBitmaps per facing
    for (int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        pHeader->NumPlayerBitmapsPerFacing[NthFacing] = 
            pPlayer->FacingBitmaps[NthFacing].MetaBitmaps.CountNonEmptySlots();
    }

// Player DrawThis.
    pHeader->PlayerDrawThisOffset = pOutfileArena->Cursor;
    int *pPlayerDrawThisArray = PushArray(pOutfileArena, int, 4);
    for (int NthFacing = 0; NthFacing < 4; ++NthFacing) {
        pPlayerDrawThisArray[NthFacing] = 
            pPlayer->FacingBitmaps[NthFacing].DrawThis;
    }

    pHeader->PlayerFilepathsOffset = pOutfileArena->Cursor;

// Player filepaths
    for (int NthFacing = 0; NthFacing < 4; ++NthFacing) {
        arr_meta_bitmap *pMetaBitmaps = &pPlayer->FacingBitmaps[NthFacing].MetaBitmaps;
        for (int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
            meta_bitmap *pIt = pMetaBitmaps->Get(Slot);
            if (pIt->IsEmpty() == false) {
                mem_idx FilepathLen = strlen(pIt->Filepath);
                ++FilepathLen;
                char *FilepathBufferInOutfile = PushArray(pOutfileArena, char, FilepathLen);
                snprintf(FilepathBufferInOutfile, FilepathLen, "%s", pIt->Filepath);
            }
        }
    }

// Tile type filepaths
    pHeader->TileTypeFilepathsOffset = pOutfileArena->Cursor;
    arr_meta_bitmap *pTypes = &pTileMap->TileTypes;
    for (int Slot = 0; Slot < pTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTypes->Get(Slot);
        if (pIt->IsEmpty() == false) {
            mem_idx FilepathLen = strlen(pIt->Filepath);
            ++FilepathLen;
            char *FilepathBufferInOutfile = PushArray(pOutfileArena, char, FilepathLen);
            snprintf(FilepathBufferInOutfile, FilepathLen, "%s", pIt->Filepath);
        }
    }

// Tile values
    pHeader->TileValuesOffset = pOutfileArena->Cursor;
    mem_idx NumTileValuesToCopy = pHeader->NumTileRows * pHeader->NumTileCols;
    s32 *TileValuesInOutfile = PushArray(pOutfileArena, s32, NumTileValuesToCopy);
    mem_idx NumBytesToCopy = NumTileValuesToCopy * sizeof(s32);
    memcpy(TileValuesInOutfile, pTileMap->TileValues, NumBytesToCopy);

// Write file
    mem_idx NumBytesToWrite = pOutfileArena->Cursor;

    // TODO(AARON): On Windows we have updated WriteEntireFile to return the number of bytes the file wrote,
    //      so that we can return that from this function.
    mem_idx BytesWritten = WriteEntireFile(pSavepath, NumBytesToWrite, pOutfileArena->Data);
    if (BytesWritten != NumBytesToWrite) {
        Result = 0;
    }
    else {
        Result = BytesWritten;
    }

    FreeScratchArena(pOutfileScratch);
    FreeScratchArena(pScratch);
    return(Result);
}

static mem_idx
LoadProject(player *pPlayer, tile_map *pTileMap, scratch_header *pScratchHeader)
{
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);

    char *pLoadedFileFilepath = PushArray(&pScratch->Arena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(pLoadedFileFilepath, STRING_LEN, false);
    if (GetFilepathResult == 0) {FreeScratchArena(pScratch); return(0);}

    u32 LoadedFileSize = GetFileSize(pLoadedFileFilepath);
    if (LoadedFileSize == 0) {FreeScratchArena(pScratch); return(0);}

    u8 *pLoadedDataStart = PushArray(&pScratch->Arena, u8, LoadedFileSize);
    mem_idx BytesReadIntoBuffer = ReadFileInto(pLoadedFileFilepath, LoadedFileSize, pLoadedDataStart);
    if (BytesReadIntoBuffer == 0) {FreeScratchArena(pScratch); return(0);} 

    saved_project *pHeader = (saved_project *)pLoadedDataStart;

    // char MagicNumber[4];
    char *pCompareMagicNumber = "TUFT";
    for (int LetterIdx = 0; LetterIdx < 4; ++LetterIdx) {
        if (pCompareMagicNumber[LetterIdx] != pHeader->MagicNumber[LetterIdx]) {
            FreeScratchArena(pScratch); return(0);
        }
    }

    if (pHeader->NumTileRows == 0 || pHeader->NumTileCols == 0) {
        FreeScratchArena(pScratch); return(0);
    }

    pTileMap->NumRows = pHeader->NumTileRows;
    pTileMap->NumCols = pHeader->NumTileCols;
    pTileMap->NumTilesInWorld = pTileMap->NumRows * pTileMap->NumCols;

    // int NumPlayerBitmapsPerFacing[4];
    // mem_idx PlayerDrawThisOffset;
    // mem_idx PlayerFilepathsOffset;
    char *pFilepathToCopyCursor = (char *)(pLoadedDataStart + pHeader->PlayerFilepathsOffset);
    int *pPlayerDrawThisToCopy = (int *)(pLoadedDataStart + pHeader->PlayerDrawThisOffset);
    for (int NthFacing = 0; NthFacing < 4; ++NthFacing) {
        facing_bitmaps *pThisFacing = pPlayer->FacingBitmaps + NthFacing;
        arr_meta_bitmap *pMetaBitmaps = &pThisFacing->MetaBitmaps;
        pMetaBitmaps->FullClear();
        for (int NthFilepath = 0; NthFilepath < pHeader->NumPlayerBitmapsPerFacing[NthFacing]; ++NthFilepath) {
            int AddedToSlotN = pMetaBitmaps->Add(pFilepathToCopyCursor);
            Assert(AddedToSlotN);
            pFilepathToCopyCursor += strlen(pFilepathToCopyCursor) + 1;
        }
        LoadPlayerBitmapsDir(pPlayer, pThisFacing, pScratchHeader);
        pThisFacing->DrawThis = pPlayerDrawThisToCopy[NthFacing];
    }

    // Use the same cursor pointer as we used for player filepaths
    pFilepathToCopyCursor = (char *)(pLoadedDataStart + pHeader->TileTypeFilepathsOffset);
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
    pTileTypes->FullClear();
    for (int NthTileType = 0; NthTileType < pHeader->NumTileTypes; ++NthTileType) {
        int AddedToSlotN = pTileTypes->Add(pFilepathToCopyCursor);
        Assert(AddedToSlotN);
        pFilepathToCopyCursor += strlen(pFilepathToCopyCursor) + 1;
    }

    // Tile values
    mem_idx NumTileValueBytes = pTileMap->NumTilesInWorld * sizeof(s32);
    memcpy(pTileMap->TileValues, pLoadedDataStart + pHeader->TileValuesOffset, NumTileValueBytes);

    LoadTileBitmapsDir(pTileMap, pScratchHeader);

    FreeScratchArena(pScratch); 
    return(0);
}

static void
ChangeHeldPlayerMetaBitmap(player *pPlayer, cursor_state *pCursorState,
                           int NewFacingIdx, int NewSlot)
{
    if (NewFacingIdx >= 0 && NewFacingIdx < 4) {
        arr_meta_bitmap *pNewFacingBitmaps = &pPlayer->FacingBitmaps[NewFacingIdx].MetaBitmaps;
        pCursorState->HeldFacingIdx = NewFacingIdx;
        pCursorState->HeldSlot = NewSlot;
    }
}

static void
ChangeHeldTileType(tile_map *pTileMap, cursor_state *pCursorState, 
                   int NewSlot)
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
    return(pResult);
}

static meta_bitmap *
GetTileTypePointer(const tile_map *pTileMap, int Slot)
{
    meta_bitmap *pResult = pTileMap->TileTypes.Get(Slot);
    return(pResult);
}

static void
DrawFilepathTooltip(game_offscreen_buffer *pBackbuf, arena *pDebugTextArena, 
                    v2 MouseCoords, meta_bitmap *pHoveredBitmap)
{
    f32 TooltipWidth = ((f32)strlen(pHoveredBitmap->Filepath)) * 11.5f;
    v2 TooltipMin = MouseCoords + v2{20.0f, -30.0f};
    if (TooltipMin.X + TooltipWidth >= pBackbuf->Width) {
        TooltipMin.X -= TooltipWidth;
    }
    v2 TooltipMax = TooltipMin + v2{TooltipWidth, 30.0f};
    DrawSimpleRect(pBackbuf, TooltipMin, TooltipMax, 1, 1, 0.88f, 0.75f);
    DEBUGDrawText(pBackbuf, TooltipMin.X + 5.0f, TooltipMin.Y + 6.0f, 
                  pHoveredBitmap->Filepath, pDebugTextArena, color{0, 0, 0, 1}, 2.0f);
}

static void
DrawPlayerEditor(game_offscreen_buffer *pBackbuf, scratch_header *pScratchHeader, debug_state *pDebugState, player *pPlayer)
{
    editor_state *pEditorState = &pDebugState->EditorState;
    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pBackbuf->Width * 0.5f), 0};
    v2 BrowserMax = {(f32)(pBackbuf->Width), (f32)(pBackbuf->Height)};
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);
    b32 BagelActive = false;

    // Get an array of menu_tile structs accommodating the maximum number of player bitmaps the game supports.
    //      Note that this is different than the length of the actual arr_meta_bitmaps.Data array for
    //      player bitmaps because that array contains the special nil bitmap at the front
    menu_bitmap *pMenuBitmaps = PushArray(&pScratch->Arena, menu_bitmap, 4 * (MAX_FACING_BITMAPS));

    DrawSpecialRect(pBackbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{0, 0, 0, 0});

    DEBUGDrawText(pBackbuf, BrowserMin.X + 300, 60, "Player Bitmaps Menu", &pDebugState->DebugTextArena, color{1, 1, 1, 1});

    char *Dirs[] = {"East", "North", "West", "South"};
    menu_bitmap *pMenuHovered = nullptr;
    f32 VerticalSpaceBetweenSections = 220;
    v2 HeaderTextStart = {BrowserMin.X + 30, 120};
    f32 CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    menu_bitmap *pMenuBitmapsCursor = pMenuBitmaps;
    for (int NthFace = 0; NthFace < 4; ++NthFace) {
        arr_meta_bitmap *pMetaBitmaps = &pPlayer->FacingBitmaps[NthFace].MetaBitmaps;
        char Temp[STRING_LEN];
        snprintf(Temp, STRING_LEN, "Facing %s", Dirs[NthFace]);
        DEBUGDrawText(pBackbuf, HeaderTextStart.X, HeaderTextStart.Y, Temp, &pDebugState->DebugTextArena, color{0.9, 0.9, 0.9, 1}, 2.4);

        f32 XDrawCoord = HeaderTextStart.X;
        for (int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
            meta_bitmap *pIt = pMetaBitmaps->Get(Slot);

            if (pIt->IsEmpty()) { continue; }

            pMenuBitmapsCursor->FacingIdx = NthFace;
            pMenuBitmapsCursor->Slot = Slot;
            if (pPlayer->FacingBitmaps[NthFace].DrawThis == Slot) {
                pMenuBitmapsCursor->DrawThis = true;
            }

            f32 Width, Height;
            if (pIt->IsMissing()) {
                BagelActive = true;
                Width = pGlobalBagel->Bitmap.Width;
                Height = pGlobalBagel->Bitmap.Height;
            }
            else {
                Width = pIt->Bitmap.Width;
                Height = pIt->Bitmap.Height;
            }

            pMenuBitmapsCursor->TileMin = v2{(f32)XDrawCoord, CenterAroundThisVerticalLine - Height * 0.5f};
            pMenuBitmapsCursor->TileMax = pMenuBitmapsCursor->TileMin + v2{Width, Height};

            if (IsInRect(MouseCoords, pMenuBitmapsCursor->TileMin, pMenuBitmapsCursor->TileMax)) {
                pMenuHovered = pMenuBitmapsCursor;
            }

            XDrawCoord = pMenuBitmapsCursor->TileMax.X + 30.0f;
            ++pMenuBitmapsCursor;
        }
        HeaderTextStart += v2{0, VerticalSpaceBetweenSections};
        CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    }

    int NumMenuBitmaps = pMenuBitmapsCursor - pMenuBitmaps;
    pMenuBitmapsCursor = pMenuBitmaps;
    for (int NthBitmap = 0; NthBitmap < NumMenuBitmaps; ++NthBitmap, ++pMenuBitmapsCursor) {
        meta_bitmap *pToDraw = 
            GetPlayerMetaBitmapPointer(pPlayer, pMenuBitmapsCursor->FacingIdx, pMenuBitmapsCursor->Slot);
        bitmap *pBitmap;
        if (pToDraw->IsMissing()) {
            pBitmap = &pGlobalBagel->Bitmap;
        }
        else {
            pBitmap = &pToDraw->Bitmap;
        }
        ScaleAndBlitBitmap(pBackbuf, pMenuBitmapsCursor->TileMin, pMenuBitmapsCursor->TileMax, pBitmap);
        v2 OutlineMin = {pMenuBitmapsCursor->TileMin.X-1, pMenuBitmapsCursor->TileMin.Y-1};
        v2 OutlineMax = {pMenuBitmapsCursor->TileMax.X+1, pMenuBitmapsCursor->TileMax.Y+1};
        if (pMenuBitmapsCursor->DrawThis) {
            DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0.7, 0.5, 0.8});
        }
        else {
            DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0.1, 0.1, 0.1, 0.8});        
        }
    }

    cursor_state *pCursorState = &pDebugState->EditorState.CursorState;
    UpdateCursorStateHoveredFrames(pCursorState);
    // Highlight hovered tile and draw tooltip filepath text
    if (pMenuHovered != nullptr) {
        v2 OutlineMin = {pMenuHovered->TileMin.X-1, pMenuHovered->TileMin.Y-1};
        v2 OutlineMax = {pMenuHovered->TileMax.X+1, pMenuHovered->TileMax.Y+1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
        if (pCursorState->HoveredFrames >= 30) {
            meta_bitmap *pHoveredBitmap = 
                GetPlayerMetaBitmapPointer(pPlayer, pMenuHovered->FacingIdx, pMenuHovered->Slot);
            DrawFilepathTooltip(pBackbuf, &pDebugState->DebugTextArena, MouseCoords, pHoveredBitmap);
            // char *Filepath = pHoveredBitmap->Filepath;
            // f32 TooltipWidth = ((f32)strlen(Filepath)) * 11.5f;
            // v2 TooltipMin = MouseCoords + v2{20.0f, -30.0f};
            // v2 TooltipMax = TooltipMin + v2{TooltipWidth, 30.0f};
            // DrawSimpleRect(pBackbuf, TooltipMin, TooltipMax, 1, 1, 0.88f, 0.75f);
            // DEBUGDrawText(pBackbuf, TooltipMin.X + 5.0f, TooltipMin.Y + 6.0f, Filepath, &pDebugState->DebugTextArena, color{0, 0, 0, 1}, 2.0f);
        }
    }

    /*********** UPDATE CURSOR STATE **************/


    // If ended up always go to idle
    if (pGlobalMouse->Primary.EndedDown == false) {
        pCursorState->PrimaryMode = IDLE;
    }

    // If the user clicked, always go to consumed
    else if (pCursorState->PrimaryMode == IDLE &&
             pGlobalMouse->Primary.EndedDown &&
             pGlobalMouse->Primary.HalfTransitionCount == 1) {

        pCursorState->PrimaryMode = CONSUMED;

        // If the user clicked on an actual bmp
        if (pMenuHovered != nullptr) {
            if (BagelActive) {
                // If the tile they clicked on is valid, replace
                //      current held tile with clicked bmp
                meta_bitmap *pHoveredBitmap = 
                    GetPlayerMetaBitmapPointer(pPlayer, pMenuHovered->FacingIdx, pMenuHovered->Slot);
                if (pHoveredBitmap->IsPresent()) {
                    ChangeHeldPlayerMetaBitmap(pPlayer, pCursorState, pMenuHovered->FacingIdx, pMenuHovered->Slot);
                }
                // If they clicked a bagel, a bmp was missing. If they are
                //      holding a valid bmp, replace the clicked
                //      bmp with the held one, and set held bmp to nil
                if (pHoveredBitmap->IsMissing() && pCursorState->HeldFacingIdx == pMenuHovered->FacingIdx) {
                    meta_bitmap *pHeldBitmap =
                        GetPlayerMetaBitmapPointer(pPlayer, pCursorState->HeldFacingIdx, pCursorState->HeldSlot);
                        arr_meta_bitmap *pHoveredContainingArr = GetPlayerArrMetaBitmapPtr(pPlayer, pMenuHovered->FacingIdx);
                        Assert(pHoveredContainingArr != nullptr);
                        pHoveredContainingArr->ReplaceSlotAWithSlotB(pMenuHovered->Slot, pCursorState->HeldSlot);
                        SetHeldBitmapToNil(pCursorState);
                }
            }
            // Else if no bagel is active, all tiles are present, and clicking one
            //      changes the player's facing direction in game and the DrawThis
            //      index to match the clicked bmp
            else {
                pPlayer->FacingBitmaps[pMenuHovered->FacingIdx].DrawThis = pMenuHovered->Slot;
                pPlayer->IsFacing = pMenuHovered->FacingIdx;
            }
        }
    }

    if (pGlobalMouse->Secondary.EndedDown == false) {
        pCursorState->SecondaryMode = IDLE;
    }
    // Right clicking while holding a player bmp drops it
    else if (pCursorState->SecondaryMode == IDLE &&
             pGlobalMouse->Secondary.EndedDown &&
             pGlobalMouse->Secondary.HalfTransitionCount == 1) {
        SetHeldBitmapToNil(pCursorState);
    }

    // Draw held bmp
    arr_meta_bitmap *pHeldContainingArr = GetPlayerArrMetaBitmapPtr(pPlayer, pCursorState->HeldFacingIdx);
    if (pHeldContainingArr != nullptr) {
        meta_bitmap *pIt = pHeldContainingArr->Get(pCursorState->HeldSlot);
        if (pIt->IsPresent()) {
            bitmap *pBitmap = &pIt->Bitmap;
            DrawHeldTile(pBackbuf, MouseCoords, pBitmap);
        }
    }

    // Draw player to left of browser if player editor is active, selecting
    //      whichever bmp matches the DrawThis index for the current facing
    facing_bitmaps *pCurrentFacing = pPlayer->FacingBitmaps + pPlayer->IsFacing;
    arr_meta_bitmap *pArrForCurrentFacing = GetPlayerArrMetaBitmapPtr(pPlayer, pPlayer->IsFacing);
    if (pArrForCurrentFacing->CountNonEmptySlots() > 0)
    {
        meta_bitmap *pToDraw = pArrForCurrentFacing->Get(pCurrentFacing->DrawThis);
        if (pToDraw->IsPresent()) {
            bitmap *pBitmap = &pToDraw->Bitmap;
        
            f32 PlayerWidth = pBitmap->Width;
            f32 PlayerHeight = pBitmap->Height;

            v2 PlayerMin = v2{(f32)pBackbuf->Width * 0.25f - PlayerWidth * 0.5f, 
                (f32)pBackbuf->Height * 0.5f - PlayerHeight * 0.5f};
            v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};
            ScaleAndBlitBitmap(pBackbuf, PlayerMin, PlayerMax, pBitmap);
        }
    }

    FreeScratchArena(pScratch);
}

static b32
IsHeldTileTypePresent(const tile_map *pTileMap, const cursor_state *pCursorState)
{
    meta_bitmap *pHeld = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
    b32 Result = pHeld->IsPresent();
    return(Result);
}

static void
DrawTileEditor(game_offscreen_buffer *pBackbuf, scratch_header *pScratchHeader, debug_state *pDebugState, tile_map *pTileMap)
{
    editor_state *pEditorState = &pDebugState->EditorState;
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pBackbuf->Width * 0.8f), 0};
    v2 BrowserMax = {(f32)(pBackbuf->Width), (f32)(pBackbuf->Height)};
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);

    menu_bitmap *pMenuTiles = PushArray(&pScratch->Arena, menu_bitmap, MAX_TILE_TYPES);

    // Panel
    DrawSpecialRect(pBackbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{});

    DEBUGDrawText(pBackbuf, BrowserMin.X + 120, 80, "Tile Menu", &pDebugState->DebugTextArena, color{1, 1, 1, 1}, 3.5);

    /***********MAKE ARRAY OF TILES TO DRAW**************/
    f32 Y = 160;
    f32 InnerPadding = 40;
    f32 OuterPadding = 56;
    f32 StartX = BrowserMin.X + OuterPadding;
    f32 X = StartX;
    int TilesDrawnInThisRow = 0;
    menu_bitmap *pMenuTilesCursor = pMenuTiles;
    menu_bitmap *pMenuHovered = nullptr; 
    for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTileTypes->Get(Slot);
        if (pIt->IsEmpty()) { continue; }

        pMenuTilesCursor->Slot = Slot;
        f32 Width, Height;
        if (pIt->IsMissing()) {
            Width = pGlobalBagel->Bitmap.Width;
            Height = pGlobalBagel->Bitmap.Height;
        }
        else {
            Width = pIt->Bitmap.Width;
            Height = pIt->Bitmap.Height;
        }

        pMenuTilesCursor->TileMin = {X, Y};
        pMenuTilesCursor->TileMax = pMenuTilesCursor->TileMin + v2{pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};

        if (IsInRect(MouseCoords, pMenuTilesCursor->TileMin, pMenuTilesCursor->TileMax)) {
            pMenuHovered = pMenuTilesCursor;
        }

        ++TilesDrawnInThisRow;
        if (TilesDrawnInThisRow == 3)
        {
            TilesDrawnInThisRow = 0;
            X = StartX;
            Y += pTileMap->TileSideInPixels + InnerPadding;
        }
        else
        {
            X += pTileMap->TileSideInPixels + InnerPadding;
        }

        ++pMenuTilesCursor;
    }

    /*********** DRAW THE MENU TILES AND DRAW BOXES AROUND THEM **************/
    int NumToDraw = pMenuTilesCursor - pMenuTiles;
    pMenuTilesCursor = pMenuTiles;
    for (int NthMenuTile = 0; NthMenuTile < NumToDraw; ++NthMenuTile, ++pMenuTilesCursor) {
        meta_bitmap *pToDraw = GetTileTypePointer(pTileMap, pMenuTilesCursor->Slot);
        bitmap *pBitmap = pBitmap;
        if (pToDraw->IsMissing()) {
            pBitmap = &pGlobalBagel->Bitmap;
        }
        else {
            pBitmap = &pToDraw->Bitmap;
        }
        ScaleAndBlitBitmap(pBackbuf, pMenuTilesCursor->TileMin, pMenuTilesCursor->TileMax, pBitmap);
        v2 OutlineMin = {pMenuTilesCursor->TileMin.X-1, pMenuTilesCursor->TileMin.Y-1};
        v2 OutlineMax = {pMenuTilesCursor->TileMax.X+1, pMenuTilesCursor->TileMax.Y+1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{}, color{0, 0, 0, 1});
    }

    cursor_state *pCursorState = &pDebugState->EditorState.CursorState;
    UpdateCursorStateHoveredFrames(pCursorState);
    if (pMenuHovered != nullptr) {
        v2 OutlineMin = {pMenuHovered->TileMin.X-1, pMenuHovered->TileMin.Y-1};
        v2 OutlineMax = {pMenuHovered->TileMax.X+1, pMenuHovered->TileMax.Y+1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
        if (pCursorState->HoveredFrames >= 30) {
            meta_bitmap *pHoveredBitmap = GetTileTypePointer(pTileMap, pMenuHovered->Slot);
            DrawFilepathTooltip(pBackbuf, &pDebugState->DebugTextArena, MouseCoords, pHoveredBitmap);
        }
    }

    /*********** UPDATE CURSOR STATE **************/
    b32 InBackbuf = IsInRect(MouseCoords, v2{0,0}, v2{(f32)pBackbuf->Width, (f32)pBackbuf->Height});
    b32 InBrowser = IsInRect(MouseCoords, BrowserMin, BrowserMax);

    // Primary
    if (pGlobalMouse->Primary.EndedDown == false) {
        pCursorState->PrimaryMode = IDLE;
    }

    else if (pCursorState->PrimaryMode == IDLE &&
             pGlobalMouse->Primary.EndedDown &&
             pGlobalMouse->Primary.HalfTransitionCount == 1) {

        pCursorState->PrimaryMode = CONSUMED;
        // If the user clicked on a tile in the browser, pMenuHovered will be set
        if (pMenuHovered != nullptr) {
            // If they clicked the bagel, a tile was missing.
            //      If they are holding a valid tile, replace
            //      the missing tile with the held one, and
            //      set held tile to nil.
            meta_bitmap *pHoveredBitmap = GetTileTypePointer(pTileMap, pMenuHovered->Slot);
            meta_bitmap *pHeldBitmap = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
            if(pHoveredBitmap->IsMissing() && pHeldBitmap->IsPresent()) {
                    pTileMap->TileTypes.ReplaceSlotAWithSlotB(pMenuHovered->Slot, pCursorState->HeldSlot);
                    SearchAndReplaceTileValue(pTileMap, pMenuHovered->Slot, pCursorState->HeldSlot);
                    SetHeldBitmapToNil(pCursorState);
            }
            // Else if the tile they clicked on is valid, replace current held tile
            //      with clicked tile
            else if (pHoveredBitmap->IsPresent()) {
                ChangeHeldTileType(pTileMap, pCursorState, pMenuHovered->Slot);
            }
        }

        // Else if they clicked in the tilemap
        else if (InBackbuf && !InBrowser) {
            // Figure out which tile they clicked on
            v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
            int HoveredIdx = Get1DTileCoordFrom2DCoord(pTileMap, TileAsV2);
            s32 TileValue = GetTileValueFrom1DCoord(pTileMap, HoveredIdx);
            // If they clicked on an empty tile
            if (TileValue == 0) {
                meta_bitmap *pHeldTile = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
                // And they are holding a valid tile
                if (pHeldTile->IsPresent()) {
                    // Start painting, unless user is already secondary painting
                    if (pCursorState->SecondaryMode != PAINTING) {
                        pCursorState->PrimaryMode = PAINTING;
                    }
                }
            }
            // Else, they clicked on a valid tile. Pick it up.
            else {
                ChangeHeldTileType(pTileMap, pCursorState, TileValue);
            }
        }
    }

    // Secondary cursor state
    if (pGlobalMouse->Secondary.EndedDown == false) {
        pCursorState->SecondaryMode = IDLE;
    }
    if (pCursorState->SecondaryMode==IDLE &&
        pGlobalMouse->Secondary.EndedDown &&
        pGlobalMouse->Secondary.HalfTransitionCount==1)
    {
        pCursorState->SecondaryMode = CONSUMED;
        b32 InBackbuf = IsInRect(MouseCoords, v2{0,0}, v2{(f32)pBackbuf->Width, (f32)pBackbuf->Height});
        b32 InBrowser = IsInRect(MouseCoords, BrowserMin, BrowserMax);
        meta_bitmap *pHeldTileType = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
        if (pHeldTileType->IsPresent()) {
            SetHeldBitmapToNil(pCursorState);
        }
        else {
            if (InBackbuf && !InBrowser) {
                if (pCursorState->PrimaryMode != PAINTING) {
                    pCursorState->SecondaryMode = PAINTING;
                }
            }
        }
    }

    // Drawing

    if (InBackbuf == true && InBrowser == false) {
        v2 TileCoords = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        v2 TileMin = {TileCoords.X *pTileMap->TileSideInPixels, TileCoords.Y *pTileMap->TileSideInPixels};
        v2 TileMax = TileMin + v2{pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};
        v2 OutlineMin = {TileMin.X+1, TileMin.Y+1};
        v2 OutlineMax = {TileMax.X-1, TileMax.Y-1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
    }

    meta_bitmap *pHeldTile = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
    if (pHeldTile->IsPresent())
    {
        DrawHeldTile(pBackbuf, MouseCoords, &pHeldTile->Bitmap);
    }
    if (pCursorState->PrimaryMode == PAINTING && InBrowser == false && pHeldTile->IsPresent()) {
        v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        int HoveredIdx = Get1DTileCoordFrom2DCoord(pTileMap, TileAsV2);
        SetTileValue(pTileMap, HoveredIdx, pCursorState->HeldSlot);
    }
    else if (pCursorState->SecondaryMode == PAINTING && InBrowser == false) {
        v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        int HoveredIdx = Get1DTileCoordFrom2DCoord(pTileMap, TileAsV2);
        SetTileValue(pTileMap, HoveredIdx, 0);
    }

    if (pScratch) {
        FreeScratchArena(pScratch);
    }

    if (pDebugState->EditorState.PrintRowsCols) {
        DEBUGPrintRowsCols(pBackbuf, &pDebugState->DebugTextArena, pTileMap);
    }
}

// Resolution of bacbkuffer or framebuffer is 1920 x 1080

// #define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    mem_region DebugRegion;
    DebugRegion.Size = Megabytes(16);
    DebugRegion.Data = (u8 *)Memory->PermanentStorage;

    mem_region GameRegion;
    GameRegion.Size = Memory->PermanentStorageSize - DebugRegion.Size;
    GameRegion.Data = (u8 *)Memory->PermanentStorage + DebugRegion.Size;

    mem_region TransientRegion;
    TransientRegion.Data = (u8 *)Memory->TransientStorage;

    debug_state *DebugState = (debug_state *)DebugRegion.Data;
    game_state *GameState = (game_state *)GameRegion.Data;
    scratch_header *ScratchHeader = (scratch_header *)TransientRegion.Data;

    // Convenience pointers
    tile_map *pTileMap = &GameState->TileMap;
    player *pPlayer = &GameState->Player;

    GetFileSize = Memory->DEBUGPlatformGetFileSize;
    FreeFileMemory = Memory->DEBUGPlatformFreeFileMemory;
    ReadEntireFile = Memory->DEBUGPlatformReadEntireFile;
    WriteEntireFile = Memory->DEBUGPlatformWriteEntireFile;
    GetFileWriteTime = Memory->DEBUGPlatformGetFileWriteTime;
    GetDirWriteTime = Memory->DEBUGPlatformGetDirWriteTime;
    GetListOfDirContents = Memory->DEBUGPlatformGetListOfDirContents;
    ReadFileInto = Memory->DEBUGPlatformReadFileInto;
    GetFilepathFromDialog = Memory->DEBUGPlatformGetFilepathFromDialog;
    DebugOutput = Memory->DEBUGOutput;

    // Global input pointers
    GlobalKeyboardController = GetController(Input, 0);
    GlobalDevKeys = &Input->DevKeys;
    pGlobalMouse = &Input->Mouse;

    // INIT
    if (!Memory->IsInitialized)
    {
        // Random
        GameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

        // Debug
        InitializeArena(&DebugState->DebugTextArena, (DebugRegion.Data + sizeof(debug_state)), Megabytes(1));
        InitializeArena(&DebugState->FailBitmapsArena, (DebugRegion.Data + sizeof(debug_state) + DebugState->DebugTextArena.Size), Megabytes(1));

        mem_idx GameRegionOffset = sizeof(game_state);

        // Tiles
        InitializeArena(&GameState->TileMap.TileTypes.BitmapsArena, GameRegion.Data + GameRegionOffset, Megabytes(4));
        GameRegionOffset += GameState->TileMap.TileTypes.BitmapsArena.Size;

        // pPlayer
        for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
            arena *pArena = &pPlayer->FacingBitmaps[FacingIdx].MetaBitmaps.BitmapsArena;
            InitializeArena(pArena, GameRegion.Data + GameRegionOffset, Megabytes(1));
            GameRegionOffset += Megabytes(1);
        }

        // World
        arena *pWorldArena = &GameState->WorldArena;
        InitializeArena(pWorldArena, GameRegion.Data + GameRegionOffset, (GameRegion.Size - GameRegionOffset));

        mem_idx GameRegionMemoryUsed = sizeof(game_state) + 
                                        GameState->TileMap.TileTypes.BitmapsArena.Size + 
                                        (pPlayer->FacingBitmaps[0].MetaBitmaps.BitmapsArena.Size * 4) + 
                                        GameState->WorldArena.Size;
        Assert(GameRegion.Size == GameRegionMemoryUsed);

        // Scratch
        ScratchHeader->Count = NUM_SCRATCHES;
        scratch_arena *ScratchArenas = ScratchHeader->ScratchArenas;
        for (int ScratchIdx = 0;
            ScratchIdx < ScratchHeader->Count;
            ++ScratchIdx)
        {
            scratch_arena *It = ScratchArenas + ScratchIdx;
            It->IsFree = true;
            InitializeArena(&It->Arena, 
                            (u8 *)Memory->TransientStorage + sizeof(scratch_header) + (ScratchIdx * SCRATCH_SIZE), 
                            SCRATCH_SIZE);
        }

#if 0
struct tile_map
{
    arena TilesArena;
    wait_state WaitState;
    int WaitedFrames;
    u64 LastUpdateTime;
    int NumRows;
    int NumCols;
    int NumTileTypes;
    arr_meta_bitmap TileTypes;
    f32 TileSideInPixels;
    s32 *TileValues;
};
#endif
// Tiles        
        // TileMap dimensions
        pTileMap->TileSideInPixels = 64.0f;
        pTileMap->NumRows = CeilingF32ToS32((f32)Buffer->Height / pTileMap->TileSideInPixels);
        pTileMap->NumCols = CeilingF32ToS32((f32)Buffer->Width / pTileMap->TileSideInPixels);
        pTileMap->NumTilesInWorld = pTileMap->NumRows * pTileMap->NumCols;

        arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
        pTileTypes->Data = PushArray(pWorldArena, meta_bitmap, MAX_TILE_TYPES+1);
        pTileTypes->Len = MAX_TILE_TYPES+1;
        pTileTypes->NextEmptySlot = 1;
        LoadTileBitmapsDir(pTileMap, ScratchHeader);
        pTileMap->TileValues = PushArray(pWorldArena, s32, pTileMap->NumTilesInWorld); 

// pPlayer
        for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
            facing_bitmaps *pFacingBitmaps = pPlayer->FacingBitmaps + FacingIdx;
            arr_meta_bitmap *pMetaBitmaps = &pFacingBitmaps->MetaBitmaps;
            pMetaBitmaps->Data = PushArray(&GameState->WorldArena, meta_bitmap, MAX_FACING_BITMAPS+1);
            pMetaBitmaps->Len = MAX_FACING_BITMAPS+1;
            pMetaBitmaps->NextEmptySlot = 1;
            LoadPlayerBitmapsDir(pPlayer, pFacingBitmaps, ScratchHeader);
            if(pMetaBitmaps->Data[1].IsPresent()) {
                pFacingBitmaps->DrawThis = 1;
            }
        }
        pPlayer->Position.X = Buffer->Width / 2;
        pPlayer->Position.Y = Buffer->Height / 2;
        pPlayer->IsFacing = EAST;

// Bagel
        // TODO(AARON): ScaleAndBlitBitmap called with Bitmap == nullptr draw something other than bagel
        //      so we know when we called it with nullptr

        char *BagelFilepath = "bagel1.bmp";
        pGlobalBagel = PushStruct(&DebugState->FailBitmapsArena, meta_bitmap);
        snprintf(pGlobalBagel->Filepath, sizeof(pGlobalBagel->Filepath), "%s", BagelFilepath);
        mem_idx BagelSize = GetFileSize(pGlobalBagel->Filepath);
        if (BagelSize) {
            pGlobalBagel->Bitmap.Buffer.Size = BagelSize;
            pGlobalBagel->Bitmap.Buffer.Data = PushArray(&DebugState->FailBitmapsArena, u8, BagelSize);
        }
        ActuallyLoadBitmap(pGlobalBagel);
        
        Memory->IsInitialized = true;
    }
    
    ///////////////////////////////////////// INIT END, MAIN LOOP START ////////////////////////////////////////////

    // Reset the debug text arena. This has nothing to do with the mouse cursor.
    DebugState->DebugTextArena.Cursor = 0;

    // Bitmap reloading notes:
    //      We have the following objectives:
    //          1. Be able to immediately add bitmaps to the game
    //          2. Be able to immediately remove bitmaps from the game
    //          3. Be able to make a change to a bitmap and have that change appear immediately in the game.
    //
    //      In each case, the simplest way to avoid bugs that result from trying to read a file while
    //          it's still being updated by the OS is to simply wait one frame after a change has been detected.
    //          Therefore, we store the write times for dirs and files of interest locally, and, once per frame,
    //          we compare these with the OS's write times. If the times match, no update has been made.
    //          If the times differ, we store the new time and set a ReadyToReload variable to true.
    //          Then we reload on the next frame.
    //

    // If we set ReadyToReload on the last frame, reload the tiles dir and set ReadyToReload back to false
    if (pTileMap->WaitState == READY_TO_RELOAD) {
        LoadTileBitmapsDir(pTileMap, ScratchHeader);
    }
    else if (pTileMap->WaitState == WAITING) {
        ++pTileMap->WaitedFrames;
        if (pTileMap->WaitedFrames >= 3) {
            pTileMap->WaitedFrames = 0;
            pTileMap->WaitState = READY_TO_RELOAD;
        }
    }
    else {
        // Check dir write time and compare
        u64 CheckUpdateTime = GetDirWriteTime("tiles");
        if (pTileMap->LastUpdateTime != CheckUpdateTime) {
            pTileMap->WaitState = WAITING;
            pTileMap->WaitedFrames = 0;
        }
        else {
            // If we're not planning to reload all bitmaps on the next frame, check each bitmap
            //      individually to see if that bitmap has been updated.
            arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
            for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
                ReloadBitmapIfChanged(pTileTypes, Slot);
            }

        }
    }

    for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
        facing_bitmaps *pThisFacing = &pPlayer->FacingBitmaps[FacingIdx];
        if (pThisFacing->WaitState == READY_TO_RELOAD)
        {
            LoadPlayerBitmapsDir(pPlayer, pThisFacing, ScratchHeader);
        }
        else if (pThisFacing->WaitState == WAITING) {
            ++pThisFacing->WaitedFrames;
            if (pThisFacing->WaitedFrames >= 3) {
                pThisFacing->WaitState = READY_TO_RELOAD;
                pThisFacing->WaitedFrames = 0;
            }
        }
        else {
            char Temp[STRING_LEN];
            WriteFacingBitmapsDirpathToBuffer(pPlayer, pThisFacing, Temp);
            u64 CheckUpdateTime = GetDirWriteTime(Temp);
            if (pThisFacing->LastUpdateTime != CheckUpdateTime) {
                pThisFacing->WaitState = WAITING;
                pThisFacing->WaitedFrames = 0;
            }
            // Check if each individual bitmap needs to be reloaded
            else {
                arr_meta_bitmap *pMetaBitmaps = &pThisFacing->MetaBitmaps;
                for(int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
                    ReloadBitmapIfChanged(pMetaBitmaps, Slot);
                }
            }
        }
    }

    // Controller
    for (int ControllerIdx = 0;
        ControllerIdx < ArrayCount(Input->Controllers);
        ++ControllerIdx)
    {
        game_controller_input *Controller = GetController(Input, ControllerIdx);
        if (Controller->IsAnalog)
        {
        }
        else
        {
            v2 dPlayer = {};
            if (Controller->MoveRight.EndedDown)
            {
                dPlayer.X += 1.0f;
                pPlayer->IsFacing = EAST;
            }
            if (Controller->MoveUp.EndedDown)
            {
                dPlayer.Y -= 1.0f;
                pPlayer->IsFacing = NORTH;
            }
            if (Controller->MoveLeft.EndedDown)
            {
                dPlayer.X -= 1.0f;
                pPlayer->IsFacing = WEST;
            }
            if (Controller->MoveDown.EndedDown && !Input->DevKeys.Ctrl.EndedDown)
            {
                // NOTE(AARON): The above check for the ctrl key may be incorrect but
                //      we won't know until we implement player movement again
                dPlayer.Y += 1.0f;
                pPlayer->IsFacing = SOUTH;
            }
            if ((dPlayer.X != 0) && (dPlayer.Y != 0))
            {
                dPlayer.X *= 0.707106781187f;
                dPlayer.Y *= 0.707106781187f;
            }
            f32 PlayerSpeed = 600.0f;
            
            if (DebugState->EditorState.WhichEditor == NO_EDITOR)
            {
                v2 NewPlayerP = pPlayer->Position;
                NewPlayerP.X += dPlayer.X * PlayerSpeed * Input->dtForFrame;
                NewPlayerP.Y += dPlayer.Y * PlayerSpeed * Input->dtForFrame;
                pPlayer->Position = NewPlayerP;
            }
        }
    }

    if (GlobalDevKeys->F1.EndedDown && GlobalDevKeys->F1.HalfTransitionCount == 1)
    {
        // Zero cursor state before changing editor
        InitializeCursorState(&DebugState->EditorState.CursorState);
        if (DebugState->EditorState.WhichEditor == TILE)
        {
            DebugState->EditorState.WhichEditor = NO_EDITOR;
        }
        else
        {
            DebugState->EditorState.WhichEditor = TILE;
        }
    }
    if (GlobalDevKeys->F6.EndedDown && GlobalDevKeys->F6.HalfTransitionCount == 1)
    {
        InitializeCursorState(&DebugState->EditorState.CursorState);
        if (DebugState->EditorState.WhichEditor == PLAYER)
        {
            DebugState->EditorState.WhichEditor = NO_EDITOR;
        }
        else
        {
            DebugState->EditorState.WhichEditor = PLAYER;
        }
    }

    // Underlayer
    v2 ScreenMin = {0, 0};
    v2 ScreenMax = {(f32)Buffer->Width, (f32)Buffer->Height};
    DrawSimpleRect(Buffer, ScreenMin, ScreenMax, 0.5f, 0.55f, 0.6f);

    // RENDER

    // Tiles
    for (int Row = 0; Row < pTileMap->NumRows; ++Row) {
        for (int Col = 0; Col < pTileMap->NumCols; ++Col) {
            int OneDimensionalIdx = Get1DTileCoordFrom2DCoord(pTileMap, v2{(f32)Col, (f32)Row});
            s32 TileValue = GetTileValueFrom1DCoord(pTileMap, OneDimensionalIdx);
            v2 TileMin = {Col * pTileMap->TileSideInPixels, Row * pTileMap->TileSideInPixels};
            v2 TileMax = TileMin + (v2){pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};

            if (TileValue > 0) {
                meta_bitmap *pToDraw = pTileMap->TileTypes.Get(TileValue);
                bitmap *pBitmap = &pToDraw->Bitmap;
                // For now bagel is 64x64 so we don't need to adjust the size if we're going to draw it,
                //      but if we change the tile size from 64x64 then we will
                if (pToDraw->IsPresent() == false) {
                    pBitmap = &pGlobalBagel->Bitmap;
                }
                ScaleAndBlitBitmap(Buffer, TileMin, TileMax, pBitmap);
            }
            if (DebugState->EditorState.WhichEditor == TILE)
            {
                DrawSpecialRect(Buffer, TileMin, TileMax, color{}, color{0.1, 0.1, 0.1, 1});
            }
        }
    }

    if (DebugState->EditorState.WhichEditor==TILE)
    {
        DrawTileEditor(Buffer, ScratchHeader, DebugState, pTileMap);
    }
    else if (DebugState->EditorState.WhichEditor==PLAYER)
    {
        DrawPlayerEditor(Buffer, ScratchHeader, DebugState, &GameState->Player);
    }
    else
    {
        // Player
        facing_bitmaps *CurrentFacing = &pPlayer->FacingBitmaps[pPlayer->IsFacing];
        meta_bitmap *pToDraw = CurrentFacing->MetaBitmaps.Get(CurrentFacing->DrawThis);
        if (!pToDraw->IsPresent()) {
            pToDraw = pGlobalBagel;
        }
        f32 PlayerWidth = pToDraw->Bitmap.Width;
        f32 PlayerHeight = pToDraw->Bitmap.Height;
        v2 PlayerMin = {pPlayer->Position.X - PlayerWidth * 0.5f, 
                        pPlayer->Position.Y - PlayerHeight};
        v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};

        // TODO(Aaron): doing it once per frame is bound to be very slow, and it seems like i can detect slightly jittery animation
        //      in the game when moving character around. test this.
        ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, &pToDraw->Bitmap);
    }

    // I think we don't need to check half transition count for the keys with these save/load commands because subsequent EndedDown
    //      messages are routed to the message loop for the save/load dialog, not our usual
    //      message loop in the platform layer, and we have code in the platform layer to
    //      ensure that these are ignored (AS, 9/22)
    //
    // Note MoveDown is the "S" key
    if (GlobalKeyboardController->MoveDown.EndedDown && GlobalDevKeys->Ctrl.EndedDown) {
        SaveProject(pTileMap, pPlayer, ScratchHeader);
    }

    // and ActionRight is the "L" key
    if (GlobalKeyboardController->ActionRight.EndedDown && GlobalDevKeys->Ctrl.EndedDown) {
        LoadProject(pPlayer, pTileMap, ScratchHeader);
    }

    if (GlobalDevKeys->F2.EndedDown && GlobalDevKeys->F2.HalfTransitionCount == 1) {
        DebugState->EditorState.PrintRowsCols = !DebugState->EditorState.PrintRowsCols;
    }

    DEBUGPrintFps(Buffer, Input->Fps, &DebugState->DebugTextArena);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

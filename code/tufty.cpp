#include "tufty.h"
#include "stb_easy_font.h"

#define SCRATCH_SIZE Megabytes(1)

// DEBUG_PLATFORM_GET_FILE_SIZE(name) u32 name(char *Filename)
// DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory)
// DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename)
// DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(char *Filename, u32 MemorySize, void *Memory)
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

    // Platform services
    static debug_platform_get_file_size *GetFileSize;
    static debug_platform_free_file_memory *FreeFileMemory;
    static debug_platform_read_entire_file *ReadEntireFile;
    static debug_platform_write_entire_file *WriteEntireFile;
    static debug_platform_get_file_write_time *GetFileWriteTime;
    static debug_platform_get_dir_write_time *GetDirWriteTime;
    static debug_platform_get_list_of_dir_contents *GetListOfDirContents;
    static debug_platform_read_file_into *ReadFileInto;
    static debug_platform_get_file_path_from_dialog *GetFilepathFromDialog;

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
    for(int Idx = 0;
        Idx < ScratchHeader->Count;
        ++Idx)
    {
        scratch_arena *It = (scratch_arena *)ScratchHeader->ScratchArenas + Idx;
        if(It->IsFree)
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
    if(Scratch)
    {
        Scratch->IsFree = true;
    }
}

static void
WriteFacingBitmapsDirToBuffer(int FacingIdx, char *Buffer)
{
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
    for(int NthTile = 0;
        NthTile < pTileMap->NumTilesInWorld;
        ++NthTile)
    {
        s32 *ThisTileValue = pTileMap->TileValues + NthTile;
        if(*ThisTileValue == GetsReplaced)
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

static meta_bitmap *
GetMetaBitmapPtrFromTileValue(tile_map *pTileMap, s32 TileValue)
{
    // If TileValue is 0, this just returns a pointer to the always-empty 0
    //      value at the head of the TileTypes array
    meta_bitmap *Result = pTileMap->TileTypes + TileValue;
    return(Result);
}

static b32
IsTheZeroTile(tile_map *pTileMap, meta_bitmap *pMetaBitmap)
{
    // if pMetaBitmap is pointing at the first tile type which is always empty
    b32 Result = pMetaBitmap==pTileMap->TileTypes;
    return(Result);
}

static int
GetTileValueIdxFromMetaBitmapPtr(tile_map *pTileMap, meta_bitmap *pMetaBitmap)
{
    mem_idx RelativeIdx = pMetaBitmap - pTileMap->TileTypes;
    if(RelativeIdx <= MAX_TILE_TYPES)
    {
        return((int)RelativeIdx);
    }
    else
    {
        return(0);
    }
}


static void
ReplaceBagel(meta_bitmap *pMetaBitmapsStart, mem_idx GetsReplacedSlot,
             mem_idx ReplaceWithSlot)
{
    // Note: For now, since we do not have a single struct type that we can pass in for
    //      either facing bitmaps or tile maps, the responsibility of
    //      decrementing the NumBitmaps/NumTiles/whatever value for the struct
    //      which contains pMetaBitmapsStart belongs to the caller. The caller
    //      MUST decrement this value after ReplaceBagel returns, or the game
    //      will display the wrong number of tiles.
    //
    //      I think this indicates that we should aim to move toward some sort of
    //      a linked list for storing lists of bitmaps, and in the cases where
    //      we need to know the length of a bitmap list, compute it on the fly.
    //      Or we have a generic array datatype containing a count field, and 
    //      have functions like ArrayAdd, ArrayRemove etc., and adding and 
    //      removing always increment/decrement the count. 
    //
    //      One of the two, bc currently we invite a lot of data
    //      redundancy errors.
    //
    //      AS, 9.29.26
    pMetaBitmapsStart[GetsReplacedSlot] = pMetaBitmapsStart[ReplaceWithSlot];
    pMetaBitmapsStart[ReplaceWithSlot] = {};
}


static void
ParseBitmapHeader(bitmap *Bitmap)
{
    bitmap_header *Header = (bitmap_header *)Bitmap->Buffer.Data;
    Bitmap->Pixels = Bitmap->Buffer.Data + Header->DataOffset;
    Bitmap->Height = Header->Height;
    Bitmap->Width = Header->Width;
    Assert(Header->BitsPerPixel == 32);
    Bitmap->BytesPerPixel = Header->BitsPerPixel / 8;
    Bitmap->Pitch = Bitmap->Width * Bitmap->BytesPerPixel;
}

static mem_idx
DEBUGReloadBitmapIfChanged(meta_bitmap *MetaBitmap)
{
    // Wait one frame after bitmap change detected so we don't try to load it
    //      while the save is in progress.
    bitmap *Bitmap = &MetaBitmap->Bitmap;
    char *Filepath = MetaBitmap->Filepath;
    if(MetaBitmap->ReadyToReload == true)
    {
        MetaBitmap->ReadyToReload = false;
        // Load the bitmap and check to see if it has the same dimensions. If so, just replace the pixels.
        
        // If not, the bitmap the user reloaded changed size; and we need to reload the entire dir, so return the size of the 
        // bitmap to load so the caller can decide what to do.
        // because e.g., if the reloaded bitmap is bigger than the previous version, it might not
        // fit in the space in the arena we have set aside for it.

        // In the future we could just say that a bitmap has a max size and always allocate that any bytes;
        //      this would allow us to use the same pointer for any single reloaded bitmap, no matter
        //      what the reloaded size is, and keep the other bitmaps where they are.
        mem_idx NewBitmapSize = GetFileSize(Filepath);
        if(NewBitmapSize == Bitmap->Buffer.Size)
        {
            mem_idx NumBytesLoaded = ReadFileInto(Filepath, (u32)Bitmap->Buffer.Size, Bitmap->Buffer.Data);
            ParseBitmapHeader(Bitmap);
            MetaBitmap->LastUpdateTime = GetFileWriteTime(Filepath);
            return(NumBytesLoaded);
        }
        else
        {
            return(NewBitmapSize);
        }
    }
    else
    {
        u64 WriteTime = GetFileWriteTime(Filepath);
        if(WriteTime && (WriteTime != MetaBitmap->LastUpdateTime))
        {
            MetaBitmap->ReadyToReload = true;
        }
        return(Bitmap->Buffer.Size);
    }
}

static void
ClearMetaBitmapsButLeaveFilepaths(meta_bitmap *pMetaBitmaps,
                                       mem_idx ArrayLen)
{
    for(int Slot = 0;
        Slot < ArrayLen;
        ++Slot)
    {
        pMetaBitmaps[Slot].LastUpdateTime = 0;
        pMetaBitmaps[Slot].ReadyToReload = false;
        pMetaBitmaps[Slot].Bitmap = {};
    }
}

static void
LoadBitmap(arena *Arena, meta_bitmap *MetaBitmap)
{
    bitmap *Bitmap = &MetaBitmap->Bitmap;
    char *Filepath = MetaBitmap->Filepath;
    // Not being able to get the file size is the sentinel for a filepath
    //      we have stored not matching an actual file in the dir. In this case,
    //      draw the bagel instead.
    Bitmap->Buffer.Size = GetFileSize(Filepath);
    if(Bitmap->Buffer.Size == 0)
    {
        MetaBitmap->Bitmap = pGlobalBagel->Bitmap;
        return;
    }
    Bitmap->Buffer.Data = PushArray(Arena, u8, Bitmap->Buffer.Size);
    MetaBitmap->ReadyToReload = true;
    DEBUGReloadBitmapIfChanged(MetaBitmap);
}

found_filepath *
MakeOsFilepathsList(arena *pScratchArena, buffer OsFilenames, int NumFilesFound, char *Dirname)
{
    // difference between "filenames", which are relative to the dir we passed to the OS, and "filepath",
    //      which is the path we constructed by appending the filename the OS gave us onto the end of a path,
    //      e.g., "tiles/flower.bmp": tiles is the dirname, flower is the filename, tiles/flower.bmp is a filepath

    found_filepath *pFoundFilepaths = PushArray(pScratchArena, found_filepath, NumFilesFound);
    char *pOsFilenamesCursor = (char *)OsFilenames.Data;
    found_filepath *pFoundFilepathsCursor = pFoundFilepaths;
    for(int NthOsFilename = 0;
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
UpdateBitmapList(arena *pArena, meta_bitmap *pMetaBitmapsList, 
                 int BitmapArrayLen, found_filepath *pOsFilepaths,
                 int NumOsFilepaths)
{
    // This function compares the filepaths we have in memory with the new filepaths 
    //      passed back by the OS (hereafter "OsFilepaths"). Any OsFilepaths
    //      that are already in memory are marked LoadedYet = true. Any
    //      that aren't in memory yet are appended on to the end of the list.
    int NumLoaded = 0;
    for(int Slot = 1;
        Slot <= BitmapArrayLen; // The full len of the array we allocate to store bitmaps, e.g.,
                                //      10 in the case of player facing bitmaps, or
                                //      200 in the case of tile bitmaps
        ++Slot)
    {
        meta_bitmap *It = pMetaBitmapsList + Slot;
        if(strlen(It->Filepath) > 0) 
        {
            for(int NthFoundFilepath = 0;
                NthFoundFilepath < NumOsFilepaths;
                ++NthFoundFilepath)
            {
                found_filepath *pCompareWith = pOsFilepaths + NthFoundFilepath;
                if(strncmp(It->Filepath, pCompareWith->Filepath, STRING_LEN) == 0)
                {
                    pCompareWith->LoadedYet = true;
                    break;
                }
            }
        }
    }

    for(int NthFoundFilepath = 0;
        NthFoundFilepath < NumOsFilepaths;
        ++NthFoundFilepath)
    {
        found_filepath *pCheckIfLoaded = pOsFilepaths + NthFoundFilepath;
        if(pCheckIfLoaded->LoadedYet == false) // If an OsFilepath
        {
            for(int Slot = 1;
                Slot <= BitmapArrayLen;
                ++Slot)
            {
                meta_bitmap *pThisMetaBitmap = pMetaBitmapsList + Slot;
                if(strlen(pThisMetaBitmap->Filepath) == 0)
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
    arena *pTilesArena = &pTileMap->TilesArena;
    ResetArena(pTilesArena);
    meta_bitmap *pTileTypes = pTileMap->TileTypes;
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);
    
    // Clear bitmap structs in tile types array
    ClearMetaBitmapsButLeaveFilepaths(pTileTypes, TILE_TYPES_ARRAY_LEN);

    pTileMap->LastUpdateTime = GetDirWriteTime("tiles");
    pTileMap->ReadyToReload = false;
    pTileMap->NumTileTypes = 0;

    buffer OsFilenames;
    OsFilenames.Size = MAX_TILE_TYPES * STRING_LEN;
    OsFilenames.Data = PushArray(&pScratch->Arena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, "tiles", &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if((NumFilesFound > MAX_TILE_TYPES) || (NumFilesFound == 0))
    {
        FreeScratchArena(pScratch);
        return;
    }

    // TODO(AARON): ARRAY TYPE
    found_filepath *pOsFilepaths = MakeOsFilepathsList(&pScratch->Arena, OsFilenames, NumFilesFound, "tiles");
    UpdateBitmapList(&pScratch->Arena, pTileTypes, TILE_TYPES_ARRAY_LEN, pOsFilepaths, NumFilesFound);

    for(int Slot = 1;
        Slot < TILE_TYPES_ARRAY_LEN;
        ++Slot)
    {
        meta_bitmap *pTileType = pTileMap->TileTypes + Slot;
        if(strlen(pTileType->Filepath) > 0)
        {
            LoadBitmap(pTilesArena, pTileType);
            ++pTileMap->NumTileTypes;
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
    arena *pFacingArena = &pFacingBitmaps->Arena;
    ResetArena(pFacingArena);
    meta_bitmap *pMetaBitmaps = pFacingBitmaps->MetaBitmaps;
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);

    // Clear bitmap structs in meta bitmaps 
    ClearMetaBitmapsButLeaveFilepaths(pMetaBitmaps, MAX_FACING_BITMAPS);
    

    char FacingDirpath[STRING_LEN];
    mem_idx ThisFacing = pFacingBitmaps - pPlayer->AllBitmaps.Array;
    WriteFacingBitmapsDirToBuffer((int)ThisFacing, FacingDirpath);
    pFacingBitmaps->LastUpdateTime = GetDirWriteTime(FacingDirpath);
    pFacingBitmaps->ReadyToReload = false;
    pFacingBitmaps->NumBitmaps = 0;

    buffer OsFilenames;
    OsFilenames.Size = MAX_FACING_BITMAPS * STRING_LEN;
    OsFilenames.Data = PushArray(&pScratch->Arena, u8, OsFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&OsFilenames, FacingDirpath, &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if((NumFilesFound > MAX_TILE_TYPES) || (NumFilesFound == 0))
    {
        FreeScratchArena(pScratch);
        return;
    }

    // TODO(AARON): ARRAY TYPE
    found_filepath *pOsFilepaths = MakeOsFilepathsList(&pScratch->Arena, OsFilenames, NumFilesFound, FacingDirpath);
    UpdateBitmapList(&pScratch->Arena, pMetaBitmaps, MAX_FACING_BITMAPS, pOsFilepaths, NumFilesFound);

    for(int Slot = 1;
        Slot < MAX_FACING_BITMAPS;
        ++Slot)
    {
        meta_bitmap *pMetaBitmap = pMetaBitmaps + Slot;
        if(strlen(pMetaBitmap->Filepath) > 0)
        {
            LoadBitmap(pFacingArena, pMetaBitmap);
            ++pFacingBitmaps->NumBitmaps;
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

    if(MinY < 0)
    {
        MinY = 0;
    }
    if(MaxY > Buf->Height)
    {
        MaxY = Buf->Height;
    }
    if(MinX < 0)
    {
        MinX = 0;
    }
    if(MaxX > Buf->Width)
    {
        MaxX = Buf->Width;
    }

    u32 Color = ((RoundF32ToU32(R * 255.0f) << 16) |
                 (RoundF32ToU32(G * 255.0f) << 8)  |
                 (RoundF32ToU32(B * 255.0f) << 0));

    u8 *Row = (u8 *)Buf->Memory + (MinY * Buf->Pitch) + (MinX * Buf->BytesPerPixel);
    for(int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for(int X = MinX;
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

    if(MinY < 0)
    {
        MinY = 0;
    }
    if(MaxY > Buf->Height)
    {
        MaxY = Buf->Height;
    }
    if(MinX < 0)
    {
        MinX = 0;
    }
    if(MaxX > Buf->Width)
    {
        MaxX = Buf->Width;
    }

    u32 RectBlue = RoundF32ToU32(RectColor.B * 255.0f);
    u32 RectGreen = RoundF32ToU32(RectColor.G * 255.0f);
    u32 RectRed = RoundF32ToU32(RectColor.R * 255.0f);

    u32 PackedOutlineColor = 0;
    if(HasOutline)
    {
        PackedOutlineColor = ( (RoundF32ToU32(OutlineColor.R * 255.0f) << 16) |
                               (RoundF32ToU32(OutlineColor.G * 255.0f) << 8 ) | 
                               (RoundF32ToU32(OutlineColor.B * 255.0f) << 0 ) );
    }

    u8 *Row = (u8 *)Buf->Memory + (MinY * Buf->Pitch) + (MinX * Buf->BytesPerPixel);
    for(int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u8 *Pixel = Row;
        for(int X = MinX;
            X < MaxX;
            ++X)
        {
            if( ((X == MinX) && HasOutline)   ||
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
    for(int QuadIdx = 0;
        QuadIdx < NumQuads;
        ++QuadIdx)
    {
        quad *ThisQuad = (quad *)(QuadBuf.Data + QuadIdx * sizeof(quad));
        v2 Min = {(X + ThisQuad->TopLeft.X*Scale), (Y + ThisQuad->TopLeft.Y*Scale)};
        v2 Max = {(X + ThisQuad->BottomRight.X*Scale), (Y + ThisQuad->BottomRight.Y*Scale)};
        if(Color.A == 1.0f)
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
    for(int SnapIdx = 0;
        SnapIdx < FPS_SNAPS;
        ++SnapIdx)
    {
        FpsAvg += FpsSnaps[SnapIdx];
    }
    FpsAvg /= FPS_SNAPS;

    if(FpsPrintCounter == FPS_SNAPS)
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
    for(int Col = 1;
        Col < TileMap->NumCols;
        ++Col)
    {
        int PrintX = Col * TileMap->TileSideInPixels + OffsetFromGridX; 
        char Temp[3];
        snprintf(Temp, 3, "%d", Col);
        DEBUGDrawText(Backbuf, PrintX, OffsetFromGridY, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
    }

    // Rows
    for(int Row = 1;
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
    for(int SampleIdx = 0;
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

        if(GameState->tSine > 2.0f*Pi32)
        {
            GameState->tSine -= 2.0f*Pi32;
        }
#endif
    }
}

static void
ScaleAndBlitBitmap(game_offscreen_buffer *Buf, v2 Min, v2 Max, bitmap *Bitmap)
{
    if(!Bitmap->Pixels)
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

    if(ScreenMinY < 0)
    {
        SampledScreenMinY = 0;
    }
    if(ScreenMinX < 0)
    {
        SampledScreenMinX = 0;
    }
    if(ScreenMaxY > Buf->Height)
    {
        SampledScreenMaxY = Buf->Height;
    }
    if(ScreenMaxX > Buf->Width)
    {
        SampledScreenMaxX = Buf->Width;
    }

    // Pixels are always 32 bits wide, memory order BB GG RR XX
    u8 *DestRow = (u8 *)Buf->Memory + SampledScreenMinY * Buf->Pitch + SampledScreenMinX * Buf->BytesPerPixel;
    for(int Y = SampledScreenMinY; Y < SampledScreenMaxY; ++Y)
    {
        s32 SrcRowIdx = FloorF32ToS32(YCoef*((f32)(Y-ScreenMinY)+0.5f));
        s32 SrcRow = Bitmap->Height - 1 - SrcRowIdx;
        u8 *DestPixel = DestRow;
        for(int X = SampledScreenMinX; X < SampledScreenMaxX; ++X)
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
DrawTinyMenuTile(game_offscreen_buffer *pBackbuf, v2 TinyMin, bitmap *pBitmap)
{
    v2 TinyMax = TinyMin + v2{30.0f, 30.0f};
    ScaleAndBlitBitmap(pBackbuf, TinyMin, TinyMax, pBitmap);
    v2 OutlineMin = TinyMin + v2{-1.0f, -1.0f};
    v2 OutlineMax = TinyMax + v2{1.0f, 1.0f};
    DrawSpecialRect(pBackbuf, TinyMin, TinyMax, color{}, color{0, 0, 0, 1});
}

static int
CountBitmaps(meta_bitmap *pMetaBitmapsList, mem_idx ListLen)
{
    int Count = 0;
    for(int Slot = 1;
        Slot <= ListLen;
        ++Slot)
    {
        if(strlen(pMetaBitmapsList[Slot].Filepath) > 0)
        {
            ++Count;
        }
    }
    return(Count);
}

static mem_idx
SaveProject(tile_map *TileMap, player *Player, scratch_header *ScratchHeader)
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
    scratch_arena *pScratch = GetScratchArena(ScratchHeader);

    char *Savepath = PushArray(&pScratch->Arena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(Savepath, STRING_LEN, true);
    if(GetFilepathResult == 0) {FreeScratchArena(pScratch); return(0);}
 
    scratch_arena *OutfileScratch = GetScratchArena(ScratchHeader);
    arena *OutfileArena = &OutfileScratch->Arena;
    saved_project *Header = PushStruct(OutfileArena, saved_project);

// Magic number
    char *MagicNumber = "TUFT";
    mem_idx MagicNumberSize = strnlen(MagicNumber, STRING_LEN);
    Assert(MagicNumberSize == 4);
    for(int NthLetter = 0;
        NthLetter < MagicNumberSize;
        ++NthLetter)
    {
        Header->MagicNumber[NthLetter] = MagicNumber[NthLetter];
    }

// Fields we can just copy    
    Header->NumTileRows = TileMap->NumRows;
    Header->NumTileCols = TileMap->NumCols;
    Header->NumTileTypes = TileMap->NumTileTypes;

// NumBitmaps per facing
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        Header->NumPlayerBitmapsPerFacing[NthFacing] = Player->AllBitmaps.Array[NthFacing].NumBitmaps;
    }

// Player DrawThis.
// It is slow to do multiple loops like this but it's easier to read the code and understand and don't think
//      perf is critical here
    Header->PlayerDrawThisOffset = OutfileArena->Cursor;
    int *PlayerDrawThisArray = PushArray(OutfileArena, int, 4);
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        PlayerDrawThisArray[NthFacing] = Player->AllBitmaps.Array[NthFacing].DrawThis;
    }

    Header->PlayerFilepathsOffset = OutfileArena->Cursor;

// Player filepaths
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        facing_bitmaps *ThisFacing = &Player->AllBitmaps.Array[NthFacing];
        meta_bitmap *ThisFacingMetaBitmaps = ThisFacing->MetaBitmaps;
        for(int NthBitmap = 1;
            NthBitmap <= ThisFacing->NumBitmaps;
            ++NthBitmap)
        {
            char *PlayerBitmapFilepath = ThisFacingMetaBitmaps[NthBitmap].Filepath;
            mem_idx FilepathLen = strlen(PlayerBitmapFilepath);
            // Null terminator
            ++FilepathLen;
            char *FilepathBufferInOutfile = PushArray(OutfileArena, char, FilepathLen);
            snprintf(FilepathBufferInOutfile, FilepathLen, "%s", PlayerBitmapFilepath);
        }
    }

// Tile type filepaths
    Header->TileTypeFilepathsOffset = OutfileArena->Cursor;
    for(int NthTileTypeFilepath = 1;
        NthTileTypeFilepath <= TileMap->NumTileTypes;
        ++NthTileTypeFilepath)
    {
        char *TileTypeFilepath = TileMap->TileTypes[NthTileTypeFilepath].Filepath;
        mem_idx FilepathLen = strlen(TileTypeFilepath);
        // Null terminator
        ++FilepathLen;
        char *FilepathBufferInOutfile = PushArray(OutfileArena, char, FilepathLen);
        snprintf(FilepathBufferInOutfile, FilepathLen, "%s", TileTypeFilepath);
    }

// Tile values
    Header->TileValuesOffset = OutfileArena->Cursor;
    mem_idx NumTileValuesToCopy = Header->NumTileRows * Header->NumTileCols;
    s32 *TileValuesInOutfile = PushArray(OutfileArena, s32, NumTileValuesToCopy);
    mem_idx NumBytesToCopy = NumTileValuesToCopy * sizeof(s32);
    memcpy(TileValuesInOutfile, TileMap->TileValues, NumBytesToCopy);

// Write file
    mem_idx NumBytesToWrite = OutfileArena->Cursor - 1;

    // TODO(AARON): On Windows we have updated WriteEntireFile to return the number of bytes the file wrote,
    //      so that we can return that from this function.
    mem_idx BytesWritten = WriteEntireFile(Savepath, NumBytesToWrite, OutfileArena->Data);
    if(BytesWritten != NumBytesToWrite)
    {
        Result = 0;
    }
    else
    {
        Result = BytesWritten;
    }

    FreeScratchArena(OutfileScratch);
    FreeScratchArena(pScratch);
    return(Result);
}

static mem_idx
LoadProject(player *Player, tile_map *TileMap, scratch_header *ScratchHeader)
{
    scratch_arena *pScratch = GetScratchArena(ScratchHeader);

    char *LoadedFileFilepath = PushArray(&pScratch->Arena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(LoadedFileFilepath, STRING_LEN, false);
    if(GetFilepathResult == 0) {FreeScratchArena(pScratch); return(0);}

    u32 LoadedFileSize = GetFileSize(LoadedFileFilepath);
    if(LoadedFileSize == 0) {FreeScratchArena(pScratch); return(0);}

    u8 *pLoadedDataStart = PushArray(&pScratch->Arena, u8, LoadedFileSize);
    mem_idx BytesReadIntoBuffer = ReadFileInto(LoadedFileFilepath, LoadedFileSize, pLoadedDataStart);
    if(BytesReadIntoBuffer == 0) {FreeScratchArena(pScratch); return(0);} 

    saved_project *pHeader = 0;
    pHeader = (saved_project *)pLoadedDataStart;

    // char MagicNumber[4];
    char *CompareMagicNumber = "TUFT";
    for(int LetterIdx = 0;
        LetterIdx < 4;
        ++LetterIdx)
    {
        if(CompareMagicNumber[LetterIdx] != pHeader->MagicNumber[LetterIdx]) {FreeScratchArena(pScratch); return(0);}
    }

    if( (pHeader->NumTileRows == 0) || (pHeader->NumTileCols == 0) ) {FreeScratchArena(pScratch); return(0);}

    TileMap->NumRows = pHeader->NumTileRows;
    TileMap->NumCols = pHeader->NumTileCols;
    TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
    TileMap->NumTileTypes = pHeader->NumTileTypes;

    // int NumPlayerBitmapsPerFacing[4];
    // mem_idx PlayerDrawThisOffset;
    // mem_idx PlayerFilepathsOffset;
    facing_bitmaps *pFacingBitmaps = Player->AllBitmaps.Array;
    char *pFilepathToCopyCursor = (char *)(pLoadedDataStart + pHeader->PlayerFilepathsOffset);
    int *pPlayerDrawThisToCopy = (int *)(pLoadedDataStart + pHeader->PlayerDrawThisOffset);
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        facing_bitmaps *pThisFacing = &pFacingBitmaps[NthFacing];
        pThisFacing->NumBitmaps = pHeader->NumPlayerBitmapsPerFacing[NthFacing];
        for(int NthFilepath = 0;
            NthFilepath < pThisFacing->NumBitmaps;
            ++NthFilepath)
        {
            char *pPasteFilepathHere = pThisFacing->MetaBitmaps[NthFilepath+1].Filepath;
            mem_idx FilepathLen = strlen(pFilepathToCopyCursor);
            ++FilepathLen; // Null terminator
            snprintf(pPasteFilepathHere, FilepathLen, "%s", pFilepathToCopyCursor);
            pFilepathToCopyCursor += FilepathLen;
        }
        LoadPlayerBitmapsDir(Player, pThisFacing, ScratchHeader);
        pThisFacing->DrawThis = pPlayerDrawThisToCopy[NthFacing];
    }

    // Use the same cursor pointer as we used for player filepaths
    pFilepathToCopyCursor = (char *)(pLoadedDataStart + pHeader->TileTypeFilepathsOffset);
    for(int NthTileType = 0;
        NthTileType < TileMap->NumTileTypes;
        ++NthTileType)
    {
        char *pPasteFilepathHere = TileMap->TileTypes[NthTileType+1].Filepath;
        mem_idx FilepathLen = strlen(pFilepathToCopyCursor);
        ++FilepathLen; // Null terminator
        snprintf(pPasteFilepathHere, FilepathLen, "%s", pFilepathToCopyCursor);
        pFilepathToCopyCursor += FilepathLen;
    }

    // Tile values
    mem_idx NumTileValueBytes = TileMap->NumTilesInWorld * sizeof(s32);
    memcpy(TileMap->TileValues, pLoadedDataStart + pHeader->TileValuesOffset, NumTileValueBytes);

    LoadTileBitmapsDir(TileMap, ScratchHeader);

    FreeScratchArena(pScratch); 
    return(0);
}

static void
ChangeHeldMetaBitmap(meta_bitmap **ppHeldMetaBitmap, meta_bitmap *pNew)
{
    *ppHeldMetaBitmap = pNew;
    return;
}

static void
DrawPlayerEditor(game_offscreen_buffer *pBackbuf, scratch_header *pScratchHeader, debug_state *pDebugState, player *pPlayer)
{
    editor_state *pEditorState = &pDebugState->EditorState;
    meta_bitmap **ppHeldTile = &pEditorState->CursorState.HeldMetaBitmap;
    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pBackbuf->Width * 0.5f), 0};
    v2 BrowserMax = {(f32)(pBackbuf->Width), (f32)(pBackbuf->Height)};
    scratch_arena *pScratch = GetScratchArena(pScratchHeader);
    
    // Get an array of menu_tile structs accommodating the maximum number of player bitmaps the game supports
    menu_tile *pMenuTiles = PushArray(&pScratch->Arena, menu_tile, 4 * MAX_FACING_BITMAPS);

    // Find out if there's a bagel active so that we can do the click-replace thing
    b32 BagelActive = false;
    for(int Facing = 0;
        Facing < 4;
        ++Facing)
    {
        facing_bitmaps *pThisFacing = &pPlayer->AllBitmaps.Array[Facing];
        for(int Slot = 1;
            Slot <= pThisFacing->NumBitmaps;
            ++Slot)
        {
            if(pThisFacing->MetaBitmaps[Slot].Bitmap.Pixels == pGlobalBagel->Bitmap.Pixels)
            {
                BagelActive = true;
                break;
            }
        }
    }

    DrawSpecialRect(pBackbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{0, 0, 0, 0});

    DEBUGDrawText(pBackbuf, BrowserMin.X + 300, 60, "Player Bitmaps Menu", &pDebugState->DebugTextArena, color{1, 1, 1, 1});

    char *Dirs[] = {"East", "North", "West", "South"};
    f32 VerticalSpaceBetweenSections = 220;
    v2 HeaderTextStart = {BrowserMin.X + 30, 120};
    f32 CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        menu_tile *MenuTilesCursor = pMenuTiles + NthFacing * MAX_FACING_BITMAPS;
        facing_bitmaps *Facing = &pPlayer->AllBitmaps.Array[NthFacing];
        char Temp[STRING_LEN];
        snprintf(Temp, STRING_LEN, "Facing %s", Dirs[NthFacing]);
        DEBUGDrawText(pBackbuf, HeaderTextStart.X, HeaderTextStart.Y, Temp, &pDebugState->DebugTextArena, color{0.9, 0.9, 0.9, 1}, 2.4);

        f32 XDrawCoord = HeaderTextStart.X;
        for(int NthBitmap = 1;
            NthBitmap <= Facing->NumBitmaps;
            ++NthBitmap)
        {
            bitmap *ThisBitmap = &Facing->MetaBitmaps[NthBitmap].Bitmap;
            f32 Width = ThisBitmap->Width;
            f32 Height = ThisBitmap->Height;

            MenuTilesCursor->TileMin = v2{(f32)XDrawCoord, CenterAroundThisVerticalLine - Height * 0.5f};
            MenuTilesCursor->TileMax = MenuTilesCursor->TileMin + v2{Width, Height};
            MenuTilesCursor->MetaBitmap = Facing->MetaBitmaps + NthBitmap;

            XDrawCoord = MenuTilesCursor->TileMax.X + 30.0f;
            ++MenuTilesCursor;
        }
        HeaderTextStart += v2{0, VerticalSpaceBetweenSections};
        CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    }
    
    for(int FacingIdx = 0;
        FacingIdx < 4;
        ++FacingIdx)
    {
        facing_bitmaps *NthFacing = &pPlayer->AllBitmaps.Array[FacingIdx];
        menu_tile *MenuTilesCursor = pMenuTiles + FacingIdx * MAX_FACING_BITMAPS;

        for(int NthBitmap = 1;
            NthBitmap <= NthFacing->NumBitmaps;
            ++NthBitmap)
        {
            // TODO(AARON): Think we are doing outlines differently in various places: e.g., here, we make the outline rect
            //      have dimensions one pixel larger in width and height than the bitmap being outlined; elsewhere
            //      we outline the outermost dimensions of the bitmap itself. We should do it the same way everywhere.
            ScaleAndBlitBitmap(pBackbuf, MenuTilesCursor->TileMin, MenuTilesCursor->TileMax, &MenuTilesCursor->MetaBitmap->Bitmap);
            v2 OutlineMin = MenuTilesCursor->TileMin + v2{-1.0f, -1.0f};
            v2 OutlineMax = MenuTilesCursor->TileMax + v2{1.0f, 1.0f};
            if(NthBitmap == NthFacing->DrawThis)
            {
                DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0.7, 0.5, 1});
            }
            else
            {
                DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0, 0, 1});        
            }
            if(MouseCoords.X >= MenuTilesCursor->TileMin.X &&
                MouseCoords.Y >= MenuTilesCursor->TileMin.Y &&
                MouseCoords.X < MenuTilesCursor->TileMax.X &&
                MouseCoords.Y < MenuTilesCursor->TileMax.Y)
            {
                DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
                if(pGlobalMouse->Primary.EndedDown && pGlobalMouse->Primary.HalfTransitionCount == 1)
                {
                    // There are three actions we need to handle when the user clicks a menu tile in the player editor:
                    //      1. If there is no bagel active, (which should be the vast majority of the time), 
                    //          clicking a menu tile in the player editor just sets the clicked tile to be
                    //          the DrawThis bitmap for that Facing.
                    //      
                    //      2. If there is a bagel active, then the user is allowed to hold a bitmap with
                    //          the cursor to replace the bagel. If they're not currently holding a bitmap
                    //          with the cursor, and they did not click the bagel, then clicking on a tile
                    //          should hold it so that they can next click on the bagel to replace it.
                    //
                    //      3. If there is a bagel active and the user is holding a bitmap with the cursor
                    //          and they clicked on the bagel, the click should replace the bagel, and the
                    //          held tile type should be reset to nullptr. 
                    if(BagelActive == false)
                    {
                        NthFacing->DrawThis = NthBitmap;
                        pPlayer->IsFacing = (facing)FacingIdx;
                    }
                    else // Bagel is active
                    {
                        // If the user did not click on the bagel, set HeldTileType to whatever they clicked on
                        if(NthFacing->MetaBitmaps[NthBitmap].Bitmap.Pixels != pGlobalBagel->Bitmap.Pixels)
                        {
                            ChangeHeldMetaBitmap(ppHeldTile, &NthFacing->MetaBitmaps[NthBitmap]);
                        }
                        // If they did click on the bagel
                        else
                        {
                            // And they are holding a tile type, replace the bagel with the held tile type
                            if(*ppHeldTile != nullptr)
                            {
                                meta_bitmap *GetsReplaced = MenuTilesCursor->MetaBitmap;
                                mem_idx GetsReplacedIdx = (GetsReplaced - NthFacing->MetaBitmaps);
                                mem_idx ReplaceWithIdx = (*ppHeldTile - NthFacing->MetaBitmaps);
                                ReplaceBagel(NthFacing->MetaBitmaps, GetsReplacedIdx, ReplaceWithIdx);
                                NthFacing->NumBitmaps = CountBitmaps(NthFacing->MetaBitmaps, MAX_FACING_BITMAPS);
                                ChangeHeldMetaBitmap(ppHeldTile, (meta_bitmap *)nullptr);
                            }
                        }
                    }
                }
            }
            ++MenuTilesCursor;
        }
    }

    // Draw a tiny version of the held tile
    if(*ppHeldTile != nullptr)
    {
        v2 TinyTileMin = MouseCoords + (v2){20.0f, 20.0f};

        DrawTinyMenuTile(pBackbuf, TinyTileMin, &(*ppHeldTile)->Bitmap);
    }

    facing_bitmaps *CurrentFacing = &pPlayer->AllBitmaps.Array[pPlayer->IsFacing];
    if(CurrentFacing->NumBitmaps > 0)
    {
        bitmap *ToDraw = &CurrentFacing->MetaBitmaps[CurrentFacing->DrawThis].Bitmap;
        // Draw player to left of browser if player editor is active
        f32 PlayerWidth = ToDraw->Width;
        f32 PlayerHeight = ToDraw->Height;

        v2 PlayerMin = v2{(f32)pBackbuf->Width * 0.25f - PlayerWidth * 0.5f, 
                            (f32)pBackbuf->Height * 0.5f - PlayerHeight * 0.5f};
        v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};
        ScaleAndBlitBitmap(pBackbuf, PlayerMin, PlayerMax, ToDraw);
    }

    FreeScratchArena(pScratch);
}

static void
DrawTileEditor(game_offscreen_buffer *pBackbuf, scratch_header *pScratchHeader, debug_state *pDebugState, tile_map *pTileMap)
{
    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pBackbuf->Width * 0.8f), 0};
    v2 BrowserMax = {(f32)(pBackbuf->Width), (f32)(pBackbuf->Height)};
    b32 AlreadyClickedSecondary = false;
    scratch_arena *pScratch = 0;
    menu_tile *pMenuTiles = 0;
    f32 Y = 160;
    f32 InnerPadding = 40;
    f32 OuterPadding = 56;
    f32 StartX = BrowserMin.X + OuterPadding;
    f32 X = StartX;
    int TilesDrawnInThisRow = 0;
    cursor_state *pCursorState = &pDebugState->EditorState.CursorState;
    meta_bitmap **ppHeldTile = &pCursorState->HeldMetaBitmap; 

    // Panel
    DrawSpecialRect(pBackbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{});

    DEBUGDrawText(pBackbuf, BrowserMin.X + 120, 80, "Tile Menu", &pDebugState->DebugTextArena, color{1, 1, 1, 1}, 3.5);

    // Draw tiles in menu unless there are no tiles 
    if(pTileMap->NumTileTypes > 0)
    {
        pScratch = GetScratchArena(pScratchHeader);
        pMenuTiles = PushArray(&pScratch->Arena, menu_tile, pTileMap->NumTileTypes);
        meta_bitmap *pNextTileType = pTileMap->TileTypes + 1;

        /***********MAKE ARRAY OF TILES TO DRAW**************/
        for(int NthTileType = 0;
            NthTileType < pTileMap->NumTileTypes;
            ++NthTileType)
        {
            // Scan for the next tile type that does not have an empty filepath
            while(pNextTileType->Filepath[0] == 0)
            {
                ++pNextTileType;
            }
            meta_bitmap *pThisTileType = pNextTileType;
            pNextTileType += 1;

            menu_tile *pThisMenuTile = pMenuTiles + NthTileType;

            // Store the min coordinate of where the tile will be drawn in the menu
            pThisMenuTile->TileMin = {X, Y};

            // Store the max coordinate of where the tile will be drawn in the menu
            pThisMenuTile->TileMax = pThisMenuTile->TileMin + (v2){pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};

            // Store the bitmap of the tile to be drawn
            pThisMenuTile->MetaBitmap = pThisTileType;

            // Compute the min coordinate of the next tile to be drawn
            ++TilesDrawnInThisRow;
            if(TilesDrawnInThisRow == 3)
            {
                TilesDrawnInThisRow = 0;
                X = StartX;
                Y += pTileMap->TileSideInPixels + InnerPadding;
            }
            else
            {
                X += pTileMap->TileSideInPixels + InnerPadding;
            }
        }

        /*********** DRAW THE MENU TILES AND DRAW BOXES AROUND THEM **************/
        for(int NthMenuTile = 0;
            NthMenuTile < pTileMap->NumTileTypes;
            ++NthMenuTile)
        {
            menu_tile *pThisMenuTile = pMenuTiles + NthMenuTile;
            ScaleAndBlitBitmap(pBackbuf, pThisMenuTile->TileMin, pThisMenuTile->TileMax, &pThisMenuTile->MetaBitmap->Bitmap);
            v2 OutlineMin = {pThisMenuTile->TileMin.X-1, pThisMenuTile->TileMin.Y-1};
            v2 OutlineMax = {pThisMenuTile->TileMax.X+1, pThisMenuTile->TileMax.Y+1};
            DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{}, color{0, 0, 0, 1});
        }

        /*********** UPDATE CURSOR STATE **************/
        b32 InBackbuf = IsInRect(MouseCoords, 
                                 v2{0, 0}, 
                                 v2{(f32)pBackbuf->Width, (f32)pBackbuf->Height});
        b32 InBrowser = IsInRect(MouseCoords, BrowserMin, BrowserMax);
        int MenuIdxMouseIsOver = -1;
        int MapIdxMouseIsOver = -1;
        // Determine where mouse is and whether it is hovering over a map tile or menu tile
        if(InBackbuf)
        {
            if(InBrowser)
            {
                for(int NthMenuTile = 0;
                    NthMenuTile < pTileMap->NumTileTypes;
                    ++NthMenuTile)
                {
                    menu_tile *pThisMenuTile = pMenuTiles + NthMenuTile;
                    if(IsInRect(MouseCoords, pThisMenuTile->TileMin, pThisMenuTile->TileMax))
                    {
                        MenuIdxMouseIsOver = NthMenuTile;
                        break;
                    }
                }
            }
            else
            {
                v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
                MapIdxMouseIsOver = Get1DTileCoordFrom2DCoord(pTileMap, TileAsV2);
            }
        }

        if(!pGlobalMouse->Primary.EndedDown)
        {
            pCursorState->PrimaryMode = IDLE;
        }
        else if(pCursorState->PrimaryMode==IDLE && 
                pGlobalMouse->Primary.EndedDown &&
                pGlobalMouse->Primary.HalfTransitionCount==1)
        {
            if(InBackbuf)
            {
                if(InBrowser)
                {
                    // The user has primary-clicked in the browser.
                    //      Consume the click and check if it was over a tile.
                    pCursorState->PrimaryMode = CONSUMED;
                    if(MenuIdxMouseIsOver > -1)
                    {
                        ChangeHeldMetaBitmap(ppHeldTile, 
                                             pMenuTiles[MenuIdxMouseIsOver].MetaBitmap);
                    }
                }
                else // The user primary-clicked in the tile map
                {
                    s32 TileValue = GetTileValueFrom1DCoord(pTileMap, MapIdxMouseIsOver);
                    meta_bitmap *ClickedTileBitmap = GetMetaBitmapPtrFromTileValue(pTileMap, TileValue);
                    if(IsTheZeroTile(pTileMap, ClickedTileBitmap)) // If the user clicked an empty tile
                    {
                        if(*ppHeldTile != nullptr) // And they are holding a tile
                        {
                            // Don't start primary painting
                            //      if user is secondary painting
                            if(pCursorState->SecondaryMode!=PAINTING)
                            {
                                pCursorState->PrimaryMode = PAINTING;
                            }
                        }
                    }
                    else // If the user primary-clicked a tile with a bitmap
                    {
                        if(*ppHeldTile != nullptr) // And they are holding a tile
                        {
                            pCursorState->PrimaryMode = CONSUMED;
                            // Change held tile to the clicked tile value
                            s32 ClickedTileValue = GetTileValueFrom1DCoord(pTileMap, MapIdxMouseIsOver);
                            meta_bitmap *pClickedMetaBitmap = GetMetaBitmapPtrFromTileValue(pTileMap, ClickedTileValue);
                            ChangeHeldMetaBitmap(ppHeldTile, pClickedMetaBitmap);
                        }
                    }
                }
            }
        }

        // Secondary cursor state
        if(!pGlobalMouse->Secondary.EndedDown)
        {
            pCursorState->SecondaryMode = IDLE;
        }
        if(pCursorState->SecondaryMode==IDLE &&
           pGlobalMouse->Secondary.EndedDown &&
           pGlobalMouse->Secondary.HalfTransitionCount==1)
        {
            if(InBackbuf)
            {
                if(InBrowser) // The user secondary-clicked in the browser
                {
                    pCursorState->SecondaryMode = CONSUMED;
                    ChangeHeldMetaBitmap(ppHeldTile, (meta_bitmap *)0);
                }
                else // The user secondary-clicked in the tile map
                {
                    // If they are holding a tile, drop it
                    if(*ppHeldTile != nullptr)
                    {
                        pCursorState->SecondaryMode = CONSUMED;
                        ChangeHeldMetaBitmap(ppHeldTile, (meta_bitmap *)0);
                    }
                    else
                    {
                        // Otherwise, begin painting, unless user
                        //      is already primary painting
                        if(pCursorState->PrimaryMode!=PAINTING)
                        {
                            pCursorState->SecondaryMode = PAINTING;
                        }
                    }
                }
            }
        }

        if(pCursorState->PrimaryMode==PAINTING)
        {
            SetTileValue(pTileMap, MapIdxMouseIsOver, 
                         GetTileValueIdxFromMetaBitmapPtr(pTileMap, *ppHeldTile));
        }
        else if(pCursorState->SecondaryMode==PAINTING)
        {
            SetTileValue(pTileMap, MapIdxMouseIsOver, 0);

        }

        // Highlight tile in map or menu if user is hovering over one
        if(MenuIdxMouseIsOver > -1)
        {
            menu_tile *pThisMenuTile = pMenuTiles + MenuIdxMouseIsOver;
            v2 OutlineMin = {pThisMenuTile->TileMin.X-1, pThisMenuTile->TileMin.Y-1};
            v2 OutlineMax = {pThisMenuTile->TileMax.X+1, pThisMenuTile->TileMax.Y+1};
            DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0.75, 0.75, 0.75, 0.5}, color{1, 1, 1, 1});
        }
        else if(MapIdxMouseIsOver > -1)
        {
            v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
            v2 OutlineMin = {TileAsV2.X * pTileMap->TileSideInPixels + 1, TileAsV2.Y * pTileMap->TileSideInPixels + 1};
            v2 OutlineMax = OutlineMin + v2{pTileMap->TileSideInPixels - 2, pTileMap->TileSideInPixels - 2};
            DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{0.9, 0.9, 0.9, 0.5}, color{});
        }

        // Draw a tiny version of the held tile
        if(*ppHeldTile!=nullptr)
        {
            v2 TinyTileMin = MouseCoords + v2{20.0f, 20.0f};
            DrawTinyMenuTile(pBackbuf, TinyTileMin, &(*ppHeldTile)->Bitmap);
        }

        if(pScratch)
        {
            FreeScratchArena(pScratch);
        }
    }
    // If there are no tiles, set HeldTileType to nullptr so we don't try to draw it and crash
    else
    {
        ChangeHeldMetaBitmap(ppHeldTile, (meta_bitmap *)nullptr);
    }

    // // If the user secondary-clicked while holding a tile type, stop holding tile type.
    // //      AlreadyClickedSecondary prevents the following situation:
    // //          - The user is holding a tile and secondary clicks with the pointer over a tile in the game
    // //          - HeldTileType changes to nullptr (this is good)
    // //          - But the tile in the game under the pointer is set to TILE_EMPTY at the same time (this is not good)
    // //      We want the interaction flow to be: 
    // //          - if user is hovering over a tile and they are holding a tile type and they secondary click, 
    // //                  stop holding that tile type and do nothing else
    // //          - then, if they secondary click again, set the tile value for tile under their cursor to be TILE_EMPTY
    // if((GlobalMouseFlags.SecondaryClicked) &&
    //    (*ppHeldTile))
    // {
    //     ChangeHeldTile(ppHeldTile, (meta_bitmap *)nullptr);
    //     AlreadyClickedSecondary = true;
    // }
    //
    //
    // // If mouse is not in editor panel, we need to:
    // //      - Highlight whatever tile pointer hovers over
    // //      - If user clicks primary mouse button while HeldTileType != nullptr, set type of tile under cursor to be equal
    // //              to held tile type
    // //      - If user clicks seconary mouse button while HeldTileType == nullptr, set type of tile under cursor to 
    // //              0 (TILE_EMPTY), in which case we draw nothing
    // if(GlobalMouseFlags.InBackbuf && (MouseCoords.X < BrowserMin.X))
    // {
    //     s32 OneDimensionalTileIndex = TileAsV2.Y * pTileMap->NumCols + TileAsV2.X;
    //
    //     if(GlobalMouseFlags.SecondaryClicked && 
    //        (*ppHeldTile == nullptr) && 
    //        (AlreadyClickedSecondary == false))
    //     {
    //         SetTileValue(pTileMap, OneDimensionalTileIndex, 0);
    //     }
    //     else if(GlobalMouseFlags.SecondaryHeld &&
    //             (*ppHeldTile == nullptr))
    //     {
    //         SetTileValue(pTileMap, OneDimensionalTileIndex, 0);
    //     }
    //     else if(GlobalMouseFlags.PrimaryClicked && (*ppHeldTile == nullptr))
    //     {
    //         s32 ClickedTileValue = pTileMap->TileValues[OneDimensionalTileIndex];
    //         if(ClickedTileValue)
    //         {
    //             meta_bitmap *pClickedTileType = &pTileMap->TileTypes[ClickedTileValue];
    //             ChangeHeldTile(ppHeldTile, pClickedTileType);
    //         }
    //     }
    //     else if(GlobalMouseFlags.PrimaryClicked || GlobalMouseFlags.PrimaryHeld)
    //     {
    //         if(*ppHeldTile != nullptr)
    //         {
    //             mem_idx NthTileValue = *ppHeldTile - pTileMap->TileTypes;
    //             SetTileValue(pTileMap, OneDimensionalTileIndex, NthTileValue);
    //         }
    //     }
    // }

    if(pDebugState->EditorState.PrintRowsCols)
    {
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
    tile_map *TileMap = &GameState->TileMap;
    player *Player = &GameState->Player;

    GetFileSize = Memory->DEBUGPlatformGetFileSize;
    FreeFileMemory = Memory->DEBUGPlatformFreeFileMemory;
    ReadEntireFile = Memory->DEBUGPlatformReadEntireFile;
    WriteEntireFile = Memory->DEBUGPlatformWriteEntireFile;
    GetFileWriteTime = Memory->DEBUGPlatformGetFileWriteTime;
    GetDirWriteTime = Memory->DEBUGPlatformGetDirWriteTime;
    GetListOfDirContents = Memory->DEBUGPlatformGetListOfDirContents;
    ReadFileInto = Memory->DEBUGPlatformReadFileInto;
    GetFilepathFromDialog = Memory->DEBUGPlatformGetFilepathFromDialog;

    // Global input pointers
    GlobalKeyboardController = GetController(Input, 0);
    GlobalDevKeys = &Input->DevKeys;
    pGlobalMouse = &Input->Mouse;

    // INIT
    if(!Memory->IsInitialized)
    {
        // Random
        GameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

// Arenas
        // Debug
        InitializeArena(&DebugState->DebugTextArena, (DebugRegion.Data + sizeof(debug_state)), Megabytes(1));
        InitializeArena(&DebugState->FailBitmapsArena, (DebugRegion.Data + sizeof(debug_state) + DebugState->DebugTextArena.Size), Megabytes(1));

        mem_idx GameRegionOffset = sizeof(game_state);

        // Tiles
        InitializeArena(&GameState->TileMap.TilesArena, GameRegion.Data + GameRegionOffset, Megabytes(4) );
        GameRegionOffset += GameState->TileMap.TilesArena.Size;

        // Player
        for(int Facing = 0;
            Facing < 4;
            ++Facing)
        {
            arena *Arena = &Player->AllBitmaps.Array[Facing].Arena;
            InitializeArena(Arena, GameRegion.Data + GameRegionOffset, Megabytes(1));
            GameRegionOffset += Arena->Size;
        }

        // World
        InitializeArena(&GameState->WorldArena, GameRegion.Data + GameRegionOffset, (GameRegion.Size - GameRegionOffset));

        mem_idx GameRegionMemoryUsed = sizeof(game_state) + 
                                        GameState->TileMap.TilesArena.Size + 
                                        (Player->AllBitmaps.East.Arena.Size * 4) + 
                                        GameState->WorldArena.Size;
        Assert(GameRegion.Size == GameRegionMemoryUsed);

        arena *WorldArena = &GameState->WorldArena;

        // Scratch
        ScratchHeader->Count = NUM_SCRATCHES;
        scratch_arena *ScratchArenas = ScratchHeader->ScratchArenas;
        for(int ScratchIdx = 0;
            ScratchIdx < ScratchHeader->Count;
            ++ScratchIdx)
        {
            scratch_arena *It = ScratchArenas + ScratchIdx;
            It->IsFree = true;
            InitializeArena(&It->Arena, 
                            (u8 *)Memory->TransientStorage + sizeof(scratch_header) + (ScratchIdx * SCRATCH_SIZE), 
                            SCRATCH_SIZE);
        }


// Tiles        
        // TileMap dimensions

        // TODO(Aaron): Move TileTypes array into memory?
        TileMap->TileSideInPixels = 64.0f;
        TileMap->NumRows = CeilingF32ToS32((f32)Buffer->Height / TileMap->TileSideInPixels);
        TileMap->NumCols = CeilingF32ToS32((f32)Buffer->Width / TileMap->TileSideInPixels);
        TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
        TileMap->TileValues = PushArray(WorldArena, s32, TileMap->NumTilesInWorld); 

        LoadTileBitmapsDir(TileMap, ScratchHeader);


// Player
        for(int Facing = 0;
            Facing < 4;
            ++Facing)
        {
            facing_bitmaps *ThisFacing = &Player->AllBitmaps.Array[Facing];
            LoadPlayerBitmapsDir(Player, ThisFacing, ScratchHeader);
            if(ThisFacing->NumBitmaps >= 1)
            {
                ThisFacing->DrawThis = 1;
            }
        }
        Player->Position.X = Buffer->Width / 2;
        Player->Position.Y = Buffer->Height / 2;
        Player->IsFacing = EAST;

// Bagel
        // TODO(AARON): ScaleAndBlitBitmap called with Bitmap == nullptr draw something other than bagel
        //      so we know when we called it with nullptr
        pGlobalBagel = PushStruct(&DebugState->FailBitmapsArena, meta_bitmap);
        char *BagelFilepath = "bagel1.bmp";
        snprintf(pGlobalBagel->Filepath, sizeof(pGlobalBagel->Filepath), "%s", BagelFilepath);
        LoadBitmap(&DebugState->FailBitmapsArena, pGlobalBagel);
        
        Memory->IsInitialized = true;
    }
    
    ///////////////////////////////////////// INIT END, MAIN LOOP START ////////////////////////////////////////////

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
    if(TileMap->ReadyToReload)
    {
        TileMap->ReadyToReload = false;
        LoadTileBitmapsDir(TileMap, ScratchHeader);
    }
    else
    {
        // Check dir write time and compare
        u64 CheckUpdateTime = GetDirWriteTime("tiles");
        if(TileMap->LastUpdateTime != CheckUpdateTime)
        {
            TileMap->ReadyToReload = true;
        }
        else
        {
            // If we're not planning to reload all bitmaps on the next frame, check each bitmap
            //      individually to see if that bitmap has been updated.
            for(int TileIdx = 1;
                TileIdx < TILE_TYPES_ARRAY_LEN;
                ++TileIdx)
            {
                meta_bitmap *TileType = TileMap->TileTypes + TileIdx;
                if(TileType->Filepath[0])
                {
                    // DEBUGReloadBitmap reloads the bitmap if the size of the one to load
                    //      is the same as the current one, and returns that size. 
                    //      If the sizes are different, it returns the new size, so the if check
                    //      below will fail, signaling that we need to reload the entire dir
                    //      because we might not have room for the new bitmap to load in the arena
                    mem_idx CurrentBitmapSize = TileType->Bitmap.Buffer.Size;
                    mem_idx BitmapToLoadSize = DEBUGReloadBitmapIfChanged(TileType);
                    if(CurrentBitmapSize != BitmapToLoadSize)
                    {
                        LoadTileBitmapsDir(TileMap, ScratchHeader);
                    }
                }
            }
        }
    }

    for(int FacingIdx = 0;
        FacingIdx < 4;
        ++FacingIdx)
    {
        facing_bitmaps *ThisFacing = &Player->AllBitmaps.Array[FacingIdx];
        if(ThisFacing->ReadyToReload)
        {
            ThisFacing->ReadyToReload = false;
            LoadPlayerBitmapsDir(Player, ThisFacing, ScratchHeader);
        }
        else
        {
            char Temp[STRING_LEN];
            WriteFacingBitmapsDirToBuffer(FacingIdx, Temp);
            u64 CheckUpdateTime = GetDirWriteTime(Temp);
            if(ThisFacing->LastUpdateTime != CheckUpdateTime)
            {
                ThisFacing->ReadyToReload = true;
            }
            else
            {
                for(int NthBitmap = 1;
                    NthBitmap <= MAX_FACING_BITMAPS;
                    ++NthBitmap)
                {
                    meta_bitmap *MetaBitmap = ThisFacing->MetaBitmaps + NthBitmap;
                    if(MetaBitmap->Filepath[0])
                    {
                        // DEBUGReloadBitmapIfChanged reloads the bitmap if the size of the one to load
                        //      is the same as the current one, and returns that size. 
                        //      If the sizes are different, it returns the new size, so the if check
                        //      below will fail, signaling that we need to reload the entire dir
                        //      because we might not have room for the new bitmap to load in the arena.
                        //
                        //  But another job DEBUGReloadBitmapIfChanged does is check if the update time for
                        //      the bitmap has changed and set ReadyToReload to true if it does, and
                        //      we also need to return a value from that control path...for now we
                        //      just return the bitmap size indicating that no reload of the
                        //      entire dir is necessary, but this is too messy; FIX
                        mem_idx CurrentBitmapSize = MetaBitmap->Bitmap.Buffer.Size;
                        mem_idx BitmapToLoadSize = DEBUGReloadBitmapIfChanged(MetaBitmap);
                        if(CurrentBitmapSize != BitmapToLoadSize)
                        {
                            LoadPlayerBitmapsDir(Player, ThisFacing, ScratchHeader);
                        }
                    }
                }
            }
        }
    }

    // Controller
    for(int ControllerIdx = 0;
        ControllerIdx < ArrayCount(Input->Controllers);
        ++ControllerIdx)
    {
        game_controller_input *Controller = GetController(Input, ControllerIdx);
        if(Controller->IsAnalog)
        {
        }
        else
        {
            v2 dPlayer = {};
            if(Controller->MoveRight.EndedDown)
            {
                dPlayer.X += 1.0f;
                Player->IsFacing = EAST;
            }
            if(Controller->MoveUp.EndedDown)
            {
                dPlayer.Y -= 1.0f;
                Player->IsFacing = NORTH;
            }
            if(Controller->MoveLeft.EndedDown)
            {
                dPlayer.X -= 1.0f;
                Player->IsFacing = WEST;
            }
            if(Controller->MoveDown.EndedDown && !Input->DevKeys.Ctrl.EndedDown)
            {
                // NOTE(AARON): The above check for the ctrl key may be incorrect but
                //      we won't know until we implement player movement again
                dPlayer.Y += 1.0f;
                Player->IsFacing = SOUTH;
            }
            if((dPlayer.X != 0) && (dPlayer.Y != 0))
            {
                dPlayer.X *= 0.707106781187f;
                dPlayer.Y *= 0.707106781187f;
            }
            f32 PlayerSpeed = 600.0f;
            
            if(DebugState->EditorState.WhichEditor == NO_EDITOR)
            {
                v2 NewPlayerP = Player->Position;
                NewPlayerP.X += dPlayer.X * PlayerSpeed * Input->dtForFrame;
                NewPlayerP.Y += dPlayer.Y * PlayerSpeed * Input->dtForFrame;
                Player->Position = NewPlayerP;
            }
        }
    }

    if(GlobalDevKeys->F1.EndedDown && GlobalDevKeys->F1.HalfTransitionCount == 1)
    {
        // Zero cursor state before changing editor
        DebugState->EditorState.CursorState = {};
        if(DebugState->EditorState.WhichEditor == TILE)
        {
            DebugState->EditorState.WhichEditor = NO_EDITOR;
        }
        else
        {
            DebugState->EditorState.WhichEditor = TILE;
        }
    }
    if(GlobalDevKeys->F6.EndedDown && GlobalDevKeys->F6.HalfTransitionCount == 1)
    {
        DebugState->EditorState.CursorState = {};
        if(DebugState->EditorState.WhichEditor == PLAYER)
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
    for(int Row = 0; 
        Row < TileMap->NumRows;
        ++Row)
    {
        for(int Col = 0;
            Col < TileMap->NumCols; 
            ++Col)
        {
            s32 TileOneDimensionalIndex = Row * TileMap->NumCols + Col;
            s32 TileValue = TileMap->TileValues[TileOneDimensionalIndex];
            v2 TileMin = {Col * TileMap->TileSideInPixels, Row * TileMap->TileSideInPixels};
            v2 TileMax = TileMin + (v2){TileMap->TileSideInPixels, TileMap->TileSideInPixels};

            if(TileValue > 0)
            {
                meta_bitmap *TileType = TileMap->TileTypes + TileValue;
                bitmap *TileBitmap = &TileMap->TileTypes[TileValue].Bitmap;
                ScaleAndBlitBitmap(Buffer, TileMin, TileMax, TileBitmap);
            }
            if(DebugState->EditorState.WhichEditor == TILE)
            {
                DrawSpecialRect(Buffer, TileMin, TileMax, color{}, color{0.1, 0.1, 0.1, 1});
            }
        }
    }

    if(DebugState->EditorState.WhichEditor==TILE)
    {
        DrawTileEditor(Buffer, ScratchHeader, DebugState, TileMap);
    }
    else if(DebugState->EditorState.WhichEditor==PLAYER)
    {
        DrawPlayerEditor(Buffer, ScratchHeader, DebugState, &GameState->Player);
    }
    else
    {
        // Player
        facing_bitmaps *CurrentFacing = &Player->AllBitmaps.Array[Player->IsFacing];
        bitmap *ToDraw = &CurrentFacing->MetaBitmaps[CurrentFacing->DrawThis].Bitmap;
        f32 PlayerWidth = ToDraw->Width;
        f32 PlayerHeight = ToDraw->Height;
        v2 PlayerMin = {Player->Position.X - PlayerWidth * 0.5f, 
                        Player->Position.Y - PlayerHeight};
        v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};

        // TODO(Aaron): doing it once per frame is bound to be very slow, and it seems like i can detect slightly jittery animation
        //      in the game when moving character around. test this.
        ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, ToDraw);
    }

    // I think we don't need to check half transition count for the keys with these save/load commands because subsequent EndedDown
    //      messages are routed to the message loop for the save/load dialog, not our usual
    //      message loop in the platform layer, and we have code in the platform layer to
    //      ensure that these are ignored (AS, 9/22)
    //
    // Note MoveDown is the "S" key
    if(GlobalKeyboardController->MoveDown.EndedDown && GlobalDevKeys->Ctrl.EndedDown)
    {
        SaveProject(TileMap, Player, ScratchHeader);
    }

    // and ActionRight is the "L" key
    if(GlobalKeyboardController->ActionRight.EndedDown && GlobalDevKeys->Ctrl.EndedDown)
    {
        LoadProject(Player, TileMap, ScratchHeader);
    }

    if(GlobalDevKeys->F2.EndedDown && GlobalDevKeys->F2.HalfTransitionCount == 1)
    {
        DebugState->EditorState.PrintRowsCols = !DebugState->EditorState.PrintRowsCols;
    }

    DEBUGPrintFps(Buffer, Input->Fps, &DebugState->DebugTextArena);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

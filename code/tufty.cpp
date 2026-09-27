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
    static meta_bitmap *GlobalBagel;
    static game_controller_input *GlobalKeyboardController;
    static dev_keys *GlobalDevKeys;
    static game_mouse_input *GlobalMouse;

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
LoadBitmap(arena *Arena, meta_bitmap *MetaBitmap)
{
    bitmap *Bitmap = &MetaBitmap->Bitmap;
    char *Filepath = MetaBitmap->Filepath;
    // If we can't get the file size, then we can't load the file, 
    //      so we want the file size in the bitmap to be 0.
    Bitmap->Buffer.Size = GetFileSize(Filepath);
    if(Bitmap->Buffer.Size == 0)
    {
        return;
    }
    Bitmap->Buffer.Data = PushArray(Arena, u8, Bitmap->Buffer.Size);
    MetaBitmap->ReadyToReload = true;
    DEBUGReloadBitmapIfChanged(MetaBitmap);
}

static void 
LoadPlayerBitmapsDir(player *Player, facing_bitmaps *FacingBitmaps, scratch_header *ScratchHeader)
{
    // Figure out which set of facing bitmaps we're looking at (E, N, W, S)
    mem_idx WhichFacing = FacingBitmaps - Player->AllBitmaps.Array;
    arena *FacingArena = &FacingBitmaps->Arena;
    meta_bitmap *MetaBitmaps = FacingBitmaps->MetaBitmaps;
    char Dirpath[STRING_LEN];
    WriteFacingBitmapsDirToBuffer(WhichFacing, Dirpath);

    ResetArena(FacingArena);
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    
    // Unlike with tiles, we don't need to worry about preserving a mapping between TileValues and 
    //      indices in the TileTypes array. We only want to ensure that bitmap selected
    //      as the MetaBitmaps->DrawThis bitmap continues to be selected after the reload,
    //      assuming it is still in the game. Therefore, we store the filepath for the DrawThis
    //      bitmap before clearing the MetaBitmaps array. If it is no longer in the game after reload, we just set
    //      MetaBitmaps->DrawThis to 1 if there is at least one bitmap for this facing
    //      in the dir, or 0 if there are no bitmaps in the dir
    
    char SavedDrawThisFilepath[STRING_LEN];
    snprintf(SavedDrawThisFilepath, STRING_LEN, "%s", MetaBitmaps[FacingBitmaps->DrawThis].Filepath);

    //  Clear the entire meta bitmaps array
    for(int NthBitmap = 0;
        NthBitmap < FACING_BITMAPS_ARRAY_LEN;
        ++NthBitmap)
    {
        MetaBitmaps[NthBitmap] = {};
    }

    // Other stuff that needs state adjusted
    FacingBitmaps->LastUpdateTime = GetDirWriteTime(Dirpath);
    FacingBitmaps->ReadyToReload = false;
    FacingBitmaps->NumBitmaps = 0;
    FacingBitmaps->DrawThis = 0;

    buffer FoundFilenames;
    FoundFilenames.Size = MAX_FACING_BITMAPS * STRING_LEN;
    FoundFilenames.Data = PushArray(&Scratch->Arena, u8, FoundFilenames.Size);
    int NumFilesFound = 0;
    
    GetListOfDirContents(&FoundFilenames, Dirpath, &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if((NumFilesFound > MAX_FACING_BITMAPS) || (NumFilesFound == 0))
    {
        FreeScratchArena(Scratch);
        return;
    }

    // If we made it this far, we found a valid number of bitmaps (i.e., more than 0 and less than or equal to the max)
    char *ThisFilename = (char *)FoundFilenames.Data;
    for(int NthBitmapToLoad = 0;
        NthBitmapToLoad < NumFilesFound;
        ++NthBitmapToLoad)
    {
        char FilepathToLoad[STRING_LEN];
        snprintf(FilepathToLoad, sizeof(FilepathToLoad), "%s/%s", Dirpath, ThisFilename);
        meta_bitmap *Slot = &MetaBitmaps[NthBitmapToLoad+1];
        snprintf(Slot->Filepath, STRING_LEN, "%s", FilepathToLoad);
        LoadBitmap(FacingArena, Slot);
        ++FacingBitmaps->NumBitmaps;

        while(*ThisFilename)
        {
            ++ThisFilename;
        }
        ++ThisFilename;
    }

    // Check to see if the previous DrawThis bitmap filepath is still in the game
    //      and set DrawThis to the idx of that bitmap if so (because its idx may have changed).
    //      If not, just set the idx to 1.

    FacingBitmaps->DrawThis = 1;
    for(int NthSlot = 1;
        NthSlot <= FacingBitmaps->NumBitmaps;
        ++NthSlot)
    {
        meta_bitmap *NthMetaBitmap = MetaBitmaps + NthSlot;
        if(strncmp(SavedDrawThisFilepath, NthMetaBitmap->Filepath, strlen(NthMetaBitmap->Filepath)) == 0)
        {
            FacingBitmaps->DrawThis = NthSlot;
        }
    }

    FreeScratchArena(Scratch);
}

static void
LoadTileBitmapsDir(tile_map *TileMap, scratch_header *ScratchHeader)
{
    // TODO: Use CPP class constructor destructor setup to always 
    //      FreeScratchArena whenever any scope containing a 
    //      GetScratchArena call is exited
    arena *TilesArena = &TileMap->TilesArena;
    ResetArena(TilesArena);
    meta_bitmap *TileTypes = TileMap->TileTypes;
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    
    // Clear bitmap structs in tile types array
    for(int TypeIdx = 0;
        TypeIdx < TILE_TYPES_ARRAY_LEN;
        ++TypeIdx)
    {
        TileTypes[TypeIdx].LastUpdateTime = 0;
        TileTypes[TypeIdx].ReadyToReload = false;
        TileTypes[TypeIdx].Bitmap = {};
    }

    TileMap->LastUpdateTime = GetDirWriteTime("tiles");
    TileMap->ReadyToReload = false;
    TileMap->NumTileTypes = 0;

    buffer FoundFilenames;
    FoundFilenames.Size = MAX_TILE_TYPES * STRING_LEN;
    FoundFilenames.Data = PushArray(&Scratch->Arena, u8, FoundFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&FoundFilenames, "tiles", &NumFilesFound);

    // If we found no bitmaps or too many, just return
    if((NumFilesFound > MAX_TILE_TYPES) || (NumFilesFound == 0))
    {
        FreeScratchArena(Scratch);
        return;
    }

    // If we made it this far, we found a valid number of bitmaps (i.e., more than 0 and less than or equal to the max)
    char *ThisFilename = (char *)FoundFilenames.Data;
    for(int NthBitmapToLoad = 0;
        NthBitmapToLoad < NumFilesFound;
        ++NthBitmapToLoad)
    {
        char FilepathToLoad[STRING_LEN];
        
        // TODO(Aaron): Confirm that we check that the OS is giving us a filename less than or equal to STRING_LEN in length
        snprintf(FilepathToLoad, sizeof(FilepathToLoad), "tiles/%s", ThisFilename);

        int FirstAvailable = 0;
        b32 ExistingTileType = false;
        // Always keep 0th slot empty
        for(int NthSlot = 1;
            NthSlot < MAX_TILE_TYPES + 1;
            ++NthSlot)
        {
            meta_bitmap *TileType = TileTypes + NthSlot;
            if(TileType->Filepath[0] == 0)
            {
                if(!FirstAvailable)
                {
                    FirstAvailable = NthSlot;
                }
            }
            else
            {
                if(strncmp(FilepathToLoad, TileType->Filepath, STRING_LEN) == 0)
                {
                    LoadBitmap(TilesArena, TileType);
                    ExistingTileType = true;
                    break;
                }
            }
        }

        // If the bitmap wasn't in the game previously, we pick the first available slot.
        //      We have to copy the contents of the FilepathToLoad array to the new ID's Filepath field.
        if(ExistingTileType == false)
        {
            Assert(strnlen(FilepathToLoad, STRING_LEN) < STRING_LEN);
            meta_bitmap *SlotForNewBitmap = TileTypes + FirstAvailable;
            snprintf(SlotForNewBitmap->Filepath, STRING_LEN, "%s", FilepathToLoad);
            LoadBitmap(TilesArena, SlotForNewBitmap);
        }

        while(*ThisFilename)
        {
            ++ThisFilename;
        }
        ++ThisFilename;
    }

        // Do some cleanup. Find all instances of TileTypes that do have filepaths, but have no bitmap pointers.
        //      These are tile bitmaps that were removed from the game. We want them to be
        //      drawn as a bagel instead

    for(int Slot = 1;
        Slot < TILE_TYPES_ARRAY_LEN;
        ++Slot)
    {
        meta_bitmap *TileType = TileTypes + Slot;
        bitmap *Bitmap = &TileType->Bitmap;
        if(TileType->Filepath[0]) // If the TileType has a filepath
        {
            ++TileMap->NumTileTypes;
            if(Bitmap->Buffer.Data == 0) // if bitmap buffer pointer is null
            {
                // Set the bitmap to bagel
                *Bitmap = GlobalBagel->Bitmap;
            }
        }
    }

    FreeScratchArena(Scratch);
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

static v2
GetTileCoordsFromMouseCoords(f32 TileSideInPixels)
{
    v2 Result = {};
    Result.X = FloorF32ToS32(GlobalMouse->X / (s32)TileSideInPixels);
    Result.Y = FloorF32ToS32(GlobalMouse->Y / (s32)TileSideInPixels);
    return(Result);
}

static mem_idx
SaveProject(tile_map *TileMap, player *Player, scratch_header *ScratchHeader)
{
    // If we successfully got a filepath from the OS file dialog and wrote the project to a file,
    //      this function returns the number of bytes written to that file. If we didn't have
    //      anything to write (e.g., empty project), or some failure occurred along the way,
    //      we return 0. The caller can interpret this in whichever way they choose.

    mem_idx Result = 0;
    // TODO (AARON): Remove this and test how it behaves if we have no tile types, no player bitmaps, or combo of both

    // On Windows we call GetSaveFilenameA. It returns either nonzero or zero.
    //      The return value is NONZERO if: 
    //              - the user specifies a filepath, and 
    //              - clicks the OK button, and
    //              - the function is successful.
    //      The return value is ZERO if:
    //              - the user cancels, or
    //              - the user closes the dialog box, or
    //              - an error, such as the file name buffer being too small, occurred.
    char FilepathToWrite[STRING_LEN];
    int GetFilepathResult = GetFilepathFromDialog(FilepathToWrite, STRING_LEN, true);
    if(GetFilepathResult == 0)
    {
        return(0);
    }
 
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
    mem_idx BytesWritten = WriteEntireFile(FilepathToWrite, NumBytesToWrite, OutfileArena->Data);
    if(BytesWritten != NumBytesToWrite)
    {
        Result = 0;
    }
    else
    {
        Result = BytesWritten;
    }

    FreeScratchArena(OutfileScratch);
    return(Result);
}

mem_idx
LoadProject(player *Player, tile_map *TileMap, scratch_header *ScratchHeader)
{
    scratch_arena *pScratch = GetScratchArena(ScratchHeader);
    arena *pTilesArena = &TileMap->TilesArena;
    all_bitmaps *pFacingBitmaps = &Player->AllBitmaps.Array;
    saved_project *pHeader = 0;
    int *pPlayerDrawThis = 0;
    char *pPlayerFilepaths = 0;
    char *pTileTypeFilepaths = 0;
    s32 *pTileValues = 0;

    char *LoadedFileFilepath = PushArray(pScratch->Arena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(LoadedFileFilepath, STRING_LEN, false);
    if(GetFilepathResult == 0) { goto fail; }

    u32 LoadedFileSize = GetFileSize(LoadedFileFilepath);
    if(LoadedFileSize == 0) { goto fail; }

    u8 *LoadedProject = PushArray(pScratch->Arena, u8, LoadedFileSize);
    // Validate and set data pointers
    mem_idx BytesReadIntoBuffer = ReadFileInto(LoadedFilepath, LoadedFileSize, LoadedProject);
    if(BytesReadIntoBuffer == 0) { goto fail; }

    pHeader = (saved_project *)LoadedProject;
    char *CompareMagicNumber = "TUFT";
    for(int LetterIdx = 0;
        LetterIdx < 4;
        ++LetterIdx)
    {
        if(CompareMagicNumber[LetterIdx] != Header->MagicNumber[LetterIdx]) { goto fail; }
    }

fail:
    FreeScratchArena(pScratch);
    return(0);


}




































static b32
LoadTileMapFromFile(tile_map *TileMap, scratch_header *ScratchHeader)
{
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    arena *TilesArena = &TileMap->TilesArena;

    b32 Result = false;

    // Pointers to the data in the loaded file
    saved_project *Header = 0;
    char *TileTypeFilepaths = 0;
    s32 *LoadedTileValues = 0;

    // TODO(Aaron): Fix how we handle strings to be uniform. Currently we are all over the place!!!!
    mem_idx MaxPath = 260;
    char *Filepath = PushArray(&Scratch->Arena, char, MaxPath);
    int GetFilepathResult = GetFilepathFromDialog(Filepath, MaxPath, false);
    if(GetFilepathResult == 0)
    {
        FreeScratchArena(Scratch);
        return(Result);
    }

    u32 Size = GetFileSize(Filepath);
    u8 *LoadedTileMap = PushArray(&Scratch->Arena, u8, Size);
    // We don't worry about dest buffer being too small here because if it's too small
    //      PushArray call above will fail

    // Validate and set data pointers
    if(ReadFileInto(Filepath, Size, LoadedTileMap))
    {
        Header = (saved_project *)LoadedTileMap;
        char *CompareMagicNumber = "TILE";
        for(int LetterIdx = 0;
            LetterIdx < 4;
            ++LetterIdx)
        {
            Assert(CompareMagicNumber[LetterIdx] == Header->MagicNumber[LetterIdx]);
        }
        Assert(Header->NumTileRows);
        TileMap->NumRows = Header->NumTileRows;
        Assert(Header->NumTileCols);
        TileMap->NumCols = Header->NumTileCols;
        Assert(Header->NumTileTypes);
        TileMap->NumTileTypes = Header->NumTileTypes;
        TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
        TileTypeFilepaths = (char *)(LoadedTileMap + Header->TileTypeFilepathsOffset);
        LoadedTileValues = (s32 *)(LoadedTileMap + Header->TileValuesOffset);
    }

    // If the file is valid go ahead and clear the tile data structures to prepare for loading 
    //      the new data from the file
    ResetArena(TilesArena);
    meta_bitmap *TileTypes = TileMap->TileTypes;
    for(int TypeIdx = 0;
        TypeIdx < TILE_TYPES_ARRAY_LEN;
        ++TypeIdx)
    {
        TileTypes[TypeIdx] = {};
    }

    char *FilepathCursor = TileTypeFilepaths;
    for(int NthType = 1;
        NthType <= TileMap->NumTileTypes;
        ++NthType)
    {
        meta_bitmap *ThisType = TileTypes + NthType;
        mem_idx FilepathLen = strnlen(FilepathCursor, STRING_LEN);

        FilepathLen += 1;
        snprintf(ThisType->Filepath, FilepathLen, "%s", FilepathCursor);
        FilepathCursor += FilepathLen;
    }

    LoadTileBitmapsDir(TileMap, ScratchHeader);

    for(int ValueIdx = 0;
        ValueIdx < TileMap->NumTilesInWorld;
        ++ValueIdx)
    {
        TileMap->TileValues[ValueIdx] = LoadedTileValues[ValueIdx];
    }

    FreeScratchArena(Scratch);

    return(true);
}

static void
DrawPlayerEditor(game_offscreen_buffer *Backbuf, scratch_header *ScratchHeader, debug_state *DebugState, player *Player)
{
    v2 MouseCoords = {(f32)GlobalMouse->X, (f32)GlobalMouse->Y};
    v2 BrowserMin = {(f32)(Backbuf->Width * 0.5f), 0};
    v2 BrowserMax = {(f32)(Backbuf->Width), (f32)(Backbuf->Height)};
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    
    // Get an array of menu_tile structs accommodating the maximum number of player bitmaps the game supports
    menu_tile *MenuTiles = PushArray(&Scratch->Arena, menu_tile, 4 * MAX_FACING_BITMAPS);

    DrawSpecialRect(Backbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{0, 0, 0, 0});

    DEBUGDrawText(Backbuf, BrowserMin.X + 300, 60, "Player Bitmaps Menu", &DebugState->DebugTextArena, color{1, 1, 1, 1});

    char *Dirs[] = {"East", "North", "West", "South"};
    int NumTilesNeeded = 0;
    f32 VerticalSpaceBetweenSections = 220;
    v2 HeaderTextStart = {BrowserMin.X + 30, 120};
    f32 CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    for(int NthFacing = 0;
        NthFacing < 4;
        ++NthFacing)
    {
        menu_tile *MenuTilesCursor = MenuTiles + NthFacing * MAX_FACING_BITMAPS;
        facing_bitmaps *Facing = &Player->AllBitmaps.Array[NthFacing];
        char Temp[STRING_LEN];
        snprintf(Temp, STRING_LEN, "Facing %s", Dirs[NthFacing]);
        DEBUGDrawText(Backbuf, HeaderTextStart.X, HeaderTextStart.Y, Temp, &DebugState->DebugTextArena, color{0.9, 0.9, 0.9, 1}, 2.4);

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
            ++NumTilesNeeded;
        }
        HeaderTextStart += v2{0, VerticalSpaceBetweenSections};
        CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    }
    
    for(int FacingIdx = 0;
        FacingIdx < 4;
        ++FacingIdx)
    {
        facing_bitmaps *NthFacing = &Player->AllBitmaps.Array[FacingIdx];
        menu_tile *MenuTilesCursor = MenuTiles + FacingIdx * MAX_FACING_BITMAPS;

        for(int NthBitmap = 1;
            NthBitmap <= NthFacing->NumBitmaps;
            ++NthBitmap)
        {
            // TODO(AARON): Think we are doing outlines differently in various places: e.g., here, we make the outline rect
            //      have dimensions one pixel larger in width and height than the bitmap being outlined; elsewhere
            //      we outline the outermost dimensions of the bitmap itself. We should do it the same way everywhere.
            ScaleAndBlitBitmap(Backbuf, MenuTilesCursor->TileMin, MenuTilesCursor->TileMax, &MenuTilesCursor->MetaBitmap->Bitmap);
            v2 OutlineMin = MenuTilesCursor->TileMin + v2{-1.0f, -1.0f};
            v2 OutlineMax = MenuTilesCursor->TileMax + v2{1.0f, 1.0f};
            if(NthBitmap == NthFacing->DrawThis)
            {
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0.7, 0.5, 1});
            }
            else
            {
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0, 0, 1});        
            }
            if(MouseCoords.X >= MenuTilesCursor->TileMin.X &&
                MouseCoords.Y >= MenuTilesCursor->TileMin.Y &&
                MouseCoords.X < MenuTilesCursor->TileMax.X &&
                MouseCoords.Y < MenuTilesCursor->TileMax.Y)
            {
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
                if(GlobalMouse->Primary.EndedDown && GlobalMouse->Primary.HalfTransitionCount == 1)
                {
                    NthFacing->DrawThis = NthBitmap;
                    Player->IsFacing = (facing)FacingIdx;
                }
            }
            ++MenuTilesCursor;
        }
    }

    facing_bitmaps *CurrentFacing = &Player->AllBitmaps.Array[Player->IsFacing];
    if(CurrentFacing->NumBitmaps > 0)
    {
        bitmap *ToDraw = &CurrentFacing->MetaBitmaps[CurrentFacing->DrawThis].Bitmap;
        // Draw player to left of browser if player editor is active
        f32 PlayerWidth = ToDraw->Width;
        f32 PlayerHeight = ToDraw->Height;

        v2 PlayerMin = v2{(f32)Backbuf->Width * 0.25f - PlayerWidth * 0.5f, 
                            (f32)Backbuf->Height * 0.5f - PlayerHeight * 0.5f};
        v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};
        ScaleAndBlitBitmap(Backbuf, PlayerMin, PlayerMax, ToDraw);
    }

    FreeScratchArena(Scratch);
}

static void
DrawTileEditor(game_offscreen_buffer *Backbuf, scratch_header *ScratchHeader, debug_state *DebugState, tile_map *TileMap)
{
    v2 MouseCoords = {(f32)GlobalMouse->X, (f32)GlobalMouse->Y};
    v2 BrowserMin = {(f32)(Backbuf->Width * 0.8f), 0};
    v2 BrowserMax = {(f32)(Backbuf->Width), (f32)(Backbuf->Height)};
    b32 AlreadyClickedSecondary = false;
    scratch_arena *Scratch = 0;
    menu_tile *MenuTiles = 0;
    f32 Y = 160;
    f32 InnerPadding = 40;
    f32 OuterPadding = 56;
    f32 StartX = BrowserMin.X + OuterPadding;
    f32 X = StartX;
    int TilesDrawnInThisRow = 0;
    editor_state *EditorState = &DebugState->EditorState;

    // Panel
    DrawSpecialRect(Backbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{});

    DEBUGDrawText(Backbuf, BrowserMin.X + 120, 80, "Tile Menu", &DebugState->DebugTextArena, color{1, 1, 1, 1}, 3.5);

    // Draw tiles in menu unless there are no tiles
    if(TileMap->NumTileTypes > 0)
    {
        Scratch = GetScratchArena(ScratchHeader);
        MenuTiles = PushArray(&Scratch->Arena, menu_tile, TileMap->NumTileTypes);
        meta_bitmap *NextTileType = TileMap->TileTypes + 1;

        for(int NthTileType = 0;
            NthTileType < TileMap->NumTileTypes;
            ++NthTileType)
        {
            // Scan for the next tile type that does not have an empty filepath
            while(NextTileType->Filepath[0] == 0)
            {
                ++NextTileType;
            }
            meta_bitmap *ThisTileType = NextTileType;
            NextTileType += 1;

            menu_tile *ThisMenuTile = MenuTiles + NthTileType;
            ThisMenuTile->TileMin = {X, Y};
            ThisMenuTile->TileMax = ThisMenuTile->TileMin + (v2){TileMap->TileSideInPixels, TileMap->TileSideInPixels};
            ThisMenuTile->MetaBitmap = ThisTileType;

            ++TilesDrawnInThisRow;
            if(TilesDrawnInThisRow == 3)
            {
                TilesDrawnInThisRow = 0;
                X = StartX;
                Y += TileMap->TileSideInPixels + InnerPadding;
            }
            else
            {
                X += TileMap->TileSideInPixels + InnerPadding;
            }
        }

        for(int NthMenuTile = 0;
            NthMenuTile < TileMap->NumTileTypes;
            ++NthMenuTile)
        {
            menu_tile *ThisMenuTile = MenuTiles + NthMenuTile;
            ScaleAndBlitBitmap(Backbuf, ThisMenuTile->TileMin, ThisMenuTile->TileMax, &ThisMenuTile->MetaBitmap->Bitmap);

            v2 OutlineMin = {ThisMenuTile->TileMin.X-1, ThisMenuTile->TileMin.Y-1};
            v2 OutlineMax = {ThisMenuTile->TileMax.X+1, ThisMenuTile->TileMax.Y+1};

            if(MouseCoords.X >= ThisMenuTile->TileMin.X &&
               MouseCoords.Y >= ThisMenuTile->TileMin.Y &&
               MouseCoords.X <= ThisMenuTile->TileMax.X &&
               MouseCoords.Y <= ThisMenuTile->TileMax.Y)
            {
                if(GlobalMouse->Primary.EndedDown && GlobalMouse->Primary.HalfTransitionCount == 1)
                {
                    if(EditorState->HeldTileType && 
                        (ThisMenuTile->MetaBitmap->Bitmap.Pixels == GlobalBagel->Bitmap.Pixels))
                    {
                        meta_bitmap *TileTypeToReplace = ThisMenuTile->MetaBitmap;
                        mem_idx TileTypeToReplaceIdx = (ThisMenuTile->MetaBitmap - TileMap->TileTypes);
                        mem_idx OldTileIdx = (EditorState->HeldTileType - TileMap->TileTypes);
                        TileMap->TileTypes[TileTypeToReplaceIdx] = TileMap->TileTypes[OldTileIdx]; 
                        TileMap->TileTypes[OldTileIdx] = {};
                        for(int TileValuesIdx = 0;
                            TileValuesIdx < TileMap->NumTilesInWorld;
                            ++TileValuesIdx)
                        {
                            s32 *ThisTileValue = TileMap->TileValues + TileValuesIdx;
                            s32 Test = TileMap->TileValues[TileValuesIdx];
                            if(*ThisTileValue == (s32)OldTileIdx)
                            {
                                *ThisTileValue = TileTypeToReplaceIdx;
                            }
                        }
                        EditorState->HeldTileType = nullptr;
                        --TileMap->NumTileTypes;
                    }
                    else
                    {
                        EditorState->HeldTileType = ThisMenuTile->MetaBitmap;
                    }
                }
                // If mouse is hovering over this tile, highlight it
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{0.75, 0.75, 0.75, 0.5}, color{1, 1, 1, 1});
            }
            else
            {
                // Otherwise just draw a box around it
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{}, color{0, 0, 0, 1});
            }
        }
        if(Scratch)
        {
            FreeScratchArena(Scratch);
        }
    }
    // If there are no tiles, set HeldTileType to nullptr so we don't try to draw it and crash
    else
    {
        EditorState->HeldTileType = nullptr;
    }

    // If the user secondary-clicked while holding a tile type, stop holding tile type.
    //      AlreadyClickedSecondary prevents the following situation:
    //          - The user is holding a tile and secondary clicks with the pointer over a tile in the game
    //          - HeldTileType changes to nullptr (this is good)
    //          - But the tile in the game under the pointer is set to TILE_EMPTY at the same time (this is not good)
    //      We want the interaction flow to be: 
    //          - if user is hovering over a tile and they are holding a tile type and they secondary click, 
    //                  stop holding that tile type and do nothing else
    //          - then, if they secondary click again, set the tile value for tile under their cursor to be TILE_EMPTY
    if((GlobalMouse->Secondary.EndedDown) && 
       (GlobalMouse->Secondary.HalfTransitionCount == 1) &&
       (EditorState->HeldTileType))
    {
        EditorState->HeldTileType = nullptr;
        AlreadyClickedSecondary = true;
    }

    // Draw a tiny version of the held tile
    if(EditorState->HeldTileType != nullptr)
    {
        v2 TinyTileMin = MouseCoords + (v2){20.0f, 20.0f};
        v2 TinyTileMax = TinyTileMin + (v2){(f32)(TileMap->TileSideInPixels * 0.5), (f32)(TileMap->TileSideInPixels * 0.5)};
        ScaleAndBlitBitmap(Backbuf, TinyTileMin, TinyTileMax, &EditorState->HeldTileType->Bitmap);
        DrawSpecialRect(Backbuf, TinyTileMin, TinyTileMax, color{}, color{0, 0, 0, 1});
    }


    // If mouse is not in editor panel, we need to:
    //      - Highlight whatever tile pointer hovers over
    //      - If user clicks primary mouse button while HeldTileType != nullptr, set type of tile under cursor to be equal
    //              to held tile type
    //      - If user clicks seconary mouse button while HeldTileType == nullptr, set type of tile under cursor to 
    //              0 (TILE_EMPTY), in which case we draw nothing
    if(MouseCoords.X < BrowserMin.X)
    {
        v2 TileAsV2 = GetTileCoordsFromMouseCoords(TileMap->TileSideInPixels);
        v2 OutlineMin = {TileAsV2.X * TileMap->TileSideInPixels + 1, TileAsV2.Y * TileMap->TileSideInPixels + 1};
        v2 OutlineMax = OutlineMin + v2{TileMap->TileSideInPixels - 2, TileMap->TileSideInPixels - 2};
        DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, color{0.9, 0.9, 0.9, 0.5}, color{});
        s32 OneDimensionalTileIndex = TileAsV2.Y * TileMap->NumCols + TileAsV2.X;

        if((GlobalMouse->Secondary.EndedDown) &&
           (GlobalMouse->Secondary.HalfTransitionCount == 1) &&
           (EditorState->HeldTileType == nullptr) &&
           (AlreadyClickedSecondary == false))
        {
            *(TileMap->TileValues + OneDimensionalTileIndex) = 0;
        }

        else if((GlobalMouse->Primary.EndedDown) && 
           (GlobalMouse->Primary.HalfTransitionCount == 1) && 
           (EditorState->HeldTileType != nullptr))
        {
            mem_idx NthTileValue = EditorState->HeldTileType - TileMap->TileTypes;
            *(TileMap->TileValues + OneDimensionalTileIndex) = NthTileValue;
        }
    }


    if(DebugState->EditorState.PrintRowsCols)
    {
        DEBUGPrintRowsCols(Backbuf, &DebugState->DebugTextArena, TileMap);
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
    GlobalMouse = &Input->Mouse;

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
        }
        Player->Position.X = Buffer->Width / 2;
        Player->Position.Y = Buffer->Height / 2;
        Player->IsFacing = EAST;

// Bagel
        // TODO(AARON): ScaleAndBlitBitmap called with Bitmap == nullptr draw something other than bagel
        //      so we know when we called it with nullptr
        GlobalBagel = PushStruct(&DebugState->FailBitmapsArena, meta_bitmap);
        char *BagelFilepath = "bagel1.bmp";
        snprintf(GlobalBagel->Filepath, sizeof(GlobalBagel->Filepath), "%s", BagelFilepath);
        LoadBitmap(&DebugState->FailBitmapsArena, GlobalBagel);
        
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
            f32 PlayerSpeed = 200.0f;
            
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
        // Ensure HeldTileType is always null before the tile editor initially opens
        DebugState->EditorState.HeldTileType = nullptr;
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

    if(DebugState->EditorState.WhichEditor == TILE)
    {
        DrawTileEditor(Buffer, ScratchHeader, DebugState, TileMap);
    }
    else if(DebugState->EditorState.WhichEditor == PLAYER)
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

    // MoveDown is the "S" key
    // I think we don't need to check half transition count for the keys with these save/load commands because subsequent EndedDown
    //      messages are routed to the message loop for the save/load dialog, not our usual
    //      message loop in the platform layer, and we have code in the platform layer to
    //      ensure that these are ignored (AS, 9/22)
    if(GlobalKeyboardController->MoveDown.EndedDown && GlobalDevKeys->Ctrl.EndedDown)
    {
        SaveProject(TileMap, Player, ScratchHeader);
    }

    // ActionRight is the "L" key
    if(GlobalKeyboardController->ActionRight.EndedDown && GlobalDevKeys->Ctrl.EndedDown)
    {
        b32 LoadResult = LoadTileMapFromFile(TileMap, ScratchHeader);
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

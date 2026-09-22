#include "tufty.h"
#include "stb_easy_font.h"

#define SCRATCH_SIZE Megabytes(1)

// DEBUG_PLATFORM_GET_FILE_SIZE(name) u32 name(char *Filename)
// DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory)
// DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename)
// DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(char *Filename, u32 MemorySize, void *Memory)
// DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename)
// DEBUG_PLATFORM_GET_DIR_WRITE_TIME(name) u64 name(char *Dirname)
// DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(buffer *GamePackedFilenames, char *DirName, int *NumFilesFound)
//
// returns either the number of bytes read, or 0 if the file was missing, too big, or locked
// DEBUG_PLATFORM_READ_FILE_INTO(name) u32 name(char *Filename, u32 DestSize, void *Dest)
//
// IsSave = true if saving, false if loading
// #define DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(name) int name(char *Dest, int DestSize, b32 IsSave)
//

// GLOBALS
    // variables
    static tile_type *GlobalBagel;
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

enum
{
    TILE_EMPTY = 0,
    DANDELION,
    PUFF,
    PATH,
    LADYBUG,
    APHID,
};

struct vertex
{
    f32 X, Y, Z;
    u32 Color;
};

struct quad
{
    vertex TopLeft, TopRight, BottomRight, BottomLeft;
};

void
ZeroArena(arena *Arena)
{
    memset(Arena->Data, 0, Arena->Size);
}

void
ResetArena(arena *Arena)
{
    ZeroArena(Arena);
    Arena->Cursor = 0;
}

scratch_arena *
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

void
FreeScratchArena(scratch_arena *Scratch)
{
    Scratch->IsFree = true;
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

static void
DEBUGReloadBitmapIfChanged(tile_type *TileType)
{
    // Wait one frame after bitmap change detected so we don't try to load it
    //      while the save is in progress.
    bitmap *Bitmap = &TileType->Bitmap;
    char *Filepath = TileType->Filepath;
    if(Bitmap->ReadyToRead == true)
    {
        Bitmap->ReadyToRead = false;
        mem_idx BytesRead = ReadFileInto(Filepath, (u32)Bitmap->Buffer.Size, Bitmap->Buffer.Data);
        if(BytesRead == Bitmap->Buffer.Size)
        {
            ParseBitmapHeader(Bitmap);
            Bitmap->LastWriteTime = GetFileWriteTime(Filepath);
        }
    }
    else
    {
        u64 WriteTime = GetFileWriteTime(Filepath);
        if(WriteTime && (WriteTime != Bitmap->LastWriteTime))
        {
            Bitmap->ReadyToRead = true;
            Bitmap->LastWriteTime = WriteTime;
        }
    }
}

static void
LoadBitmap(arena *Arena, tile_type *TileType)
{
    bitmap *Bitmap = &TileType->Bitmap;
    char *Filepath = TileType->Filepath;
    // If we can't get the file size, then we can't load the file, 
    //      so we want the file size in the bitmap to be 0.
    Bitmap->Buffer.Size = GetFileSize(Filepath);
    if(Bitmap->Buffer.Size == 0)
    {
        return;
    }
    Bitmap->Buffer.Data = PushArray(Arena, u8, Bitmap->Buffer.Size);
    Bitmap->ReadyToRead = true;
    DEBUGReloadBitmapIfChanged(TileType);
}

void
LoadTileBitmapsDir(tile_map *TileMap, random_series *RandomSeries,
                    scratch_header *ScratchHeader)
{
    // TODO: Use CPP class constructor destructor setup to always 
    //      FreeScratchArena whenever any scope containing a 
    //      GetScratchArena call is exited
    arena *TilesArena = &TileMap->TilesArena;
    ResetArena(TilesArena);
    tile_type *TileTypes = TileMap->TileTypes;
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    
    // Clear bitmap structs in tile types array
    for(int TypeIdx = 0;
        TypeIdx < TILE_ARRAY_LEN;
        ++TypeIdx)
    {
        TileTypes[TypeIdx].Bitmap = {};
    }

    buffer FoundFilenames;
    FoundFilenames.Size = MAX_TILE_TYPES * STRING_LEN;
    FoundFilenames.Data = PushArray(&Scratch->Arena, u8, FoundFilenames.Size);
    int NumFilesFound = 0;
    GetListOfDirContents(&FoundFilenames, "tiles", &NumFilesFound);
    if(NumFilesFound > MAX_TILE_TYPES)
    {
        Assert(!"Tiles dir contains more than 200 tile types. 200 is the max.");
    }

    // Load the bitmap for every .bmp filepath found in the tiles/ dir
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
            tile_type *TileType = TileTypes + NthSlot;
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

        // If the bitmap wasn't in the game previously, we pick the first available ID slot.
        //      We have to copy the contents of the FilepathToLoad array to the new ID's Filepath field.
        if(ExistingTileType == false)
        {
            Assert(strnlen(FilepathToLoad, STRING_LEN) < STRING_LEN);
            tile_type *SlotForNewBitmap = TileTypes + FirstAvailable;
            snprintf(SlotForNewBitmap->Filepath, STRING_LEN, "%s", FilepathToLoad);
            LoadBitmap(TilesArena, SlotForNewBitmap);
        }

        while(*ThisFilename)
        {
            ++ThisFilename;
        }
        ++ThisFilename;
    }

        // Do some cleanup. Find all instances of IDs that do have filepaths, but have no bitmap pointers.
        //      These are tile bitmaps that were removed from the game. We want them to be
        //      drawn as a bagel instead

    TileMap->NumTileTypes = 0;
    for(int Slot = 1;
        Slot < TILE_ARRAY_LEN;
        ++Slot)
    {
        tile_type *TileType = TileTypes + Slot;
        bitmap *Bitmap = &TileType->Bitmap;
        if(TileType->Filepath[0]) // If the ID has a filepath
        {
            // Then we count it as a TileType
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


void
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

void
DrawSpecialRect(game_offscreen_buffer *Buf,
                v2 Min, v2 Max,
                f32 R, f32 G, f32 B, b32 Outline = false, f32 Alpha = 0.5f)
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

    u32 Blue = RoundF32ToU32(B * 255.0f);
    u32 Green = RoundF32ToU32(G * 255.0f);
    u32 Red = RoundF32ToU32(R * 255.0f);

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
            if( ((X == MinX) && Outline) ||
                ((Y == MinY) && Outline) ||
                ((X == MaxX-1) && Outline) ||
                ((Y == MaxY-1) && Outline))
            {
                *(u32 *)Pixel = 0;                
            }
            else
            {
                // Blue
                Pixel[0] = LerpS32(Pixel[0], Blue, Alpha);

                // Green
                Pixel[1] = LerpS32(Pixel[1], Green, Alpha);
                
                // Red
                Pixel[2] = LerpS32(Pixel[2], Red, Alpha);

            }
            Pixel += sizeof(u32);
        }
        Row += Buf->Pitch;
    }
}

void
DEBUGDrawText(game_offscreen_buffer *Backbuf,
              f32 X, f32 Y,
              char *StringText, arena *DebugTextArena,
              f32 R, f32 G, f32 B,
              f32 Alpha = 1.0f,
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
        if(Alpha == 1.0f)
        {
            DrawSimpleRect(Backbuf, Min, Max, R, G, B);
        }
        else
        {
            DrawSpecialRect(Backbuf, Min, Max, R, G, B, false, Alpha);
        }
    }
}

void
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
    DEBUGDrawText(Backbuf, (Backbuf->Width * 0.9f), 30, Temp, DebugTextArena, 0.9f, 0.2f, 0.5f);
#undef FPS_SNAPS
}

void
DEBUGPrintRowsCols(game_offscreen_buffer *Backbuf, arena *DebugTextArena, tile_map *TileMap)
{
    int OffsetFromGridX = 15;
    int OffsetFromGridY = 15;
    // 0, 0
    DEBUGDrawText(Backbuf, OffsetFromGridX, OffsetFromGridY, "0", DebugTextArena, 0.9f, 0.2f, 0.5f);

    // Cols
    for(int Col = 1;
        Col < TileMap->NumCols;
        ++Col)
    {
        int PrintX = Col * TileMap->TileSideInPixels + OffsetFromGridX; 
        char Temp[3];
        snprintf(Temp, 3, "%d", Col);
        DEBUGDrawText(Backbuf, PrintX, OffsetFromGridY, Temp, DebugTextArena, 0.9f, 0.2f, 0.5f);
    }

    // Rows
    for(int Row = 1;
        Row < TileMap->NumRows;
        ++Row)
    {
        int PrintY = Row * TileMap->TileSideInPixels + OffsetFromGridY; 
        char Temp[3];
        snprintf(Temp, 3, "%d", Row);
        DEBUGDrawText(Backbuf, OffsetFromGridX, PrintY, Temp, DebugTextArena, 0.9f, 0.2f, 0.5f);
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
        Bitmap = &GlobalBagel->Bitmap;
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

static b32
SaveTileMap(tile_map *TileMap, scratch_header *ScratchHeader)
{
    b32 Success = false;
    if(TileMap->NumTileTypes == 0)
    {
        return(Success);
    }

    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    serialized_tile_map *ToWrite = PushStruct(&Scratch->Arena, serialized_tile_map);

    mem_idx BytesToWrite = sizeof(serialized_tile_map);
    char *MagicNumber = "TILE";
    mem_idx MagicNumberSize = strnlen(MagicNumber, STRING_LEN);
    Assert(MagicNumberSize == 4);

    for(int NthLetter = 0;
        NthLetter < MagicNumberSize;
        ++NthLetter)
    {
        ToWrite->MagicNumber[NthLetter] = MagicNumber[NthLetter];
    }

    ToWrite->NumRows = TileMap->NumRows;
    ToWrite->NumCols = TileMap->NumCols;
    ToWrite->NumTileTypes = TileMap->NumTileTypes;
    ToWrite->TileTypeFilepathsOffset = sizeof(serialized_tile_map);

    // We initially set this to the same as TileTypeFilepathsOffset but continually increment it as
    //      we write more filepaths to the file
    ToWrite->TileValuesOffset = sizeof(serialized_tile_map);

    for(int TypeIdx = 1;
        TypeIdx <= ToWrite->NumTileTypes;
        ++TypeIdx)
    {
        char *FilepathToWrite = TileMap->TileTypes[TypeIdx].Filepath;
        mem_idx FilepathBytesNeeded = strnlen(FilepathToWrite, STRING_LEN);

        // For null terminator
        FilepathBytesNeeded += 1;

        char *WriteFilepathHere = PushArray(&Scratch->Arena, char, FilepathBytesNeeded);
        snprintf(WriteFilepathHere, FilepathBytesNeeded, "%s", FilepathToWrite);
        ToWrite->TileValuesOffset += FilepathBytesNeeded;
        BytesToWrite += FilepathBytesNeeded;
    }

    s32 *WriteTileValuesHere = PushArray(&Scratch->Arena, s32, TileMap->NumTilesInWorld);
    for(int ValueIdx = 0;
        ValueIdx < TileMap->NumTilesInWorld;
        ++ValueIdx)
    {
        WriteTileValuesHere[ValueIdx] = TileMap->TileValues[ValueIdx];
        BytesToWrite += sizeof(s32);
    }

    mem_idx MaxPath = 260;
    char *Filepath = PushArray(&Scratch->Arena, char, MaxPath);
    int GetFilepathResult = GetFilepathFromDialog(Filepath, MaxPath, true);

    if(GetFilepathResult)
    {
        Success = WriteEntireFile(Filepath, BytesToWrite, Scratch->Arena.Data);
    }

    FreeScratchArena(Scratch);
    return(Success);
}

static b32
LoadTileMapFromFile(tile_map *TileMap, scratch_header *ScratchHeader)
{
    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    arena *TilesArena = &TileMap->TilesArena;

    b32 Result = false;

    serialized_tile_map *Header = 0;
    char *TileTypeFilepaths = 0;
    s32 *LoadedTileValues = 0;

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
    if(ReadFileInto(Filepath, Size, LoadedTileMap))
    {
        Header = (serialized_tile_map *)LoadedTileMap;
        char *CompareMagicNumber = "TILE";
        for(int LetterIdx = 0;
            LetterIdx < 4;
            ++LetterIdx)
        {
            Assert(CompareMagicNumber[LetterIdx] == Header->MagicNumber[LetterIdx]);
        }
        Assert(Header->NumRows);
        TileMap->NumRows = Header->NumRows;
        Assert(Header->NumCols);
        TileMap->NumCols = Header->NumCols;
        Assert(Header->NumTileTypes);
        TileMap->NumTileTypes = Header->NumTileTypes;
        TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
        TileTypeFilepaths = (char *)(LoadedTileMap + Header->TileTypeFilepathsOffset);
        LoadedTileValues = (s32 *)(LoadedTileMap + Header->TileValuesOffset);
    }

    ResetArena(TilesArena);
    tile_type *TileTypes = TileMap->TileTypes;
    for(int TypeIdx = 0;
        TypeIdx < TILE_ARRAY_LEN;
        ++TypeIdx)
    {
        memset(TileTypes[TypeIdx].Filepath, 0, STRING_LEN);
        TileTypes[TypeIdx].Bitmap = {};
    }

    char *FilepathCursor = TileTypeFilepaths;
    for(int NthType = 1;
        NthType <= Header->NumTileTypes;
        ++NthType)
    {
        tile_type *ThisType = TileTypes + NthType;
        mem_idx FilepathLen = strnlen(FilepathCursor, STRING_LEN);

        FilepathLen += 1;
        snprintf(ThisType->Filepath, FilepathLen, "%s", FilepathCursor);
        FilepathCursor += FilepathLen;

        LoadBitmap(TilesArena, ThisType);
    }

    for(int ValueIdx = 0;
        ValueIdx < TileMap->NumTilesInWorld;
        ++ValueIdx)
    {
        TileMap->TileValues[ValueIdx] = LoadedTileValues[ValueIdx];
    }
    // memcpy(TileMap->TileValues, LoadedTileValues, sizeof(s32) * TileMap->NumTilesInWorld);

    FreeScratchArena(Scratch);

    return(true);
}


static void
DrawEditor(game_offscreen_buffer *Backbuf, scratch_header *ScratchHeader, debug_state *DebugState, tile_map *TileMap)
{
    struct menu_tile
    {
        v2 TileMin;
        v2 TileMax;
        tile_type *TileType;
    };
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
    DrawSpecialRect(Backbuf, BrowserMin, BrowserMax, 0.5f, 0.5f, 0.5f, true, 0.85f);

    DEBUGDrawText(Backbuf, BrowserMin.X + 120, 80, "Tile Menu", &DebugState->DebugTextArena, 1, 1, 1, 1, 3.5);

    // Draw tiles in menu unless there are no tiles
    if(TileMap->NumTileTypes > 0)
    {
        Scratch = GetScratchArena(ScratchHeader);
        MenuTiles = PushArray(&Scratch->Arena, menu_tile, TileMap->NumTileTypes);
        tile_type *NextTileType = TileMap->TileTypes + 1;

        for(int NthTileType = 0;
            NthTileType < TileMap->NumTileTypes;
            ++NthTileType)
        {
            // scan for the next tile type that does not have an empty filepath
            while(NextTileType->Filepath[0] == 0)
            {
                ++NextTileType;
            }
            tile_type *ThisTileType = NextTileType;
            NextTileType += 1;

            menu_tile *ThisMenuTile = MenuTiles + NthTileType;
            ThisMenuTile->TileMin = {X, Y};
            ThisMenuTile->TileMax = ThisMenuTile->TileMin + (v2){TileMap->TileSideInPixels, TileMap->TileSideInPixels};
            ThisMenuTile->TileType = ThisTileType;

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
            ScaleAndBlitBitmap(Backbuf, ThisMenuTile->TileMin, ThisMenuTile->TileMax, &ThisMenuTile->TileType->Bitmap);

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
                        (ThisMenuTile->TileType->Bitmap.Pixels == GlobalBagel->Bitmap.Pixels))
                    {
                        tile_type *TileTypeToReplace = ThisMenuTile->TileType;
                        mem_idx TileTypeToReplaceIdx = (ThisMenuTile->TileType - TileMap->TileTypes);
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
                        EditorState->HeldTileType = ThisMenuTile->TileType;
                    }
                }
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, 0.75, 0.75, 0.75, true);
            }
            else
            {
                DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, 0, 0, 0, true, 0);
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
        DrawSpecialRect(Backbuf, TinyTileMin, TinyTileMax, 0, 0, 0, true, 0);
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
        v2 OutlineMin = {TileAsV2.X * TileMap->TileSideInPixels, TileAsV2.Y * TileMap->TileSideInPixels};
        v2 OutlineMax = OutlineMin + (v2){TileMap->TileSideInPixels, TileMap->TileSideInPixels};
        DrawSpecialRect(Backbuf, OutlineMin, OutlineMax, 0.75, 0.75, 0.75, true);
        s32 OneDimensionalTileIndex = TileAsV2.Y * TileMap->NumCols + TileAsV2.X;

        if((GlobalMouse->Secondary.EndedDown) &&
           (GlobalMouse->Secondary.HalfTransitionCount == 1) &&
           (EditorState->HeldTileType == nullptr) &&
           (AlreadyClickedSecondary == false))
        {
            *(TileMap->TileValues + OneDimensionalTileIndex) = TILE_EMPTY;
        }

        else if((GlobalMouse->Primary.EndedDown) && 
           (GlobalMouse->Primary.HalfTransitionCount == 1) && 
           (EditorState->HeldTileType != nullptr))
        {
            mem_idx NthTileValue = EditorState->HeldTileType - TileMap->TileTypes;
            *(TileMap->TileValues + OneDimensionalTileIndex) = NthTileValue;
        }
    }

    // MoveDown is the "S" key
    // I think we don't need to check half transition count for the keys with these save/load commands because subsequent EndedDown
    //      messages are routed to the message loop for the save/load dialog, not our usual
    //      message loop in the platform layer, and we have code in the platform layer to
    //      ensure that these are ignored (AS, 9/22)
    if(GlobalKeyboardController->MoveDown.EndedDown && GlobalDevKeys->Ctrl.EndedDown)
    {
        b32 WriteResult = SaveTileMap(TileMap, ScratchHeader);
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

    if(!Memory->IsInitialized)
    {
        // Random
        GameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

// Arenas
        // Debug
        InitializeArena(&DebugState->DebugTextArena, (DebugRegion.Data + sizeof(debug_state)), Megabytes(1));
        InitializeArena(&DebugState->FailBitmapsArena, (DebugRegion.Data + sizeof(debug_state) + DebugState->DebugTextArena.Size), Megabytes(1));
        DebugState->EditorState.TilesReadyToReload = true;

        // Tiles
        InitializeArena(&GameState->TileMap.TilesArena, (GameRegion.Data + sizeof(game_state)), Megabytes(4) );

        // World
        InitializeArena(&GameState->WorldArena, 
                        (GameRegion.Data + sizeof(game_state) + GameState->TileMap.TilesArena.Size),
                        (GameRegion.Size - sizeof(game_state) - GameState->TileMap.TilesArena.Size) );

        Assert(GameRegion.Size == sizeof(game_state) + GameState->TileMap.TilesArena.Size + GameState->WorldArena.Size);

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
        TileMap->TileSideInPixels = 64.0f;
        TileMap->NumRows = CeilingF32ToS32((f32)Buffer->Height / TileMap->TileSideInPixels);
        TileMap->NumCols = CeilingF32ToS32((f32)Buffer->Width / TileMap->TileSideInPixels);
        mem_idx TileArraySize = sizeof(tile_type) * TILE_ARRAY_LEN;
        memset(TileMap->TileTypes, 0, TileArraySize);
        TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
        TileMap->TileValues = PushArray(WorldArena, s32, TileMap->NumTilesInWorld); 

        // // TODO(Aaron): Load default bitmap if one failed that indicates obvious failure
        // char *TempPlayerBitmapFilepaths[] = {"player\\16x16faceright.bmp", 
        //                                 "player\\16x16behindview.bmp", 
        //                                 "player\\16x16faceleft.bmp", 
        //                                 "player\\16x16frontview.bmp"};
        // for(int PlayerBitmapIdx = 0;
        //     PlayerBitmapIdx < 4;
        //     ++PlayerBitmapIdx)
        // {
        //     bitmap *It = &GameState->PlayerBitmaps[PlayerBitmapIdx];
        //     It->ReadyToRead = true;
        //     // Per tufty.h enum order is East = 0, North = 1, West = 2, South = 3
        //     PushBitmapToArena(WorldArena, TempPlayerBitmapFilepaths[PlayerBitmapIdx], It);
        // }

        LoadTileBitmapsDir(TileMap, &GameState->RandomSeries, ScratchHeader);
        DebugState->EditorState.LastTileDirUpdate = GetDirWriteTime("tiles");
        DebugState->EditorState.TilesReadyToReload = false;


// Player
        GameState->PlayerP.X = Buffer->Width / 2;
        GameState->PlayerP.Y = Buffer->Height / 2;
        GameState->PlayerFacing = EAST;

// Bagel
        GlobalBagel = PushStruct(&DebugState->FailBitmapsArena, tile_type);
        char *BagelFilepath = "bagel1.bmp";
        snprintf(GlobalBagel->Filepath, sizeof(GlobalBagel->Filepath), "%s", BagelFilepath);
        LoadBitmap(&DebugState->FailBitmapsArena, GlobalBagel);
        
        Memory->IsInitialized = true;
    }
    
    ///////////////////////////////////////// INIT END, MAIN LOOP START ////////////////////////////////////////////

    DebugState->DebugTextArena.Cursor = 0;

    // Check if new tile bitmaps added; if so unload and reload all bitmaps.
    // Wait one frame after bitmap change detected so we don't try to load it
    //      while the save is in progress.
    if(DebugState->EditorState.TilesReadyToReload == true)
    {
        DebugState->EditorState.TilesReadyToReload = false;
        LoadTileBitmapsDir(TileMap, &GameState->RandomSeries, ScratchHeader);
    }
    else
    {
        u64 CheckUpdateTime = GetDirWriteTime("tiles");
        if(DebugState->EditorState.LastTileDirUpdate != CheckUpdateTime)
        {
            DebugState->EditorState.LastTileDirUpdate = CheckUpdateTime;
            DebugState->EditorState.TilesReadyToReload = true;
        }
    }

    // Hot reload tile bitmaps but only do so if ReadyToReload is false; i.e., 
    //      if we're going to reload all of the tiles on the next frame
    if(DebugState->EditorState.TilesReadyToReload == false)
    {
        for(int TileIdx = 1;
            TileIdx < TILE_ARRAY_LEN;
            ++TileIdx)
        {
            tile_type *TileType = TileMap->TileTypes + TileIdx;
            if(TileType->Filepath[0])
            {
                DEBUGReloadBitmapIfChanged(TileType);
            }
        }
    }

    // hot reload player bitmaps
    // for(int PlayerBitmapIdx = 0;
    //     PlayerBitmapIdx < 4;
    //     ++PlayerBitmapIdx)
    // {
    //     DEBUGReloadBitmapIfChanged(&GameState->PlayerBitmaps[PlayerBitmapIdx]);
    // }
    //


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
                GameState->PlayerFacing = EAST;
            }
            if(Controller->MoveUp.EndedDown)
            {
                dPlayer.Y -= 1.0f;
                GameState->PlayerFacing = NORTH;
            }
            if(Controller->MoveLeft.EndedDown)
            {
                dPlayer.X -= 1.0f;
                GameState->PlayerFacing = WEST;
            }
            if(Controller->MoveDown.EndedDown && !Input->DevKeys.Ctrl.EndedDown)
            {
                // NOTE(AARON): The above check for the ctrl key may be incorrect but
                //      we won't know until we implement player movement again
                dPlayer.Y += 1.0f;
                GameState->PlayerFacing = SOUTH;
            }
            if((dPlayer.X != 0) && (dPlayer.Y != 0))
            {
                dPlayer.X *= 0.707106781187f;
                dPlayer.Y *= 0.707106781187f;
            }
            f32 PlayerSpeed = 200.0f;
            
            v2 NewPlayerP = GameState->PlayerP;
            NewPlayerP.X += dPlayer.X * PlayerSpeed * Input->dtForFrame;
            NewPlayerP.Y += dPlayer.Y * PlayerSpeed * Input->dtForFrame;

            GameState->PlayerP = NewPlayerP;
        }
    }

    if(GlobalDevKeys->F1.EndedDown && GlobalDevKeys->F1.HalfTransitionCount == 1)
    {
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
                tile_type *TileType = TileMap->TileTypes + TileValue;
                bitmap *TileBitmap = &TileMap->TileTypes[TileValue].Bitmap;
                ScaleAndBlitBitmap(Buffer, TileMin, TileMax, TileBitmap);
            }
            if(DebugState->EditorState.WhichEditor == TILE)
            {
                DrawSpecialRect(Buffer, TileMin, TileMax, 0, 0, 0, true, 0);
            }
        }
    }

    if(DebugState->EditorState.WhichEditor == TILE)
    {
        DrawEditor(Buffer, ScratchHeader, DebugState, TileMap);
    }

    // else
    // {
    //     // Player
    //     v2 PlayerMin = {GameState->PlayerP.X - (PLAYER_WIDTH * 0.5f), 
    //         GameState->PlayerP.Y - PLAYER_HEIGHT};
    //     v2 PlayerMax = {GameState->PlayerP.X + (PLAYER_WIDTH * 0.5f), 
    //         GameState->PlayerP.Y};
    //
    //     // TODO(Aaron): doing it once per frame is bound to be very slow, and it seems like i can detect slightly jittery animation
    //     //      in the game when moving character around. test this.
    //     ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, &GameState->PlayerBitmaps[GameState->PlayerFacing]);
    // }

    DEBUGPrintFps(Buffer, Input->Fps, &DebugState->DebugTextArena);

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

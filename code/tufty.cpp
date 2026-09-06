#include "tufty.h"
#include "stb_easy_font.h"

#define PLAYER_HEIGHT 16.0f
#define PLAYER_WIDTH  16.0f
#define MAX_TILE_TYPES 200
// Assume average filename length of 20
#define TILE_FILENAME_BUFSIZE 200 * 20


enum
{
    TILE_INVALID = 0,
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

// void
// RefreshTiles(buffer *TileFilenameList, u32 *NumTileTypes)
// {
//     u8 NewTileFilenameList[TILE_FILENAME_BUFSIZE];
//
//     u32 NewTileFilenameCount = 0;
//     PlatformGetListOfDirContents(&NewTileFilenameList, TILE_FILENAME_BUFSIZE, "tiles", &NewTileFilenameCount);
//
//
//
//
//
//
//
//
//
//
// }


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
            if((Y == MinY) && Outline)
            {
                *(u32 *)Pixel = 0;
            }
            else if((X == MinX) && Outline)
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
              char *StringText, buffer RasterizedTextBuf,
              f32 R, f32 G, f32 B,
              f32 Alpha = 1.0f)
{
    int NumQuads = stb_easy_font_print(0, 0, StringText, NULL, RasterizedTextBuf.Start, RasterizedTextBuf.Size);
    for(int QuadIdx = 0;
        QuadIdx < NumQuads;
        ++QuadIdx)
    {
        quad *ThisQuad = (quad *)(RasterizedTextBuf.Start + QuadIdx * sizeof(quad));
        v2 Min = {(X + ThisQuad->TopLeft.X*2.5f), (Y + ThisQuad->TopLeft.Y*2.5f)};
        v2 Max = {(X + ThisQuad->BottomRight.X*2.5f), (Y + ThisQuad->BottomRight.Y*2.5f)};
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
DEBUGPrintFps(game_offscreen_buffer *Backbuf, f32 NewFpsReading, buffer DebugTextBuf)
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
    DEBUGDrawText(Backbuf, (Backbuf->Width * 0.9f), 30, Temp, DebugTextBuf, 0.9f, 0.2f, 0.5f);
#undef FPS_SNAPS
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
        u32 SrcRowIdx = TruncateF32ToU32(YCoef*((f32)(Y-ScreenMinY)+0.5f));
        u32 SrcRow = Bitmap->Height - 1 - SrcRowIdx;
        u8 *DestPixel = DestRow;
        for(int X = SampledScreenMinX; X < SampledScreenMaxX; ++X)
        {
            u32 SrcCol = TruncateF32ToU32(XCoef*((f32)(X-ScreenMinX)+0.5f));
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
ParseBitmapHeader(bitmap *Bitmap)
{
    bitmap_header *Header = (bitmap_header *)Bitmap->Buffer.Start;
    Bitmap->Pixels = Bitmap->Buffer.Start + Header->DataOffset;
    Bitmap->Height = Header->Height;
    Bitmap->Width = Header->Width;
    Assert(Header->BitsPerPixel == 32);
    Bitmap->BytesPerPixel = Header->BitsPerPixel / 8;
    Bitmap->Pitch = Bitmap->Width * Bitmap->BytesPerPixel;
}

static void
DEBUGReloadBitmapIfChanged(bitmap *Bitmap, game_memory *Memory)
{
    // NOTE(Aaron): as of now, reloaded bitmap is REQUIRED to have the same dimensions as the old one,
    u64 WriteTime = Memory->DEBUGPlatformGetFileWriteTime(Bitmap->Filename);
    if(WriteTime && (WriteTime != Bitmap->LastWriteTime))
    {
        Bitmap->Buffer.Size = Memory->DEBUGPlatformReadFileInto(Bitmap->Filename,
                                             (u32)Bitmap->Buffer.Size, Bitmap->Buffer.Start);
        if(Bitmap->Buffer.Size)
        {
            ParseBitmapHeader(Bitmap);
            Bitmap->LastWriteTime = WriteTime;
        }
    }
}

static void
PushBitmapToArena(arena *Arena, char *Filename, bitmap *FillThisOut, game_memory *Memory)
{
    FillThisOut->Filename = Filename;
    FillThisOut->Buffer.Size = Memory->DEBUGPlatformGetFileSize(Filename);
    FillThisOut->Buffer.Start = PushArray(Arena, u8, FillThisOut->Buffer.Size);
    DEBUGReloadBitmapIfChanged(FillThisOut, Memory);
}

// #define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    mem_idx DebugStateNumBytes = Megabytes(16);
    mem_idx GameStateNumBytes = Memory->PermanentStorageSize - DebugStateNumBytes;

    debug_state *DebugState = (debug_state *)Memory->PermanentStorage;
    game_state *GameState = (game_state *)((u8 *)Memory->PermanentStorage + DebugStateNumBytes);

    if(!Memory->IsInitialized)
    {
        InitializeArena(&DebugState->DebugArena,
                        ((u8 *)Memory->PermanentStorage + sizeof(debug_state)),
                        (DebugStateNumBytes - sizeof(debug_state)));

        DebugState->DebugTextBuf.Start = PushArray(&DebugState->DebugArena, u8, Megabytes(2));
        DebugState->DebugTextBuf.Size = Megabytes(2);
        
        InitializeArena(&GameState->WorldArena, 
                        ((u8 *)Memory->PermanentStorage + DebugStateNumBytes + sizeof(game_state)),
                        (GameStateNumBytes - sizeof(game_state)));

        arena *WorldArena = &GameState->WorldArena;
        Assert((sizeof(debug_state) + DebugState->DebugArena.Size + sizeof(game_state) + WorldArena->Size) == Memory->PermanentStorageSize);

// Player
        GameState->PlayerP.X = Buffer->Width / 2;
        GameState->PlayerP.Y = Buffer->Height / 2;
        GameState->PlayerFacing = EAST;

        // TODO(Aaron): Load default bitmap if one failed that indicates obvious failure
        PushBitmapToArena(WorldArena, "16x16faceright.bmp", &GameState->PlayerBitmaps[EAST], Memory);
        PushBitmapToArena(WorldArena, "16x16behindview.bmp", &GameState->PlayerBitmaps[NORTH], Memory);
        PushBitmapToArena(WorldArena, "16x16faceleft.bmp", &GameState->PlayerBitmaps[WEST], Memory);
        PushBitmapToArena(WorldArena, "16x16frontview.bmp", &GameState->PlayerBitmaps[SOUTH], Memory);

// Random
        GameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

// Tiles        
        // How we are doing this.
        //      While we are making the game, we want to be able to add tiles to the tufty/data/tiles/ dir
        //          as it's running, and have the game dynamically load them in so that we can place them
        //          using the tile editor.
        //
        //      Our process is as follows:
        //          On init, we allocate a buffer in the Debug arena to store the list of the filenames
        //              currently in the tiles/ directory. 
        //          We call "GetListOfDirContents" to fill out this buffer.
        //          Assumptions are that we have no more than 200 tile types in the game, and that
        //              filenames for these tiles have an average length of 20 characters.
        //          GetListOfDirContents fills the buffer with buffer structs containing the filenames
        //              (note buffer struct just contains a u8 pointer and the number of bytes in the buffer),
        //              and tells us how many files it found.
        //          Then we visit each of those filenames in the buffer and load them into the TileBitmaps array.
        //
        //          While the game is running, once per frame, we call the GetListOfDirContents function
        //              and determine whether the list is the same. We do this naively: we just check to see
        //              if the contents of the list is byte-for-byte the same. If not, we zero the list of tiles
        //              and the buffer of bitmaps for the tiles and reload them. This is inefficient, but
        //              in a shipping game we would know the exact list of tiles and won't have to do this.
        //
        tile_map *TileMap = &GameState->TileMap;
        buffer *TilesList = &DebugState->ListOfFilesInTilesDir;

        TilesList->Size = MAX_TILE_TYPES * 20;
        TilesList->Start = PushArray(&DebugState->DebugArena, u8, TilesList->Size);
        

        Memory->DEBUGPlatformGetListOfDirContents(TilesList->Start, TilesList->Size, "tiles", &TileMap->NumTileTypes);
        TileMap->TileFilenames = &DebugState->ListOfFilesInTilesDir;

        TileMap->TileBitmaps = PushArray(WorldArena, bitmap, TileMap->NumTileTypes);
        // for(int BitmapIdx = 0;
        //     BitmapIdx < TileMap->NumTileTypes;
        //     ++BitmapIdx)
        // {
        //     PushBitmapToArena(WorldArena, TileMap->TileFilenames[BitmapIdx], TileMap->TileBitmaps + BitmapIdx, Memory);
        // }

        // TileMap dimensions
        TileMap->TileDim = 96.0f;
        TileMap->TileRows = (Buffer->Height / TileMap->TileDim) + 1;
        TileMap->TileCols = (Buffer->Width / TileMap->TileDim) + 1;

        TileMap->NumTilesInWorld = TileMap->TileRows * TileMap->TileCols;
        TileMap->TileValues = PushArray(WorldArena, s32, TileMap->NumTilesInWorld); 
        for(int TileIdx = 0;
            TileIdx < TileMap->NumTilesInWorld;
            ++TileIdx)
        {
            TileMap->TileValues[TileIdx] = RandomS32InRange(&GameState->RandomSeries, 0, 4);
        }





        Memory->IsInitialized = true;
    }

    // hot reload player bitmaps
    for(int PlayerBitmapIdx = 0;
        PlayerBitmapIdx < 4;
        ++PlayerBitmapIdx)
    {
        DEBUGReloadBitmapIfChanged(&GameState->PlayerBitmaps[PlayerBitmapIdx], Memory);
    }

    // hot reload tile bitmaps
    for(int TileBitmapIdx = 0;
        TileBitmapIdx < GameState->TileMap.NumTileTypes;
        ++TileBitmapIdx)
    {
        DEBUGReloadBitmapIfChanged(&GameState->TileMap.TileBitmaps[TileBitmapIdx], Memory);
    }




    // What to do:
    //      1. Push a reasonably sized array to DebugArena: 4096 bytes
    //      2. Pass this to platform layer along with path to directory to read.
    //              Say platform layer and game layer share a data structure "buffer"
    //                  which contains a byte count and a u8 pointer.
    //      3. Platform layer reads the directory and fills out 
    //      3
    //
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
            if(Controller->MoveDown.EndedDown)
            {
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

    if(Input->FunctionKeys.F1.EndedDown && Input->FunctionKeys.F1.HalfTransitionCount == 1)
    {
        GameState->Editor = !GameState->Editor;
    }

    // Underlayer
    v2 ScreenMin = {0, 0};
    v2 ScreenMax = {(f32)Buffer->Width, (f32)Buffer->Height};
    DrawSimpleRect(Buffer, ScreenMin, ScreenMax, 0.1f, 0.5f, 0.45f);

    tile_map *TileMap = &GameState->TileMap;
    // Tiles
    for(int Row = 0; 
        Row < TileMap->TileRows;
        ++Row)
    {
        for(int Col = 0;
            Col < TileMap->TileCols; 
            ++Col)
        {
            s32 TileOneDimensionalIndex = Row * TileMap->TileCols + Col;
            s32 TileValue = TileMap->TileValues[TileOneDimensionalIndex];
            if(TileValue > 0)
            {
                v2 TileMin = {Col * TileMap->TileDim, Row * TileMap->TileDim};
                v2 TileMax = TileMin + (v2){TileMap->TileDim, TileMap->TileDim};
                bitmap *TileBitmap = TileMap->TileBitmaps + TileValue;
                ScaleAndBlitBitmap(Buffer, TileMin, TileMax, TileBitmap);
            }
        }
    }
    // Player
    v2 PlayerMin = {GameState->PlayerP.X - (PLAYER_WIDTH * 0.5f), 
                    GameState->PlayerP.Y - PLAYER_HEIGHT};
    v2 PlayerMax = {GameState->PlayerP.X + (PLAYER_WIDTH * 0.5f), 
                    GameState->PlayerP.Y};

    // TODO(Aaron): doing it once per frame is bound to be very slow, and it seems like i can detect slightly jittery animation
    //      in the game when moving character around. test this.
    ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, &GameState->PlayerBitmaps[GameState->PlayerFacing]);

    // Draw tile browser
    if(GameState->Editor)
    {
        v2 BrowserMin = {(f32)(Buffer->Width * 0.8f), 0};
        v2 BrowserMax = {(f32)(Buffer->Width), (f32)(Buffer->Height)};
        DrawSpecialRect(Buffer, BrowserMin, BrowserMax, 0.5f, 0.5f, 0.5f, true, 0.5f);
    }

    DEBUGPrintFps(Buffer, Input->Fps, DebugState->DebugTextBuf);
}



extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

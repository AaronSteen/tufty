#include "tufty.h"
#include "stb_easy_font.h"

#define PLAYER_HEIGHT 16.0f
#define PLAYER_WIDTH  16.0f

#define MAX_TILE_TYPES 200
#define AVG_TILE_FILENAME_LEN 30
#define SCRATCH_SIZE Kilobytes(10)

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

void
ZeroArena(arena *Arena)
{
    for(int Idx = 0;
        Idx < Arena->Size;
        ++Idx)
    {
        Arena->Start[Idx] = 0;
    }
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
            ZeroArena(&It->Arena);
            It->Arena.Cursor = 0;
        }
        return(It);
    }

    // If we got here there were no free scratches and we need to make more
    __debugbreak();
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
DEBUGReloadBitmapIfChanged(bitmap *Bitmap, game_memory *Memory)
{
    // NOTE(Aaron): as of now, reloaded bitmap is REQUIRED to have the same dimensions as the old one,
    u64 WriteTime = Memory->DEBUGPlatformGetFileWriteTime(Bitmap->Filepath);
    if(WriteTime && (WriteTime != Bitmap->LastWriteTime))
    {
        Bitmap->Buffer.Size = Memory->DEBUGPlatformReadFileInto(Bitmap->Filepath,
                                                                (u32)Bitmap->Buffer.Size, Bitmap->Buffer.Data);
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
    strncpy_s(FillThisOut->Filepath, Filename, strnlen(Filename, MAX_STRING_LEN));
    FillThisOut->Buffer.Size = Memory->DEBUGPlatformGetFileSize(Filename);
    FillThisOut->Buffer.Data = PushArray(Arena, u8, FillThisOut->Buffer.Size);
    DEBUGReloadBitmapIfChanged(FillThisOut, Memory);
}

void
LoadTileBitmaps(arena *TilesArena, tile_map *TileMap, 
                scratch_header *ScratchHeader,
                u64 *LastUpdateTime, game_memory *Memory)
{
    u64 CheckUpdateTime = Memory->DEBUGPlatformGetDirWriteTime("tiles");
    if(*LastUpdateTime == CheckUpdateTime)
    {
        return;
    }

    *LastUpdateTime = CheckUpdateTime;
    ResetArena(TilesArena);
    TileMap->Bitmaps = PushArray(TilesArena, bitmap, MAX_TILE_TYPES);

    scratch_arena *Scratch = GetScratchArena(ScratchHeader);
    buffer Filenames;
    Filenames.Size = MAX_TILE_TYPES * AVG_TILE_FILENAME_LEN;
    Filenames.Data = PushArray(&Scratch->Arena, u8, Filenames.Size);
    Memory->DEBUGPlatformGetListOfDirContents(&Filenames, "tiles", &TileMap->NumTileTypes);

    char *Filename = (char *)Filenames.Data;
    for(int TileIdx = 0;
        TileIdx < TileMap->NumTileTypes;
        ++TileIdx)
    {
        bitmap *It = TileMap->Bitmaps + TileIdx;
        char BitmapFilepath[MAX_STRING_LEN];
        snprintf(BitmapFilepath, sizeof(BitmapFilepath), "tiles\\%s", Filename);
        PushBitmapToArena(TilesArena, BitmapFilepath, It, Memory);

        while(*Filename)
        {
            ++Filename;
        }
        ++Filename;
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
              char *StringText, buffer TextQuadBuf,
              f32 R, f32 G, f32 B,
              f32 Alpha = 1.0f)
{
    int NumQuads = stb_easy_font_print(0, 0, StringText, NULL, TextQuadBuf.Data, TextQuadBuf.Size);
    for(int QuadIdx = 0;
        QuadIdx < NumQuads;
        ++QuadIdx)
    {
        quad *ThisQuad = (quad *)(TextQuadBuf.Data + QuadIdx * sizeof(quad));
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
        u32 SrcRowIdx = FloorF32ToU32(YCoef*((f32)(Y-ScreenMinY)+0.5f));
        u32 SrcRow = Bitmap->Height - 1 - SrcRowIdx;
        u8 *DestPixel = DestRow;
        for(int X = SampledScreenMinX; X < SampledScreenMaxX; ++X)
        {
            u32 SrcCol = FloorF32ToU32(XCoef*((f32)(X-ScreenMinX)+0.5f));
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

// Framebuffer is 1920 x 1080

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

    if(!Memory->IsInitialized)
    {
// Arenas
        /* Layout.
         *
         *      - PermanentStorage 67,108,864 bytes
         *
         *          - DebugRegion 16,777,216 bytes
         *              - struct DebugState 
         *              - arena DebugArena 16,777,128 bytes 
         *                  - buffer DebugTextBuf 4096 bytes
         *
         *          - GameRegion 50,331,648 bytes
         *              - struct GameState
         *              - arena TilesArena: (max 200 tiles): 110,000 + 3,290,800 with some padding = 4mb
         *                  Bitmap structs 550 bytes * 200 = 110,000
         *                  Bitmaps: 64 x 64, 16,454 bytes each * 200 = 3,290,800 bytes
         *              - arena WorldArena: 
         *                  - TileValues: sizeof(s32) * 30 tile cols * 17 tile cols = 4 * 510 = 2,040 bytes
         *                  - PlayerBitmaps: 16 x 16, 1094 bytes each * 4 bitmaps = 4,376 bytes
         *                  
         *
         *      - TransientStorage: 1,073,741,824 bytes
         *
         *          - struct ScratchHeader
         *          - ScratchArenas: 10 arenas at 10,240 bytes each: 100,240 bytes
         *
         */
        

        // Random
        GameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

        // Debug
        InitializeArena(&DebugState->DebugArena, (DebugRegion.Data + sizeof(debug_state)), (DebugRegion.Size - sizeof(debug_state)) );
        Assert(DebugRegion.Size == sizeof(debug_state) + DebugState->DebugArena.Size);

        DebugState->DebugTextBuf.Data = PushArray(&DebugState->DebugArena, u8, Kilobytes(4) );
        DebugState->DebugTextBuf.Size = Kilobytes(4);

        // Tiles
        InitializeArena(&GameState->TilesArena, (GameRegion.Data + sizeof(game_state)), Megabytes(4) );

        // World
        InitializeArena(&GameState->WorldArena, 
                        (GameRegion.Data + sizeof(game_state) + GameState->TilesArena.Size),
                        (GameRegion.Size - sizeof(game_state) - GameState->TilesArena.Size) );

        Assert(GameRegion.Size == sizeof(game_state) + GameState->TilesArena.Size + GameState->WorldArena.Size);

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
        tile_map *TileMap = &GameState->TileMap;
        // TileMap dimensions
        TileMap->TileSideInPixels = 64.0f;
        TileMap->NumRows = CeilingF32ToS32((f32)Buffer->Height / TileMap->TileSideInPixels);
        TileMap->NumCols = CeilingF32ToS32((f32)Buffer->Width / TileMap->TileSideInPixels);
        TileMap->NumTilesInWorld = TileMap->NumRows * TileMap->NumCols;
        TileMap->Bitmaps = (bitmap *)&GameState->TilesArena.Start;
        TileMap->TileValues = PushArray(WorldArena, s32, TileMap->NumTilesInWorld); 
        for(int TileIdx = 0;
            TileIdx < TileMap->NumTilesInWorld;
            ++TileIdx)
        {
            TileMap->TileValues[TileIdx] = RandomS32InRange(&GameState->RandomSeries, 0, 4);
        }

        // TODO(Aaron): Load default bitmap if one failed that indicates obvious failure
        char *TempPlayerBitmapFilepaths[] = {"player\\16x16faceright.bmp", 
                                        "player\\16x16behindview.bmp", 
                                        "player\\16x16faceleft.bmp", 
                                        "player\\16x16frontview.bmp"};
        for(int PlayerBitmapIdx = 0;
            PlayerBitmapIdx < 4;
            ++PlayerBitmapIdx)
        {
            bitmap *It = &GameState->PlayerBitmaps[PlayerBitmapIdx];
            // Per tufty.h enum order is East = 0, North = 1, West = 2, South = 3
            PushBitmapToArena(WorldArena, TempPlayerBitmapFilepaths[PlayerBitmapIdx], It, Memory);
        }

        LoadTileBitmaps(&GameState->TilesArena, TileMap, ScratchHeader, &DebugState->LastTileDirUpdate, Memory);


// Player
        GameState->PlayerP.X = Buffer->Width / 2;
        GameState->PlayerP.Y = Buffer->Height / 2;
        GameState->PlayerFacing = EAST;


        Memory->IsInitialized = true;
    }

    // hot reload player bitmaps
    for(int PlayerBitmapIdx = 0;
        PlayerBitmapIdx < 4;
        ++PlayerBitmapIdx)
    {
        DEBUGReloadBitmapIfChanged(&GameState->PlayerBitmaps[PlayerBitmapIdx], Memory);
    }

    // // hot reload tile bitmaps
    // for(int TileBitmapIdx = 0;
    //     TileBitmapIdx < GameState->TileMap.NumTileTypes;
    //     ++TileBitmapIdx)
    // {
    //     DEBUGReloadBitmapIfChanged(&GameState->TileMap.Bitmaps[TileBitmapIdx], Memory);
    // }
    //



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
        Row < TileMap->NumRows;
        ++Row)
    {
        for(int Col = 0;
            Col < TileMap->NumCols; 
            ++Col)
        {
            s32 TileOneDimensionalIndex = Row * TileMap->NumCols + Col;
            s32 TileValue = TileMap->TileValues[TileOneDimensionalIndex];
            if(TileValue > 0)
            {
                bitmap *TileBitmap;
                switch(TileValue)
                {
                    case 1:
                    {
                        TileBitmap = TileMap->Bitmaps;
                        while(!strstr(TileBitmap->Filepath, "dandelion"))
                        {
                            ++TileBitmap;
                        }
                    } break;

                    case 2:
                    {
                        TileBitmap = TileMap->Bitmaps;
                        while(!strstr(TileBitmap->Filepath, "puff"))
                        {
                            ++TileBitmap;
                        }
                    } break;

                    case 3:
                    {
                        TileBitmap = TileMap->Bitmaps;
                        while(!strstr(TileBitmap->Filepath, "path"))
                        {
                            ++TileBitmap;
                        }
                    } break;
                    
                    case 4:
                    {
                        TileBitmap = TileMap->Bitmaps;
                        while(!strstr(TileBitmap->Filepath, "ladybug"))
                        {
                            ++TileBitmap;
                        }
                    } break;
                    
                    case 5:
                    {
                        TileBitmap = TileMap->Bitmaps;
                        while(!strstr(TileBitmap->Filepath, "aphid"))
                        {
                            ++TileBitmap;
                        }
                    } break;
                }
                v2 TileMin = {Col * TileMap->TileSideInPixels, Row * TileMap->TileSideInPixels};
                v2 TileMax = TileMin + (v2){TileMap->TileSideInPixels, TileMap->TileSideInPixels};
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

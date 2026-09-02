#include "tufty.h"
#include "stb_easy_font.h"

#define PLAYER_HEIGHT 16.0f
#define PLAYER_WIDTH  16.0f

enum
{
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

static u32
RoundF32ToU32(f32 Real)
{
    u32 Result = (u32)(Real + 0.5f);
    return(Result);
}

static s32
RoundF32ToS32(f32 Real)
{
    s32 Result = (s32)(Real + 0.5f);
    return(Result);
}

static u32
TruncateF32ToU32(f32 Real)
{
    u32 Result = (u32)Real;
    return(Result);
}


static s32
LerpS32(s32 A, s32 B, f32 T)
{
    s32 Result = A + T * (B - A);
    return(Result);
}

static void
DrawRect(game_offscreen_buffer *Buf, v2 Min, v2 Max, f32 R, f32 G, f32 B)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    if(MinX < 0)
    {
        MinX = 0;
    }
    if(MinY < 0)
    {
        MinY = 0;
    }
    if(MaxX >= Buf->Width)
    {
        MaxX = Buf->Width;
    }
    if(MaxY >= Buf->Height)
    {
        MaxY = Buf->Height;
    }

    u32 Color = (((RoundF32ToU32(R * 255.0f)) << 16) |
                 ((RoundF32ToU32(G * 255.0f)) <<  8) |
                 ((RoundF32ToU32(B * 255.0f)) <<  0));

    u8 *Row = (u8 *)Buf->Memory + MinY * Buf->Pitch + MinX * Buf->BytesPerPixel;

    for(int Y = MinY; Y < MaxY; ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for(int X = MinX; X < MaxX; ++X)
        {
            *Pixel++ = Color;
        }
        Row += Buf->Pitch;
    }
}

void
DrawOpaqueRectWithOutline(game_offscreen_buffer *Buffer,
                    v2 Min, v2 Max,
                    f32 R, f32 G, f32 B)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    if(MinY < 0)
    {
        MinY = 0;
    }
    if(MaxY > Buffer->Height)
    {
        MaxY = Buffer->Height;
    }
    if(MinX < 0)
    {
        MinX = 0;
    }
    if(MaxX > Buffer->Width)
    {
        MaxX = Buffer->Width;
    }

    u32 Blue = RoundF32ToU32(B * 255.0f);
    u32 Green = RoundF32ToU32(G * 255.0f);
    u32 Red = RoundF32ToU32(R * 255.0f);
    f32 Alpha = 0.5f;

    u8 *Row = (u8 *)Buffer->Memory + (MinY * Buffer->Pitch) + (MinX * Buffer->BytesPerPixel);
    for(int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u8 *Pixel = Row;
        for(int X = MinX;
            X < MaxX;
            ++X)
        {
            if(Y == MinY)
            {
                *(u32 *)Pixel = 0;
                Pixel += sizeof(u32);
            }
            else if(X == MinX)
            {
                *(u32 *)Pixel = 0;                
                Pixel += sizeof(u32);
            }
            else
            {
                // Blue
                Pixel[0] = LerpS32(Pixel[0], Blue, Alpha);

                // Green
                Pixel[1] = LerpS32(Pixel[1], Green, Alpha);
                
                // Red
                Pixel[2] = LerpS32(Pixel[2], Red, Alpha);

                Pixel += sizeof(u32);
            }
        }
        Row += Buffer->Pitch;
    }
}
void
DEBUGDrawText(game_offscreen_buffer *Backbuf,
              f32 X, f32 Y,
              char *StringText, buffer RasterizedTextBuf,
              f32 R, f32 G, f32 B)
{
    int NumQuads = stb_easy_font_print(0, 0, StringText, NULL, RasterizedTextBuf.Start, RasterizedTextBuf.Size);
    for(int QuadIdx = 0;
        QuadIdx < NumQuads;
        ++QuadIdx)
    {
        quad *ThisQuad = (quad *)(RasterizedTextBuf.Start + QuadIdx * sizeof(quad));
        v2 Min = {(X + ThisQuad->TopLeft.X*2.5f), (Y + ThisQuad->TopLeft.Y*2.5f)};
        v2 Max = {(X + ThisQuad->BottomRight.X*2.5f), (Y + ThisQuad->BottomRight.Y*2.5f)};
        DrawRect(Backbuf, Min, Max, R, G, B);
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

    if(!(Memory->IsInitialized))
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

        GameState->PlayerP.X = Buffer->Width / 2;
        GameState->PlayerP.Y = Buffer->Height / 2;
        GameState->PlayerFacing = EAST;


        // TODO(Aaron): Load default bitmap if one failed that indicates obvious failure
        PushBitmapToArena(WorldArena, "16x16faceright.bmp", &GameState->PlayerBitmaps[EAST], Memory);
        PushBitmapToArena(WorldArena, "16x16behindview.bmp", &GameState->PlayerBitmaps[NORTH], Memory);
        PushBitmapToArena(WorldArena, "16x16faceleft.bmp", &GameState->PlayerBitmaps[WEST], Memory);
        PushBitmapToArena(WorldArena, "16x16frontview.bmp", &GameState->PlayerBitmaps[SOUTH], Memory);


        GameState->MaxThings = 10;
        GameState->Things = PushArray(WorldArena, thing, GameState->MaxThings);
        thing *Things = GameState->Things;

        char *ThingFilenames[] = {"64x64dandelion.bmp", "64x64dandelionpuff.bmp", "64x64grasspath.bmp", "64x64ladybug.bmp", "64x64aphid.bmp"};

        for(int ThingIdx = 0;
            ThingIdx < ArrayCount(ThingFilenames);
            ++ThingIdx)
        {
            thing *Thing = Things + ThingIdx;
            Thing->Id = ThingIdx;
            PushBitmapToArena(WorldArena, ThingFilenames[ThingIdx], &Thing->Bitmap, Memory);
        }

        Memory->IsInitialized = true;
    }

    // hot reload bitmaps
    for(int PlayerBitmapIdx = 0;
        PlayerBitmapIdx < 4;
        ++PlayerBitmapIdx)
    {
        DEBUGReloadBitmapIfChanged(&GameState->PlayerBitmaps[PlayerBitmapIdx], Memory);
    }

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

            // if(NewPlayerP.X-(PLAYER_WIDTH*0.5) < 0)
            // {
            //     NewPlayerP.X = PLAYER_WIDTH*0.5f;
            // }
            // if(NewPlayerP.X+(PLAYER_WIDTH*0.5) >= Buffer->Width)
            // {
            //     NewPlayerP.X = Buffer->Width-(PLAYER_WIDTH*0.5f);
            // }
            // if(NewPlayerP.Y-PLAYER_HEIGHT < 0)
            // {
            //     NewPlayerP.Y = PLAYER_HEIGHT;
            // }
            // if(NewPlayerP.Y >= Buffer->Height)
            // {
            //     NewPlayerP.Y = Buffer->Height;
            // }
            //
            GameState->PlayerP = NewPlayerP;
        }
    }

    // Underlayer
    v2 ScreenMin = {0, 0};
    v2 ScreenMax = {(f32)Buffer->Width, (f32)Buffer->Height};
    DrawRect(Buffer, ScreenMin, ScreenMax, 0.1f, 0.5f, 0.45f);

    // Tiles
    for(int Row = 0; 
        Row < RoundF32ToS32((f32)Buffer->Height / 64.0f); 
        ++Row)
    {
        for(int Col = 0;
            Col < RoundF32ToS32((f32)Buffer->Width / 64.0f); 
            ++Col)
        {
            v2 TileMin = {Col * 64.0f, Row * 64.0f};
            v2 TileMax = TileMin + (v2){64.0f, 64.0f};
            DrawOpaqueRectWithOutline(Buffer, TileMin, TileMax, 0.75, 0.75, 0.75);
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

    // Dandelion
    thing *Things = GameState->Things;
    thing *Dandelion = Things + DANDELION;
    Dandelion->Position = {0, (f32)(Buffer->Height - Dandelion->Bitmap.Height - 1)};

    // Puff
    thing *Puff = Things + PUFF;
    Puff->Position = {(f32)Buffer->Width * 0.33f, (f32)Buffer->Height * 0.33f};

    // Grass path
    thing *Path = Things + PATH;
    Path->Position = {(f32)(Buffer->Width * 0.5f) - 32.0f, -40.0f};

    // Aphid
    thing *Aphid = Things + APHID;
    Aphid->Position = {Things[DANDELION].Position.X, Things[DANDELION].Position.Y - 10.0f};

    thing *Ladybug = Things + LADYBUG;
    Ladybug->Position = {(f32)Things[APHID].Position.X, Things[APHID].Position.Y - 30.0f};

    for(thing *Thing = GameState->Things;
        Thing->Id < GameState->MaxThings;
        ++Thing)
    {
        if(Thing->Position == (v2){0, 0})
        {
            continue;
        }

        v2 ThingMax = Thing->Position + (v2){(f32)Thing->Bitmap.Width, (f32)Thing->Bitmap.Height};
        ScaleAndBlitBitmap(Buffer, Thing->Position, ThingMax, &Thing->Bitmap);
    }
        

    // Dandelion
    // thing *Dandelion = GameState->Things + 0;
    // v2 DandelionMax = {Dandelion->Position.X + Dandelion->Bitmap.Width, Dandelion->Position.Y + Dandelion->Bitmap.Height};
    // ScaleAndBlitBitmap(Buffer, Dandelion->Position, DandelionMax, &Dandelion->Bitmap);

#define FPS_SNAPS 30 
    static f32 FpsSnaps[FPS_SNAPS] = {};
    static int FpsPrintCounter = 0;
    f32 FpsAvg = 0;
    FpsSnaps[FpsPrintCounter] = Input->Fps;
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
    DEBUGDrawText(Buffer, (Buffer->Width * 0.85f), 30, Temp, DebugState->DebugTextBuf, 0.9f, 0.2f, 0.5f);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

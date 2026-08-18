#include "tufty.h"

#define PLAYER_HEIGHT 256.0f
#define PLAYER_WIDTH (256.0f)

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

static void
DrawRect(game_offscreen_buffer *Buf, f32 RealMinX, f32 RealMinY, f32 RealMaxX, f32 RealMaxY, f32 R, f32 G, f32 B)
{
    s32 MinX = RoundF32ToS32(RealMinX);
    s32 MinY = RoundF32ToS32(RealMinY);
    s32 MaxX = RoundF32ToS32(RealMaxX);
    s32 MaxY = RoundF32ToS32(RealMaxY);

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

static bitmap
DEBUGLoadBitmap(char *Filename, debug_platform_read_entire_file *ReadEntireFile)
{
    // not checking a lot of stuff here. doing casts of uints to s-ints, etc. Hence DEBUG
    bitmap Result;
    debug_read_file_result ReadResult = ReadEntireFile(Filename);
    bitmap_header *Header = (bitmap_header *)ReadResult.Contents;
    Result.Size = (size_t)Header->Size;
    Result.Pixels = (u8 *)ReadResult.Contents + Header->DataOffset;
    Result.Height = Header->Height;
    Result.Width = Header->Width;
    Assert(Header->BitsPerPixel == 32);
    Result.BytesPerPixel = Header->BitsPerPixel / 8;
    Result.Pitch = Result.Width * Result.BytesPerPixel;

    return(Result);
}

static void
ScaleAndBlitBitmap(game_offscreen_buffer *Buf, v2 Min, v2 Max, bitmap *Bitmap)
{
    // Note: this will probably write out of bounds as soon as we try to render
    //      the player walking off the side of the screen.
    f32 XCoef = (f32)Bitmap->Width / (Max.X - Min.X);
    f32 YCoef = (f32)Bitmap->Height / (Max.Y - Min.Y);
    u32 MinY = RoundF32ToU32(Min.Y);
    u32 MaxY = RoundF32ToU32(Max.Y);
    u32 MinX = RoundF32ToU32(Min.X);
    u32 MaxX = RoundF32ToU32(Max.X);

    u8 *DestRow = (u8 *)Buf->Memory + MinY * Buf->Pitch + MinX * Buf->BytesPerPixel;
    for(int Y = MinY; Y < MaxY; ++Y)
    {
        u32 SrcRowIdx = TruncateF32ToU32(YCoef*((f32)(Y-MinY)+0.5f));
        u32 SrcRow = Bitmap->Height - 1 - SrcRowIdx;
        u32 *DestPixel = (u32 *)DestRow;
        for(int X = MinX; X < MaxX; ++X)
        {
            u32 SrcCol = TruncateF32ToU32(XCoef*((f32)(X-MinX)+0.5f));
            u32 SrcPixel = *(u32 *)(Bitmap->Pixels + SrcRow * Bitmap->Pitch + SrcCol * Bitmap->BytesPerPixel);
            *DestPixel++ = SrcPixel;
        }
        DestRow += Buf->Pitch;
    }
}


// #define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    game_state *GameState = (game_state *)Memory->PermanentStorage;
    if(!(Memory->IsInitialized))
    {
        InitializeArena(&GameState->WorldArena, (u8 *)Memory->PermanentStorage + sizeof(game_state), 
                        Memory->PermanentStorageSize-sizeof(game_state));

        GameState->PlayerP.X = Buffer->Width / 2;
        GameState->PlayerP.Y = Buffer->Height / 2;
        GameState->PlayerFacing = SOUTH;

        Memory->IsInitialized = true;
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

            if(NewPlayerP.X-(PLAYER_WIDTH*0.5) < 0)
            {
                NewPlayerP.X = PLAYER_WIDTH*0.5f;
            }
            if(NewPlayerP.X+(PLAYER_WIDTH*0.5) >= Buffer->Width)
            {
                NewPlayerP.X = Buffer->Width-(PLAYER_WIDTH*0.5f);
            }
            if(NewPlayerP.Y-PLAYER_HEIGHT < 0)
            {
                NewPlayerP.Y = PLAYER_HEIGHT;
            }
            if(NewPlayerP.Y >= Buffer->Height)
            {
                NewPlayerP.Y = Buffer->Height;
            }

            GameState->PlayerP = NewPlayerP;
        }
    }

    GameState->PlayerBitmaps[EAST] = DEBUGLoadBitmap("16x16faceright.bmp", Memory->DEBUGPlatformReadEntireFile);
    GameState->PlayerBitmaps[NORTH] = DEBUGLoadBitmap("16x16behindview.bmp", Memory->DEBUGPlatformReadEntireFile);
    GameState->PlayerBitmaps[WEST] = DEBUGLoadBitmap("16x16faceleft.bmp", Memory->DEBUGPlatformReadEntireFile);
    GameState->PlayerBitmaps[SOUTH] = DEBUGLoadBitmap("16x16frontview.bmp", Memory->DEBUGPlatformReadEntireFile);

    // Underlayer
    f32 ScreenMinY = 0;
    f32 ScreenMinX = 0;
    f32 ScreenMaxX = (f32)Buffer->Width;
    f32 ScreenMaxY = (f32)Buffer->Height;
    DrawRect(Buffer, ScreenMinX, ScreenMinY, ScreenMaxX, ScreenMaxY, 0.75f, 0.25f, 0.5f);

    v2 PlayerMin = {GameState->PlayerP.X - (PLAYER_WIDTH * 0.5f), 
                    GameState->PlayerP.Y - PLAYER_HEIGHT};
    v2 PlayerMax = {GameState->PlayerP.X + (PLAYER_WIDTH * 0.5f), 
                    GameState->PlayerP.Y};

    ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, &GameState->PlayerBitmaps[GameState->PlayerFacing]);


    // DrawRect(Buffer, 
    //          GameState->PlayerP.X - (PLAYER_WIDTH * 0.5f), GameState->PlayerP.Y - PLAYER_HEIGHT,
    //          GameState->PlayerP.X + (PLAYER_WIDTH * 0.5f), GameState->PlayerP.Y,
    //          0, 0, 0);

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    GameOutputSound(SoundBuffer, 400);
}

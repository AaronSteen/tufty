#include "tufty.h"

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
        MaxX = Buf->Width-1;
    }
    if(MaxY >= Buf->Height)
    {
        MaxY = Buf->Height-1;
    }

    u32 Color = (((RoundF32ToU32(R * 255.0f)) << 16) ||
                 ((RoundF32ToU32(G * 255.0f)) <<  8) ||
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
GameOutputSound(game_state *GameState, game_sound_output_buffer *SoundBuffer, int ToneHz)
{
    s16 ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    s16 *SampleOut = SoundBuffer->Samples;
    for(int SampleIdx = 0;
        SampleIdx < SoundBuffer->SampleCount;
        ++SampleIdx)
    {
#if 0
        real32 SineValue = sinf(GameState->tSine);
        s16 SampleValue = (s16)SineVavlue * ToneVolume;
#else
        s16 SampleValue = 0;
#endif
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;
#if 0
        GameState->tSine += 2.0f*Pi32*1.0f/(real32)WavePeriod;

        if(GameState->tSine > 2.0f*Pi32)
        {
            GameState->tSine -= 2.0f*Pi32;
        }
#endif
    }
}

GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
#if 0    
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
            f32 dPlayerY = 0;
            f32 dPlayerX = 0;
            if(Controller->MoveRight.EndedDown)
            {
                dPlayerX += 1.0f;
            }
            if(Controller->MoveUp.EndedDown)
            {
                dPlayerY += 1.0f;
            }
            if(Controller->MoveLeft.EndedDown)
            {
                dPlayerX -= 1.0f;
            }
            if(Controller->MoveDown.EndedDown)
            {
                dPlayerY -= 1.0f;
            }
            if((dPlayer.X != 0) && (dPlayer.Y != 0))
            {
                dPlayerX *= 0.707106781187f;
                dPlayerY *= 0.707106781187f;
            }
        }
    }
#endif

    f32 ScreenMinY = 0;
    f32 ScreenMinX = 0;
    f32 ScreenMaxX = (f32)Buffer->Width;
    f32 ScreenMaxY = (f32)Buffer->Height;
    DrawRectangle(Buffer, ScreenMinX, ScreenMinY, ScreenMaxX, ScreenMaxY, 0.75f, 0.25f, 0.5f);
}

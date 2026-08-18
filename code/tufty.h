#pragma once
#include "tufty_platform.h"

struct arena
{
    u8 *Start;
    memory_idx Size;
    memory_idx Cursor;
};

static void
InitializeArena(arena *Arena, u8 *Start, memory_idx Size)
{
    Arena->Start = Start;
    Arena->Size = Size;
    Arena->Cursor = 0;
}

static void *
ArenaPush_(arena *Arena, memory_idx NumBytes)
{
    Assert(Arena->Cursor+NumBytes < Arena->Size);

    void *Result = Arena->Start + Arena->Cursor;
    Arena->Cursor += NumBytes;

    return(Result);
}

#define PushArray(Arena, Type, Count) (Type *)ArenaPush_((Arena), sizeof(Type) * (Count))
#define PushStruct(Arena, Type) (Type *)ArenaPush_((Arena), sizeof(Type))

struct v2
{
    f32 X, Y;
};

struct bitmap
{
    u8 *Pixels;
    memory_idx Size;
    int Height;
    int Width;
    int BytesPerPixel;
    int Pitch;
};

enum facing
{
    EAST,
    NORTH,
    WEST,
    SOUTH
};

struct game_state
{
    arena WorldArena;
    v2 PlayerP;
    facing PlayerFacing;
    bitmap PlayerBitmaps[4];
};

#pragma pack(push, 1)
struct bitmap_header
{
    char Signature[2];
    u32 FileSize;
    u8 Reserved[4];
    u32 DataOffset;
    u32 Size;
    u32 Width;
    s32 Height;
    u16 Planes;
    u16 BitsPerPixel;
    u32 Compression;
    u32 CompressedImageSize;
    u32 XPixelsPerMeter;
    u32 YPixelsPerMeter;
    u32 ColorsUsed;
    u32 ImportantColors;
    u32 RedMask;
    u32 GreenMask;
    u32 BlueMask;
};
#pragma pack(pop)

#pragma once

#if TUFTY_INTERNAL
#include <stdio.h>
#endif
#include "tufty_platform.h"

struct arena
{
    u8 *Start;
    mem_idx Size;
    mem_idx Cursor;
};

static void
InitializeArena(arena *Arena, u8 *Start, mem_idx Size)
{
    Arena->Start = Start;
    Arena->Size = Size;
    Arena->Cursor = 0;
}

static void *
ArenaPush_(arena *Arena, mem_idx NumBytes)
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

v2
operator+(v2 A, v2 B)
{
    v2 Result;

    Result.X = A.X + B.X;
    Result.Y = A.Y + B.Y;

    return(Result);
}

b32
operator==(v2 A, v2 B)
{
    b32 Result = ((A.X == B.X) && (A.Y == B.Y));

    return(Result);
}

struct buffer
{
    u8 *Start;
    mem_idx Size;
};

struct bitmap
{
    // This is the entire bitmap file, including the header
    buffer Buffer;
    char *Filename;
    u64 LastWriteTime;

    // Where the pixels actually live
    u8 *Pixels;
    int Height, Width, BytesPerPixel, Pitch;
};

enum facing
{
    EAST,
    NORTH,
    WEST,
    SOUTH
};


struct debug_state
{
    arena DebugArena;
    buffer DebugTextBuf;
};

struct thing
{
    int Id;
    bitmap Bitmap;
    buffer ScaledBitmap;
    v2 Position;
};

struct game_state
{
    arena WorldArena;
    v2 PlayerP;
    facing PlayerFacing;
    bitmap PlayerBitmaps[4];
    thing *Things;
    int MaxThings;
};

#pragma pack(push, 1)
struct bitmap_header
{
    char Signature[2];
    u32 FileSize;
    u8 Reserved[4];
    u32 DataOffset;
    u32 HeaderSize;
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
    // NOTE(Aaron): May need to adopt alpha and RGB masks fields from Beaver at some point, but
    //      for now we don't use.
};
#pragma pack(pop)


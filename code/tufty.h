#pragma once

#if TUFTY_INTERNAL
#include <stdio.h>
#endif
#include "tufty_platform.h"
#include <math.h>
#include <string.h>

#define MAX_STRING_LEN 512
#define MAX_TILE_TYPES 200
#define TILE_ID_ARRAY_LEN MAX_TILE_TYPES + 1

static u32
RoundF32ToU32(f32 Real)
{
    u32 Result = (u32)roundf(Real);
    return(Result);
}

static s32
RoundF32ToS32(f32 Real)
{
    s32 Result = (s32)roundf(Real);
    return(Result);
}

static u32
FloorF32ToU32(f32 Real)
{
    u32 Result = (u32)floorf(Real);
    return(Result);
}

static s32
CeilingF32ToS32(f32 Real)
{
    s32 Result = (s32)ceilf(Real);
    return(Result);
}

static s32
LerpS32(s32 A, s32 B, f32 T)
{
    s32 Result = A + T * (B - A);
    return(Result);
}


#include "myrandom.h"

struct arena
{
    u8 *Start;
    mem_idx Size;
    mem_idx Cursor;
};

#define NUM_SCRATCHES 10

struct scratch_arena
{
    b32 IsFree;
    arena Arena;
};

struct scratch_header
{
    u32 Count;
    scratch_arena ScratchArenas[NUM_SCRATCHES];
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


struct bitmap
{
    // This is the entire bitmap file, including the header
    buffer Buffer;
    char *Filepath;

    b32 ReadyToRead;
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

struct mem_region
{
    mem_idx Size;
    u8 *Data;
};

struct debug_state
{
    arena DebugArena;
    buffer DebugTextBuf;
    u64 LastTileDirUpdate;
    b32 ReadyToReload;
};

struct tile_id
{
    char Filepath[MAX_STRING_LEN];
    bitmap *Bitmap;
};

struct tile_map
{
    u32 NumRows;
    u32 NumCols;
    u32 NumTileTypes;
    bitmap *Bitmaps;
    tile_id TileIDs[TILE_ID_ARRAY_LEN];
    int NumTilesInWorld;
    f32 TileSideInPixels;
    s32 *TileValues;
};

struct game_state
{
    arena WorldArena;

    tile_map TileMap;
    arena TilesArena;

    random_series RandomSeries;

    v2 PlayerP;
    facing PlayerFacing;
    bitmap PlayerBitmaps[4];

    b32 EditMode;
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


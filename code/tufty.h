#pragma once

#if TUFTY_INTERNAL
#include <stdio.h>
#endif
#include "tufty_platform.h"
#include <math.h>
#include <string.h>

#define STRING_LEN 250
#define MAX_TILE_TYPES 200
#define TILE_TYPES_ARRAY_LEN (MAX_TILE_TYPES + 1)
#define MAX_FACING_BITMAPS 10
#define FACING_BITMAPS_ARRAY_LEN (MAX_FACING_BITMAPS + 1)

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

static s32
FloorF32ToS32(f32 Real)
{
    s32 Result = (s32)floorf(Real);
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
    u8 *Data;
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
    Arena->Data = Start;
    Arena->Size = Size;
    Arena->Cursor = 0;
}

static void *
ArenaPush_(arena *Arena, mem_idx NumBytes)
{
    Assert(Arena->Cursor+NumBytes < Arena->Size);

    void *Result = Arena->Data + Arena->Cursor;
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

v2
operator-(v2 A, v2 B)
{
    v2 Result;

    Result.X = A.X - B.X;
    Result.Y = A.Y - B.Y;

    return(Result);
}

v2
operator+=(v2 &A, v2 B)
{
    A = A + B;
    return(A);
}

b32
operator==(v2 A, v2 B)
{
    b32 Result = ((A.X == B.X) && (A.Y == B.Y));
    return(Result);
}

struct color
{
    f32 R, G, B, A;
};

struct bitmap
{
    // This is the entire bitmap file, including the header
    buffer Buffer;

    // Where the pixels actually live. Just a pointer into
    //      the buffer Buffer above where the pixels start.
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

struct meta_bitmap
{
    char Filepath[STRING_LEN];
    b32 ReadyToReload;
    u64 LastUpdateTime;
    bitmap Bitmap;
};

struct tile_map
{
    arena TilesArena;
    b32 ReadyToReload;
    u64 LastUpdateTime;
    int NumRows;
    int NumCols;
    int NumTileTypes;
    meta_bitmap *TileTypes;
    int NumTilesInWorld;
    f32 TileSideInPixels;
    s32 *TileValues;
};

struct menu_tile
{
    v2 TileMin;
    v2 TileMax;
    meta_bitmap *MetaBitmap;
};

b32
DoFilepathsMatch(char *A, char *B) {
    if(strncmp(A, B, STRING_LEN) == 0) {
        return(true);
    }
    else {
        return(false);
    }
}

struct arr_meta_bitmap {
    int Size;
    meta_bitmap *Data;
    // int *Used;
    int NextEmptySlot = 1;

    int 
        Add(char *AddFilepath) {
            int Slot = 0;
            if(NextEmptySlot < Size) {
                Slot = NextEmptySlot++;
            }
            if(Slot) {
                Data[Slot] = {};
                snprintf(Data[Slot].Filepath, strlen(AddFilepath), "%s", AddFilepath);
                return(Slot);
            } 
            else {
                return(0);
            }
        }

    meta_bitmap& 
        Get(int Idx) {
            if(Idx > 0 && Idx < Size) {
                return(Data[Idx]);
            } 
            else {
                return(Data[0]);
            }
        }

    void
        SetMissing(int Idx) {
            if(Idx > 0 && Idx < Size) {
                Data[Idx].ReadyToReload = false;
                Data[Idx].LastUpdateTime = 0;
                Data[Idx].Bitmap = {};
            }
        }

    void
        SetEmpty(int Idx) {
            if(Idx > 0 && Idx < Size) {
                Data[Idx] = {};
            }
        }

    bool 
        IsEmpty(int Slot) {
            if(Data[Slot].Filepath[0] == 0) {
                return(true);
            }
            else {
                return(false);
            }
        }

    bool 
        IsMissing(int Slot) {
            if(Data[Slot].Filepath[0] != 0 && Data[Slot].Bitmap.Buffer.Size == 0) {
                return(true);
            }
            else {
                return(false);
            }
        }

    bool 
        IsPresent(int Slot) {
            if(Data[Slot].Filepath[0] != 0 && Data[Slot].Bitmap.Buffer.Size > 0) {
                return(true);
            }
            else {
                return(false);
            }
        }

    int
        FindByFilepath(char *SearchFilepath) {
            if(SearchFilepath[0] != 0) {
                for(int Idx = 1; Idx < Size; ++Idx) {
                    if(DoFilepathsMatch(Data[Idx].Filepath, SearchFilepath)) {
                        return(Idx);
                    }
                }
            }
            return(0);
        }

    int
        CountNonEmptySlots(void) {
            int Count = 0;
            for(int Idx = 1; Idx < Size; ++Idx) {
                if(!IsEmpty(Idx)) {
                    ++Count;
                }
            }
            return(Count);
        }
};

struct facing_bitmaps
{
    b32 ReadyToReload;
    u64 LastUpdateTime;
    int NumBitmaps;
    arena Arena;
    int DrawThis;
    arr_meta_bitmap MetaBitmaps;
};

union all_player_bitmaps
{
    facing_bitmaps Array[4];
    struct
    {
        facing_bitmaps East;
        facing_bitmaps North;
        facing_bitmaps West;
        facing_bitmaps South;
    };
};

struct player
{
    v2 Position;
    facing IsFacing;
    all_player_bitmaps AllBitmaps;
};

struct found_filepath
{
    char Filepath[STRING_LEN];
    b32 LoadedYet;
};

enum mouse_mode
{
    IDLE,
    CONSUMED,
    PAINTING
};

struct cursor_state
{
    mouse_mode PrimaryMode;
    mouse_mode SecondaryMode;
    meta_bitmap *HeldMetaBitmap;
};

struct game_state
{
    arena WorldArena;

    tile_map TileMap;
    random_series RandomSeries;

    player Player;
};

enum which_editor
{
    NO_EDITOR = 0,
    TILE,
    PLAYER
};

struct editor_state
{
    which_editor WhichEditor;
    cursor_state CursorState;
    b32 PrintRowsCols;
};

struct debug_state
{
    arena DebugTextArena;
    arena FailBitmapsArena;
    editor_state EditorState;
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

struct saved_project
{
    char MagicNumber[4];
    int NumPlayerBitmapsPerFacing[4];
    int NumTileRows;
    int NumTileCols;
    int NumTileTypes;
    mem_idx PlayerDrawThisOffset;
    mem_idx PlayerFilepathsOffset;
    mem_idx TileTypeFilepathsOffset;
    mem_idx TileValuesOffset;
};
#pragma pack(pop)


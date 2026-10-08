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
static debug_output *DebugOutput;

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

enum wait_state
{
    NOTHING,
    WAITING,
    READY_TO_RELOAD
};

struct meta_bitmap
{
    char Filepath[STRING_LEN];
    wait_state WaitState; 
    int WaitedFrames;
    u64 LastWriteTime;
    bitmap Bitmap;

    bool 
    IsEmpty(void) 
    {
        if (Filepath[0] == 0) {
            return(true);
        }
        else {
            return(false);
        }
    }

    bool 
    IsMissing(void) 
    {
        if (Filepath[0] != 0 && Bitmap.Buffer.Size == 0) {
            return(true);
        }
        else {
            return(false);
        }
    }

    bool 
    IsPresent(void) 
    {
        if (Filepath[0] != 0 && Bitmap.Buffer.Size > 0 && Bitmap.Buffer.Data) {
            return(true);
        }
        else {
            return(false);
        }
    }

};

struct found_filepath
{
    char Filepath[STRING_LEN];
    b32 LoadedYet;
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

struct arr_meta_bitmap
{
    int Len;
    meta_bitmap *Data;
    int NextEmptySlot;
    arena BitmapsArena;

    int 
    Add(char *AddFilepath)
    {
        int Slot = 0;
        if(NextEmptySlot < Len) {
            Slot = NextEmptySlot++;
        }
        if(Slot) {
            Data[Slot] = {};
            snprintf(Data[Slot].Filepath, strlen(AddFilepath)+1, "%s", AddFilepath);
            return(Slot);
        } 
        else {
            return(0);
        }
    }

    meta_bitmap *
    Get(int Idx) 
    {
        if(Idx > 0 && Idx < Len) {
            return(Data + Idx);
        } 
        else {
            return(Data + 0);
        }
    }


    void
    SetMissing(int Idx) 
    {
        if(Idx > 0 && Idx < Len) {
            Data[Idx].WaitState = NOTHING;
            Data[Idx].WaitedFrames = 0;
            Data[Idx].Bitmap = {};
        }
    }

    void
    SetEmpty(int Idx) 
    {
        if(Idx > 0 && Idx < Len) {
            Data[Idx] = {};
        }
    }

    int
    FindByFilepath(char *SearchFilepath) 
    {
        if(SearchFilepath[0] != 0) {
            for(int Idx = 1; Idx < Len; ++Idx) {
                if(DoFilepathsMatch(Data[Idx].Filepath, SearchFilepath)) {
                    return(Idx);
                }
            }
        }
        return(0);
    }

    int
    CountNonEmptySlots(void) 
    {
        int Count = 0;
        for(int Idx = 1; Idx < Len; ++Idx) {
            if(!Data[Idx].IsEmpty()) {
                ++Count;
            }
        }
        return(Count);
    }

    void
    Clear(void)
    {
        for (int Slot = 1; Slot < Len; ++Slot) {
            Data[Slot] = {};
        }
    }

    void
    ClearButLeaveFilepaths(void) 
    {
        for (int Slot = 1; Slot < Len; ++Slot) {
            Data[Slot].LastWriteTime = 0;
            Data[Slot].WaitState = NOTHING;
            Data[Slot].WaitedFrames = 0;
            Data[Slot].Bitmap = {};
        }
    }

    void
    UpdateBitmapList(found_filepath *pOsFilepaths, int NumOsFilepaths)
    {
        // Identify filepaths whose bitmaps were previously in the game, and are still in the game.
        //      If so, found_filepath->LoadedYet = true, and we don't add this bitmap
        //      to the array in the list below
        for (int Slot = 1; Slot < Len; ++Slot) {
            for (int OsIdx = 0; OsIdx < NumOsFilepaths; ++OsIdx) {
                if (DoFilepathsMatch(Data[Slot].Filepath, pOsFilepaths[OsIdx].Filepath)) {
                    pOsFilepaths[OsIdx].LoadedYet = true;
                    break;
                }
            }
        }

            // Add any OsFilepaths that were not in the game yet to the list
        for (int OsIdx = 0; OsIdx < NumOsFilepaths; ++OsIdx) {
            if (!pOsFilepaths[OsIdx].LoadedYet) {
                int AddedIdx = Add(pOsFilepaths[OsIdx].Filepath);
                if(AddedIdx == 0) {
                    DebugOutput("Error: In %s, couldn't add OSFilepath %s because would have \
                                overflowed bitmaps list", __func__, pOsFilepaths[OsIdx].Filepath);
                }
                else {
                    pOsFilepaths->LoadedYet = true;
                }
            }
        }

        // For every slot in the array that contains a filepath, try to get its size.
        //      If the Os gives us a size, store that size in the bitmap and give it a buffer.
        //      If it doesn't, size remains zero and future pIt->IsPresent() calls will
        //      return false.
        for (int Slot = 0; Slot < Len; ++Slot) {
            meta_bitmap *pIt = Get(Slot);
            if (!pIt->IsEmpty()) {
                mem_idx Size = GetFileSize(pIt->Filepath);
                if (Size) {
                    pIt->Bitmap.Buffer.Size = Size;
                    pIt->Bitmap.Buffer.Data = PushArray(&BitmapsArena, u8, Size);
                    pIt->WaitState = READY_TO_RELOAD;
                    pIt->WaitedFrames = 0;
                }
            }
        }
    }   

    bool
    IsNilBitmap(meta_bitmap *pToCheck)
    {
        bool Result = (pToCheck - Data == 0);
        return(Result);
    }

    void
    ReplaceSlotAWithSlotB(int SlotA, int SlotB)
    {
        if (SlotA > 0 && SlotA < Len && SlotB > 0 && SlotB < Len) {
            Data[SlotA] = Data[SlotB];
            Data[SlotB] = {};
        }
    }

};

struct tile_map
{
    wait_state WaitState;
    int WaitedFrames;
    u64 LastUpdateTime;
    int NumRows;
    int NumCols;
    int NumTilesInWorld;
    arr_meta_bitmap TileTypes;
    f32 TileSideInPixels;
    s32 *TileValues;
};

struct menu_bitmap
{
    v2 TileMin;
    v2 TileMax;
    meta_bitmap *pMetaBitmap;
    int FacingIdx;
    int Slot;
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

struct facing_bitmaps
{
    wait_state WaitState;
    int WaitedFrames;
    b32 ReadyToReload;
    u64 LastUpdateTime;
    int DrawThis;
    arr_meta_bitmap MetaBitmaps;
};

struct player
{
    v2 Position;
    int IsFacing;
    facing_bitmaps FacingBitmaps[4];
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
    int HeldPlayerBitmapFacing;
    int Slot;
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


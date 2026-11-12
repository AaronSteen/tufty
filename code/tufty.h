#pragma once

#if TUFTY_INTERNAL
#include <stdio.h>
#endif
#include "tufty_platform.h"
#include <math.h>
#include <string.h>

struct arena
{
    u8 *pBase;
    mem_idx Size;
    mem_idx Used;
};

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

    return Result;
}

v2
operator-(v2 A, v2 B)
{
    v2 Result;

    Result.X = A.X - B.X;
    Result.Y = A.Y - B.Y;

    return Result;
}

v2
operator*(int Scalar, v2 Vector)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2
operator*(v2 Vector, int Scalar)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2 &
operator*=(v2 &Vector, int Scalar)
{
    Vector.X = Vector.X * Scalar;
    Vector.Y = Vector.Y * Scalar;

    return Vector;
}

v2
operator*(f32 Scalar, v2 Vector)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2
operator*(v2 Vector, f32 Scalar)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2 &
operator*=(v2 &Vector, f32 Scalar)
{
    Vector.X = Vector.X * Scalar;
    Vector.Y = Vector.Y * Scalar;

    return Vector;
}

v2
operator*(f64 Scalar, v2 Vector)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2
operator*(v2 Vector, f64 Scalar)
{
    v2 Result;

    Result.X = Scalar * Vector.X;
    Result.Y = Scalar * Vector.Y;

    return Result;
}

v2 &
operator*=(v2 &Vector, f64 Scalar)
{
    Vector.X = Vector.X * Scalar;
    Vector.Y = Vector.Y * Scalar;

    return Vector;
}

v2
operator+=(v2 &A, v2 B)
{
    A = A + B;
    return A;
}

b32
operator==(v2 A, v2 B)
{
    b32 Result = ((A.X == B.X) && (A.Y == B.Y));
    return Result;
}

// headers everyone needs
#include "tufty_math.h"
#include "myrandom.h"

#define STRING_LEN 250
#define MAX_TILE_TYPES 200
#define TILE_TYPES_ARRAY_LEN (MAX_TILE_TYPES + 1)
#define MAX_FACING_BITMAPS 10
#define FACING_BITMAPS_ARRAY_LEN (MAX_FACING_BITMAPS + 1)
#define MAX_UNDO_STEPS 100
#define MAX_ANIMATION_CELS 10
#define MAX_ANIMATIONS_PER_FACING 10
#define NIL 0

#define TILE_SIDE_M 1.4f
#define TILE_SIDE_PX 64.0f
#define TILES_IN_WORLD_Y 221 
#define TILES_IN_WORLD_X 393
#define TILES_IN_WORLD (TILES_IN_WORLD_Y * TILES_IN_WORLD_X)
#define TILES_IN_WORLD_NUM_BYTES (TILES_IN_WORLD * sizeof(s32))

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
static debug_read_cpu_timer *ReadCpuTimer;

static void
InitializeArena(arena *Arena, u8 *Start, mem_idx Size)
{
    Arena->pBase = Start;
    Arena->Size = Size;
    Arena->Used = 0;
}

static void
ResetArena(arena *Arena)
{
    memset(Arena->pBase, 0, Arena->Size);
    Arena->Used = 0;
}

static mem_idx
GetBytesToNextAlignment(arena Arena, mem_idx Alignment)
{
    if (Alignment == 0) {
        return 0;
    }
    mem_idx BytesToNextAlignment = 0;
    mem_idx ResultPointer = (mem_idx)Arena.pBase + Arena.Used;
    mem_idx AlignmentMask = Alignment-1;
    
    if (ResultPointer & AlignmentMask) {
        BytesToNextAlignment = Alignment - (ResultPointer & AlignmentMask);
    }

    assert(BytesToNextAlignment < Alignment);

    return BytesToNextAlignment;
}

static void *
ArenaPush_(arena *Arena, mem_idx NumBytes, mem_idx Alignment)
{
    if (Alignment > 0 && Alignment < 4) {
        Alignment = 16;
    }
    mem_idx BytesToNextAlignment = GetBytesToNextAlignment(*Arena, Alignment);
    mem_idx NumBytesWithAlignment = NumBytes + BytesToNextAlignment;
    assert(Arena->Used+NumBytesWithAlignment <= Arena->Size);
    void *Result = Arena->pBase + Arena->Used + BytesToNextAlignment;
    Arena->Used += NumBytesWithAlignment;

    return Result;
}

static void *
ArenaPushZero_(arena *Arena, mem_idx NumBytes, mem_idx Alignment)
{
    if (Alignment > 0 && Alignment < 4) {
        Alignment = 16;
    }
    mem_idx BytesToNextAlignment = GetBytesToNextAlignment(*Arena, Alignment);
    mem_idx NumBytesWithAlignment = NumBytes + BytesToNextAlignment;
    assert(Arena->Used+NumBytesWithAlignment <= Arena->Size);
    void *Result = Arena->pBase + Arena->Used + BytesToNextAlignment;
    memset(Result, 0, NumBytesWithAlignment);
    Arena->Used += NumBytesWithAlignment;

    return Result;
}

#if 0

#define PushArray(Arena, Type, Count) (Type *)ArenaPush_((Arena), sizeof(Type) * (Count), alignof(Type))
#define PushArrayZero(Arena, Type, Count) (Type *)ArenaPushZero_((Arena), sizeof(Type) * (Count), alignof(Type))
#define PushStruct(Arena, Type) (Type *)ArenaPush_((Arena), sizeof(Type), alignof(Type))
#define PushStructZero(Arena, Type) (Type *)ArenaPushZero_((Arena), sizeof(Type), alignof(Type))
#define PushPackedArray(Arena, Type, Count) (Type *)ArenaPush_((Arena), sizeof(Type) * (Count), 0)
#define PushPackedArrayZero(Arena, Type, Count) (Type *)ArenaPushZero_((Arena), sizeof(Type) * (Count), 0)
#define PushPackedStruct(Arena, Type) (Type *)ArenaPush_((Arena), sizeof(Type), 0)
#define PushPackedStructZero(Arena, Type) (Type *)ArenaPushZero_((Arena), sizeof(Type), 0)
#define GetByteCountForTypedArray(Type, Num) (sizeof(Type) * (Num))

#else

#define PushArray(Arena, Type, Count) (Type *)ArenaPushZero_((Arena), sizeof(Type) * (Count), alignof(Type))
#define PushStruct(Arena, Type) (Type *)ArenaPushZero_((Arena), sizeof(Type), alignof(Type))
#define PushPackedArray(Arena, Type, Count) (Type *)ArenaPushZero_((Arena), sizeof(Type) * (Count), 0)
#define PushPackedStruct(Arena, Type) (Type *)ArenaPushZero_((Arena), sizeof(Type), 0)
#define GetByteCountForTypedArray(Type, Num) (sizeof(Type) * (Num))

#endif

static void
InitSubArena(arena *pSubArena, arena *pSourceArena, mem_idx Size)
{
    pSubArena->Size = Size;
    pSubArena->pBase = PushArray(pSourceArena, u8, Size);
    pSubArena->Used = 0;
}

struct color
{
    f32 R, G, B, A;
};

struct fading_text
{
    char *Text;
    f32 X, Y;
    u64 FrameCount, FrameDuration;
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

enum wait_state
{
    NOTHING,
    WAITING,
    READY_TO_RELOAD,
    RELOADED
};

struct reload_state
{
    enum wait_state WaitState;
    int WaitedFrames;
    u64 LastWriteTime;
};

struct meta_bitmap
{
    char Filepath[STRING_LEN];
    reload_state ReloadState;
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
    int Len; // This is the max number of values it can hold, not the current number it's holding
    meta_bitmap *Data;
    int NextEmptySlot;
    arena PixelsArena;

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
    Get(int Idx) const
    {
        if(Idx > 0 && Idx < Len) {
            return(Data + Idx);
        } 
        else {
            return(Data + 0);
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
    FindByFilepath(char *SearchFilepath) const
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
    CountNonEmptySlots(void) const
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
    FullClear(void)
    {
        NextEmptySlot = 1;
        for (int Slot = 1; Slot < Len; ++Slot) {
            Data[Slot] = {};
        }
    }

    void
    ClearButLeaveFilepaths(void) 
    {
        for (int Slot = 1; Slot < Len; ++Slot) {
            Data[Slot].ReloadState.LastWriteTime = 0;
            Data[Slot].ReloadState.WaitState = NOTHING;
            Data[Slot].ReloadState.WaitedFrames = 0;
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
                    pIt->Bitmap.Buffer.Data = PushArray(&PixelsArena, u8, Size);
                    pIt->ReloadState.WaitState = READY_TO_RELOAD;
                }
            }
        }
    }   

    bool
    IsNilBitmap(meta_bitmap *pToCheck) const
    {
        bool Result = (pToCheck - Data == 0);
        return Result;
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
    reload_state ReloadState;
    u64 LastUpdateTime;
    arr_meta_bitmap TileTypes;
    f32 TileSideInMeters;
    f32 TileSideInPixels;
    arena TileValuesArena;
    s32 *TileValues;
};

struct menu_bitmap
{
    v2 TileMin;
    v2 TileMax;
    int FacingIdx;
    int Slot;
    b32 DrawThis;
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
    int NumTileTypes;
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
    assert(Header->BitsPerPixel == 32);
    Bitmap->BytesPerPixel = Header->BitsPerPixel / 8;
    Bitmap->Pitch = Bitmap->Width * Bitmap->BytesPerPixel;
}


struct facing_bitmaps
{
    reload_state ReloadState;
    int DrawThis;
    arr_meta_bitmap MetaBitmaps;
};

struct animation
{
    char Filepath[STRING_LEN + 1];
    bitmap Cels[MAX_ANIMATION_CELS + 1];
    int MsForCels[MAX_ANIMATION_CELS + 1];
};

struct arr_animation
{
    animation Animations[MAX_ANIMATIONS_PER_FACING + 1];
    arena *pContainingArena;
};

struct animation_state
{
    int CurrAnimationIdx;
    int CurrCelIdx;
    f32 MsInCurrCel;
};

struct player
{
    v2 Pos;
    v2 Velo;
    int IsFacing;
    int OldFacing;
    b32 TryingToMove;
    arena AnimationsArena;
    animation_state AnimationState;
    facing_bitmaps FacingBitmaps[4];
    arr_animation FacingAnimationArray[4];
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
    v2 ThisFrameCoords;
    v2 LastFrameCoords;
    u64 HoveredFrames;
    int HeldFacingIdx;
    int HeldSlot;
};

enum which_editor
{
    NO_EDITOR = 0,
    TILE,
    PLAYER
};

struct undo_step
{
    s32 *TileValues;
};

struct undo_state
{
    arena SingleFrameUndoArena;
    arena UndoStepsArena;
    int HeadIdx;
    int CurrIdx;
    undo_step *UndoStepsArr[MAX_UNDO_STEPS];
};

struct editor_state
{
    which_editor WhichEditor;
    cursor_state CursorState;
    b32 PrintRowsCols;
    undo_state UndoState;
};

struct game_state
{
    random_series RandomSeries;
    arena WorldArena;
    arena FailBitmapsArena;
    bitmap RedX;
    editor_state EditorState;
    tile_map TileMap;
    player Player;
    v2 CameraPos;
    v2 CameraVelo;
};

static b32
IsInRect(v2 Point, v2 RectMin, v2 RectMax)
{
    b32 Result = ( (Point.X >= RectMin.X) &&
                   (Point.Y >= RectMin.Y) &&
                   (Point.X < RectMax.X) &&
                   (Point.Y < RectMax.Y) );
    return Result;
}

// for interacting with stb easyfont
struct vertex
{
    f32 X, Y, Z;
    u32 Color;
};

struct quad
{
    vertex TopLeft, TopRight, BottomRight, BottomLeft;
};

// global variables
static meta_bitmap *pGlobalBagel;
static game_controller_input *GlobalKeyboardController;
static dev_keys *GlobalDevKeys;
static game_mouse_input *pGlobalMouse;

// libraries
#define CUTE_ASEPRITE_IMPLEMENTATION
#include "cute_aseprite.h"
#include "stb_easy_font.h"

// headers
#include "bitmap.h"
#include "animation.h"
#include "undo.h"
#include "tile.h"
#include "draw.h"
#include "serialize.h"

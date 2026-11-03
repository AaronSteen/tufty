#pragma once

#if TUFTY_INTERNAL
#include <stdio.h>
#endif
#include "tufty_platform.h"
#include <math.h>
#include <string.h>

#define CUTE_ASEPRITE_IMPLEMENTATION
#include "cute_aseprite.h"

#define STRING_LEN 250
#define MAX_TILE_TYPES 200
#define MAX_TILE_VALUES (100 * 100)
#define TILE_TYPES_ARRAY_LEN (MAX_TILE_TYPES + 1)
#define MAX_FACING_BITMAPS 10
#define FACING_BITMAPS_ARRAY_LEN (MAX_FACING_BITMAPS + 1)
#define MAX_UNDO_STEPS 100
#define MAX_ANIMATION_CELS 10
#define MAX_ANIMATIONS_PER_FACING 10
#define NIL 0

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
    u8 *pBase;
    mem_idx Size;
    mem_idx Used;
};

// #define NUM_SCRATCHES 10
//
// struct scratch_arena
// {
//     b32 IsFree;
//     arena Arena;
// };
//
// struct scratch_header
// {
//     u32 Count;
//     scratch_arena ScratchArenas[NUM_SCRATCHES];
// };

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

    Assert(BytesToNextAlignment < Alignment);

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
    Assert(Arena->Used+NumBytesWithAlignment <= Arena->Size);
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
    Assert(Arena->Used+NumBytesWithAlignment <= Arena->Size);
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
                    pIt->Bitmap.Buffer.Data = PushArray(&BitmapsArena, u8, Size);
                    pIt->ReloadState.WaitState = READY_TO_RELOAD;
                }
            }
        }
    }   

    bool
    IsNilBitmap(meta_bitmap *pToCheck) const
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
    reload_state ReloadState;
    u64 LastUpdateTime;
    int NumRows;
    int NumCols;
    arr_meta_bitmap TileTypes;
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
    reload_state ReloadState;
    int DrawThis;
    arr_meta_bitmap MetaBitmaps;
};

// Animation heirarchy.
//  animation_state contains animations
//      animation contains Cels, which are bitmaps.
//  
//
//          

struct animation
{
    char Filepath[STRING_LEN + 1];
    bitmap Cels[MAX_ANIMATION_CELS + 1];
    int MsForCels[MAX_ANIMATION_CELS + 1];
    int Width, Height;
};

struct animation_state
{
    animation Animations[MAX_ANIMATIONS_PER_FACING + 1];
    arena *pContainingArena;
    int CurrAnimationIdx;
    int CurrCelIdx;
    f32 MsInCurrCel;
};

struct player
{
    v2 Position;
    int IsFacing;
    facing_bitmaps FacingBitmaps[4];
    arena AnimationsArena;
    animation_state FacingAnimationStates[4];
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
    int NumTileRows;
    int NumTileCols;
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
    editor_state EditorState;
    tile_map TileMap;
    player Player;
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

static b32
IsAnimationSlotFree(const animation *pAnimation)
{
    b32 Result = false;
    if (strlen(pAnimation->Filepath) > 0) {
        Result = true;
    }
    return Result;
}

static int
GetNextFreeAnimationSlot(animation_state *pAnimationState)
{
    int Result = 0;
    for (int Slot = 1; Slot <= MAX_ANIMATIONS_PER_FACING; ++Slot) {
        animation *pAnimation = pAnimationState->Animations + Slot;
        if (IsAnimationSlotFree(pAnimation)) {
            Result = Slot;
            break;
        }
    }
    return Result;
}

static int
LoadAnimation(animation_state *pAnimationState, const char *pFilepath)
{
    // terminology.
    //      "Cels" comprise "Animations". Cels are just bitmaps.
    //      When an animation is played, the first Cel is shown on the screen for
    //          its specified ms. When that ms has elapsed, the next Cel is shown, etc.
    //      cute_aseprite uses "Cel" differently: To it, a "cel" is a single layer, in
    //          the photoshop sense. Multiple "cels" can be layered on top of each other 
    //          in Aseprite to form a "frame".
    //      So, our "cel" corresponds to cute_aseprite's "frame". But we don't
    //          want to use the term frame since that term already means
    //          something different in the context of our engine.
    int NextFreeSlot = GetNextFreeAnimationSlot(pAnimationState); 
    if (NextFreeSlot == NIL) { return NIL; }

    ase_t *pAseprite = cute_aseprite_load_from_file(pFilepath, NULL);
    if (pAseprite == nullptr) { return NIL; }
    if (pAseprite->frame_count > MAX_ANIMATION_CELS) { return NIL; }

    animation *pNewAnimation = pAnimationState->Animations + NextFreeSlot;
    strncpy(pNewAnimation->Filepath, pFilepath, STRING_LEN + 1);
    pNewAnimation->Width = pAseprite->w;
    pNewAnimation->Height = pAseprite->h;

    int BytesPerPixel = 4;
    // Aseprite indexes from 0; we reserve 0 for NIL
    for (int AsepriteIdx = 0; AsepriteIdx < pAseprite->frame_count; ++AsepriteIdx) {
        ase_frame_t *pAsepriteCel = pAseprite->frames + AsepriteIdx;
        int GameIdx = AsepriteIdx + 1;
        pNewAnimation->MsForCels[GameIdx] = pAsepriteCel->duration_milliseconds;

        mem_idx NumBytesForPixels = pNewAnimation->Width * pNewAnimation->Height * BytesPerPixel;
        pNewAnimation->Cels[GameIdx].Pixels = PushArray(pAnimationState->pContainingArena, u8, NumBytesForPixels);
        memcpy(pNewAnimation->Cels[GameIdx].Pixels, pAsepriteCel->pixels, NumBytesForPixels);
    }

    cute_aseprite_free(pAseprite);
    return NextFreeSlot;
}

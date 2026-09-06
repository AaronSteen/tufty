#pragma once

// This file defines the points of connection between the game and the platform layer. 
// The job of the platform layer is to communicate with the OS. 
//      - open the window
//      - read, write, and free files
//      - collect player input
//      - get a framebuffer from the OS
//      - get an audio buffer from the OS
//      - time stuff for performance profiling
// Therefore, the task of porting the game to a different OS means writing the platform layer for that OS.
// Meanwhile, the game does game stuff:
//      - interpret player input
//      - simulate the world state
//      - write the pixel data into the framebuffer
//      - write the audio data into the audio buffer
// So once per frame: 
//      - platform passes input and audio and framebuffer pointers to game
//      - game writes to audio and framebuffers
//      - platform gets OS to display the framebuffer and play the audio.
// The game doesn't care what platform layer it works with. It merely expects the following:
//      - To be able to call file I/O functions
//      - To get input once per frame
//      - To get passed audio and framebuffer pointers once per frame.
// 
// Therefore, this file mainly contains two varieties of definition:
//      1. Services the platform layer provides to the game. For now, just file I/O.
//      2. Services the game provides to the platform layer:
//          - GameUpdateAndRender (interpret input, simulate world state, write pixels into framebuffer)
//          - GameGetSoundSamples (write audio samples into audio buffer).
//
//      This file contains the struct definitions e.g., buffer struct, game_controller_input struct, that both
//          sides rely on.
//      It also contains function signatures. But the actual definitions of these functions are in the separate
//          game and platform cpp files.
//
//  And also we have some typedefs that both game and platform use.


// *** TYPES AND MACROS HERE SO BOTH GAME AND PLATFORM CAN USE ***
extern "C" {

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef s32 b32;
typedef float f32;
typedef double f64;
typedef size_t mem_idx;

#define Assert(Expression) if(!(Expression)) {*(volatile int *)0 = 1;}

#define ArrayCount(Array) (sizeof(Array) / sizeof(Array[0]))

#define Kilobytes(N) (((mem_idx)N) * 1024)
#define Megabytes(N) (Kilobytes(N) * 1024)
#define Gigabytes(N) (Megabytes(N) * 1024)
#define Terabytes(N) (Gigabytes(N) * 1024)

u32
SafeTruncateU64ToU32(u64 SixtyFour)
{
    Assert(SixtyFour <= 0xFFFFFFFF);
    u32 Result = (u32)SixtyFour;

    return(Result);
}

// Need a basic buffer struct for communication
struct buffer
{
    u8 *Start;
    mem_idx Size;
};

// *** SERVICES THE PLATFORM PROVIDES TO THE GAME ***

#if TUFTY_INTERNAL
// Casey says we need to write better versions for any shipping version of the game, since these will
//      block the thread and the write doesn't protect against lost data


typedef struct debug_read_file_result
{
    u32 ContentsSize;
    void *Contents;
} debug_read_file_result;

#define DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(u8 *GameFilenameArrayStart, mem_idx GameFilenameArraySize, char *DirName, u32 *NumFilesFound)
typedef DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(debug_platform_get_list_of_dir_contents);

#define DEBUG_PLATFORM_GET_FILE_SIZE(name) u32 name(char *Filename)
typedef DEBUG_PLATFORM_GET_FILE_SIZE(debug_platform_get_file_size);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(char *Filename, u32 MemorySize, void *Memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

#define DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename)
typedef DEBUG_PLATFORM_GET_FILE_WRITE_TIME(debug_platform_get_file_write_time);

// this returns either the number of bytes read, or 0 if the file was missing, too big, or locked
#define DEBUG_PLATFORM_READ_FILE_INTO(name) u32 name(char *Filename, u32 DestSize, void *Dest)
typedef DEBUG_PLATFORM_READ_FILE_INTO(debug_platform_read_file_into);

#endif

// *** SERVICES THE GAME PROVIDES TO THE PLATFORM ***

typedef struct game_offscreen_buffer
{
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
} game_offscreen_buffer;

typedef struct game_sound_output_buffer
{
    int SamplesPerSecond;
    int SampleCount;
    s16 *Samples;
} game_sound_output_buffer;

typedef struct game_button_state
{
    int HalfTransitionCount;
    b32 EndedDown;
} game_button_state;

typedef struct game_controller_input
{
    b32 IsConnected;
    b32 IsAnalog;
    f32 StickAverageX;
    f32 StickAverageY;

    union
    {
        game_button_state Buttons[12];
        struct
        {
            game_button_state MoveUp;
            game_button_state MoveDown;
            game_button_state MoveLeft;
            game_button_state MoveRight;
            
            game_button_state ActionUp;
            game_button_state ActionDown;
            game_button_state ActionLeft;
            game_button_state ActionRight;
            
            game_button_state LeftShoulder;
            game_button_state RightShoulder;

            game_button_state Back;
            game_button_state Start;
        };
    };
} game_controller_input;

typedef struct game_mouse_input
{
    s32 X, Y;

    // Unused for now
    s32 WheelDelta;

    union
    {
        game_button_state Buttons[3];
        struct
        {
            game_button_state Primary;
            game_button_state WheelClick;
            game_button_state Secondary;
        };
    };
} game_mouse_input;

typedef struct function_keys
{
    // struct to store state of the function keys, which we use for dev tool purposes.
    // Note(Aaron): We do not use F11 or F12 because those are not on my special keyboard :)
    union
    {
        game_button_state Keys[10];
        struct
        {
            game_button_state F1;
            game_button_state F2;
            game_button_state F3;
            game_button_state F4;
            game_button_state F5;
            game_button_state F6;
            game_button_state F7;
            game_button_state F8;
            game_button_state F9;
            game_button_state F10;
        };
    };
} function_keys;

typedef struct game_input
{
    game_mouse_input Mouse;

    f32 dtForFrame;
    f32 Fps;
    u64 CpuTimerReading;

    game_controller_input Controllers[5];
    function_keys FunctionKeys;
} game_input;

game_controller_input *
GetController(game_input *Input, int ControllerIdx)
{
    Assert(ControllerIdx < ArrayCount(Input->Controllers));

    game_controller_input *Result = &Input->Controllers[ControllerIdx];

    return(Result);
}

typedef struct game_memory
{
    b32 IsInitialized;

    mem_idx PermanentStorageSize;
    void *PermanentStorage; // platform must clear to zero at startup

    mem_idx TransientStorageSize;
    void *TransientStorage; // platform must clear to zero at startup

    debug_platform_free_file_memory *DEBUGPlatformFreeFileMemory;
    debug_platform_read_entire_file *DEBUGPlatformReadEntireFile;
    debug_platform_write_entire_file *DEBUGPlatformWriteEntireFile;
    debug_platform_get_file_size *DEBUGPlatformGetFileSize;
    debug_platform_read_file_into *DEBUGPlatformReadFileInto;
    debug_platform_get_file_write_time *DEBUGPlatformGetFileWriteTime;
    debug_platform_get_list_of_dir_contents *DEBUGPlatformGetListOfDirContents;
} game_memory;

#define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

#define GAME_GET_SOUND_SAMPLES(name) void name(game_memory *Memory, game_sound_output_buffer *SoundBuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);
}


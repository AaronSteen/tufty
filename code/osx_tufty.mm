#import <Cocoa/Cocoa.h>

#include <sys/mman.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <limits.h>
#include <AudioToolbox/AudioToolbox.h>
#include <dirent.h>
#include <strings.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "tufty_platform.h"

struct osx_offscreen_buffer
{
    // Pixels are always 32 bits wide, memory order BB GG RR XX
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
};

struct osx_game_code
{
    void *GameCodeDylib;
    u64 DylibLastWriteTime;
    char TempDylibName[PATH_MAX];

    game_update_and_render *UpdateAndRender;
    game_get_sound_samples *GetSoundSamples;

    b32 IsValid;
};

struct osx_audio
{
    AudioQueueRef Queue;
    AudioQueueBufferRef Buffers[2];
    AudioStreamBasicDescription Format;
    u32 SamplesPerSecond;
    u32 NumBufferSamples;
    b32 IsPlaying;

    s16 *SampleRing;
    u32 RingCapacity;
    volatile u32 RingWriteCursor;
    volatile u32 RingReadCursor;
};

static b32 GlobalRunning;
static b32 GlobalPause;
static f32 GlobalNanosPerTick;
static osx_offscreen_buffer GlobalBackbuffer;
static char GlobalEXEPath[PATH_MAX];
static u32 GlobalTempDylibCounter;
static game_controller_input *GlobalKeyboardController;
static game_mouse_input *GlobalMouse;
static dev_keys *GlobalDevKeys;
static osx_audio GlobalAudio;
static b32 GlobalDialogWasOpen;

static void
OSXResizeBackbuffer(osx_offscreen_buffer *Buffer, int Width, int Height)
{

    if(Buffer->Memory)
    {
        munmap(Buffer->Memory, Buffer->Pitch * Buffer->Height);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->BytesPerPixel = 4;
    Buffer->Pitch = Width * Buffer->BytesPerPixel;

    int BitmapMemorySize = Buffer->Pitch * Buffer->Height;
    Buffer->Memory = mmap(0, BitmapMemorySize,
                            PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS,
                            -1, 0);

}

static u64
ReadCpuTimer(void)
{
    return(mach_absolute_time());
}

static f32
OSXGetSecondsElapsed(u64 Start, u64 End)
{
    f32 NanosecondsElapsed = GlobalNanosPerTick * (f32)(End-Start);
    f32 Result = NanosecondsElapsed / 1.0e9f;
    return(Result);
}

static void
OSXProcessButtonMessage(game_button_state *NewState, b32 IsDown)
{
    if(NewState->EndedDown != IsDown)
    {
        NewState->EndedDown = IsDown;
        ++NewState->HalfTransitionCount;
    }
}

/* #define DEBUG_PLATFORM_GET_FILE_SIZE(name) mem_idx name(char *Filename) */
DEBUG_PLATFORM_GET_FILE_SIZE(DEBUGPlatformGetFileSize)
{
    mem_idx Result = 0;
    struct stat FileStat;
    if(stat(Filepath, &FileStat) == 0)
    {
        Result = SafeTruncateU64ToU32(FileStat.st_size);
    }

    return(Result);
}

/* #define DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename) */
DEBUG_PLATFORM_GET_FILE_WRITE_TIME(DEBUGPlatformGetFileWriteTime)
{
    u64 Result = 0;

    struct stat FileStat;
    if(stat(Filename, &FileStat) == 0)
    {
        Result = ((u64)FileStat.st_mtimespec.tv_sec * 1000000000ull +
                  (u64)FileStat.st_mtimespec.tv_nsec);
    }

    return(Result);
}

// #define DEBUG_PLATFORM_READ_FILE_INTO(name) u32 name(char *Filename, u32 DestSize, void *Dest)
DEBUG_PLATFORM_READ_FILE_INTO(DEBUGPlatformReadFileInto)
{
    u32 Result = 0;

    int FileHandle = open(Filepath, O_RDONLY);
    if(FileHandle != -1)
    {
        struct stat FileStat;
        if(fstat(FileHandle, &FileStat) == 0)
        {
            u32 FileSize32 = SafeTruncateU64ToU32(FileStat.st_size);
            if(FileSize32 <= DestSize)
            {
                ssize_t BytesRead = read(FileHandle, Dest, FileSize32);
                if(BytesRead == (ssize_t)FileSize32)
                {
                    Result = FileSize32;
                }
            }
        }

        close(FileHandle);
    }

    return(Result);
}

/* #define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory) */
DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
    if(Memory)
    {
        free(Memory);
    }
}

/* #define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename) */
DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
    debug_read_file_result Result = {};

    int FileHandle = open(Filename, O_RDONLY);
    if(FileHandle != -1)
    {
        struct stat FileStat;
        if(fstat(FileHandle, & FileStat) == 0)
        {
            u32 FileSize32 = SafeTruncateU64ToU32(FileStat.st_size);
            Result.Contents = malloc(FileSize32);
            if(Result.Contents)
            {
                ssize_t BytesRead = read(FileHandle, Result.Contents, FileSize32);
                if(BytesRead == (ssize_t)FileSize32)
                {
                    Result.ContentsSize = FileSize32;
                } 
                else
                {
                    DEBUGPlatformFreeFileMemory(Result.Contents);
                    Result.Contents = 0;
                }
            }
        }

        close(FileHandle);
    }

    return(Result);
}

/* #define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) mem_idx name(char *Filepath, mem_idx BufferSize, void *WriteHere) */
DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
    mem_idx BytesWritten = 0;
    int FileHandle = open(Filepath, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(FileHandle == -1)
    {
        return(BytesWritten);
    }

    BytesWritten = write(FileHandle, WriteHere, BufferSize);
    close(FileHandle);
    return(BytesWritten);
}


/* #define DEBUG_PLATFORM_GET_DIR_WRITE_TIME(name) u64 name(char *Dirname) */
DEBUG_PLATFORM_GET_DIR_WRITE_TIME(DEBUGPlatformGetDirWriteTime)
{
    // POSIX treats dirs as files so we can just call GetFileWriteTime
    u64 Result = DEBUGPlatformGetFileWriteTime(Dirname);
    return(Result);
    
}

/* #define DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(buffer *GamePackedFilenames, char *DirName, int *NumFilesFound) */
DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(DEBUGPlatformGetListOfDirContents)
{
    *NumFilesFound = 0;

    DIR *Dir = opendir(DirName);
    if(!Dir)
    {
        return;
    }

    char *Extension = ".bmp";
    int ExtensionLength = 4;
    u8 *GameCursor = GamePackedFilenames->Data;
    u8 *GameEnd = GamePackedFilenames->Data + GamePackedFilenames->Size;

    struct dirent *Entry;
    while((Entry = readdir(Dir)) != 0)
    {
        char *Name = Entry->d_name;
        int NameLength = (int)strlen(Name);

        // Skip ".", "..", ".DS_Store" etc.
        b32 IsHidden = (Name[0] == '.');

        b32 IsBmp = ( (NameLength > ExtensionLength) &&
                        (strcasecmp(Name + NameLength - ExtensionLength, Extension) == 0) );

        if(!IsHidden && IsBmp)
        {
            // If copying this filename into the buffer passed in by the game would overflow it
            if(GameCursor + NameLength + 1 > GameEnd)
            {
                *NumFilesFound = 0;
                break;
            }

            for(int CharIdx = 0;
                CharIdx < NameLength;
                ++CharIdx)
            {
                *GameCursor ++ = Name[CharIdx];
            }
            *GameCursor++ = 0;
            *NumFilesFound += 1;
        }
    }

    closedir(Dir);
}

/* #define DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(name) int name(char *Dest, mem_idx DestSize, b32 IsSave) */
DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(DEBUGPlatformGetFilepathFromDialog)
{
    GlobalDialogWasOpen = true;
    int Result = 0;

    @autoreleasepool
    {
        NSSavePanel *Panel = 0;
        if(IsSave)
        {
            Panel = [NSSavePanel savePanel];
        }
        else
        {
            NSOpenPanel *OpenPanel = [NSOpenPanel openPanel];
            [OpenPanel setCanChooseFiles:YES];
            [OpenPanel setCanChooseDirectories:NO];
            [OpenPanel setAllowsMultipleSelection:NO];
            Panel = OpenPanel;
        }

        UTType *TileMapType = [UTType typeWithFilenameExtension:@"tilemap"];
        if(TileMapType)
        {
            [Panel setAllowedContentTypes:@[TileMapType]];
        }

        if([Panel runModal] == NSModalResponseOK)
        {
            const char *Path = [[Panel URL] fileSystemRepresentation];
            mem_idx PathLength = strlen(Path);
            if(PathLength < DestSize)
            {
                memcpy(Dest, Path, PathLength + 1);
                Result = 1;
            }
        }
    }

    // Ctrl and S/L keys were released while the panel was open, so the key up message
    //      was never sent to the game; we therefore clear keyboard input to zero below,
    //      and mouse for good measure
    *GlobalKeyboardController = {};
    *GlobalDevKeys = {};
    *GlobalMouse = {};

    return(Result);
}

static void
OSXBuildEXEPathFilename(char *Filename, int DestCount, char *Dest)
{
    snprintf(Dest, DestCount, "%s%s", GlobalEXEPath, Filename);
}

static void
OSXGetEXEPath(void)
{
    char PathBuffer[PATH_MAX];
    u32 BufferSize = sizeof(PathBuffer);
    if(_NSGetExecutablePath(PathBuffer, &BufferSize) == 0)
    {
        char *OnePastLastSlash = PathBuffer;
        for(char *Scan = PathBuffer;
            *Scan;
            ++Scan)
        {
            if(*Scan == '/')
            {
                OnePastLastSlash = Scan + 1;
            }
        }

        mem_idx DirLength = OnePastLastSlash - PathBuffer;
        for(mem_idx Index = 0;
            Index < DirLength;
            ++Index)
        {
            GlobalEXEPath[Index] = PathBuffer[Index];
        }
        GlobalEXEPath[DirLength] = 0;
    }
}


static b32
OSXCopyFile(char *SourceName, char *DestName)
{
    b32 Result = false;

    int Source = open(SourceName, O_RDONLY);
    if(Source != -1)
    {
        int Dest = open(DestName, O_WRONLY|O_CREAT|O_TRUNC, 0755);
        if(Dest != -1)
        {
            char CopyBuffer[65536];
            ssize_t BytesRead;
            Result = true;
            while((BytesRead = read(Source, CopyBuffer, sizeof(CopyBuffer))) > 0)
            {
                if(write(Dest, CopyBuffer, BytesRead) != BytesRead)
                {
                    Result = false;
                    break;
                }
            }
            close(Dest);
        }
        close(Source);
    }

    return(Result);
}

static osx_game_code
OSXLoadGameCode(char *SourceDylibName)
{
    osx_game_code Result = {};

    Result.DylibLastWriteTime = DEBUGPlatformGetFileWriteTime(SourceDylibName);

    char TempFilename[64];
    snprintf(TempFilename, sizeof(TempFilename), "tufty_temp_%u.dylib", GlobalTempDylibCounter);
    OSXBuildEXEPathFilename(TempFilename, sizeof(Result.TempDylibName), Result.TempDylibName);

    if(OSXCopyFile(SourceDylibName, Result.TempDylibName))
    {
        Result.GameCodeDylib = dlopen(Result.TempDylibName, RTLD_NOW|RTLD_LOCAL);
        if(Result.GameCodeDylib)
        {
            Result.UpdateAndRender = (game_update_and_render *)
                dlsym(Result.GameCodeDylib, "GameUpdateAndRender");

            Result.GetSoundSamples = (game_get_sound_samples *)
                dlsym(Result.GameCodeDylib, "GameGetSoundSamples");

            Result.IsValid = (Result.UpdateAndRender && Result.GetSoundSamples);
        }
        else
        {
            printf("dlopen failed: %s\n", dlerror());
        }
    }

    if(!Result.IsValid)
    {
        Result.UpdateAndRender = 0;
        Result.GetSoundSamples = 0;
    }

    return(Result);
}

static void
OSXUnloadGameCode(osx_game_code *GameCode)
{
    if(GameCode->GameCodeDylib)
    { 
        dlclose(GameCode->GameCodeDylib);
        GameCode->GameCodeDylib = 0;
    }

    if(GameCode->TempDylibName[0])
    {
        unlink(GameCode->TempDylibName);
    }

    GameCode->IsValid = false;
    GameCode->UpdateAndRender = 0;
    GameCode->GetSoundSamples = 0;
}

#define OSX_VK_A       0x00
#define OSX_VK_S       0x01
#define OSX_VK_D       0x02
#define OSX_VK_Q       0x0C
#define OSX_VK_W       0x0D
#define OSX_VK_E       0x0E
#define OSX_VK_L       0x25
#define OSX_VK_J       0x26
#define OSX_VK_K       0x28
#define OSX_VK_I       0x22
#define OSX_VK_P       0x23
#define OSX_VK_SPACE   0x31
#define OSX_VK_ESCAPE  0x35
#define OSX_VK_F1      0x7A
#define OSX_VK_F2      0x78
#define OSX_VK_F3      0x63
#define OSX_VK_F4      0x76
#define OSX_VK_F5      0x60
#define OSX_VK_F6      0x61
#define OSX_VK_F7      0x62
#define OSX_VK_F8      0x64
#define OSX_VK_F9      0x65
#define OSX_VK_F10     0x6D

static void
OSXProcessPendingEvents(NSApplication *App)
{
    for(;;)
    {
        NSEvent *Event = [App nextEventMatchingMask:NSEventMaskAny
                                        untilDate:nil
                                        inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
        if(!Event) { break; }

        NSEventType Type = [Event type];
        switch(Type)
        {
            case NSEventTypeKeyDown:
            case NSEventTypeKeyUp:
            {
                b32 IsDown = (Type == NSEventTypeKeyDown);
                unsigned short KeyCode = [Event keyCode];
                b32 CommandIsDown = (([Event modifierFlags] & NSEventModifierFlagCommand) != 0);

                if([Event isARepeat]) { break; }

                game_controller_input *Keyboard = GlobalKeyboardController;
                if(Keyboard)
                {
                    switch(KeyCode)
                    {
                        case OSX_VK_W: OSXProcessButtonMessage(&Keyboard->MoveUp, IsDown); break;
                        case OSX_VK_A: OSXProcessButtonMessage(&Keyboard->MoveLeft, IsDown); break;
                        case OSX_VK_S: OSXProcessButtonMessage(&Keyboard->MoveDown, IsDown); break;
                        case OSX_VK_D: OSXProcessButtonMessage(&Keyboard->MoveRight, IsDown); break;
                        case OSX_VK_Q: OSXProcessButtonMessage(&Keyboard->LeftShoulder, IsDown); break;
                        case OSX_VK_E: OSXProcessButtonMessage(&Keyboard->RightShoulder, IsDown); break;
                        case OSX_VK_I: OSXProcessButtonMessage(&Keyboard->ActionUp, IsDown); break;
                        case OSX_VK_J: OSXProcessButtonMessage(&Keyboard->ActionLeft, IsDown); break;
                        case OSX_VK_K: OSXProcessButtonMessage(&Keyboard->ActionDown, IsDown); break;
                        case OSX_VK_L: OSXProcessButtonMessage(&Keyboard->ActionRight, IsDown); break;
                        case OSX_VK_ESCAPE: OSXProcessButtonMessage(&Keyboard->Start, IsDown); break;
                        case OSX_VK_SPACE: OSXProcessButtonMessage(&Keyboard->Back, IsDown); break;

#if TUFTY_INTERNAL
                        case OSX_VK_P:
                        {
                            if(IsDown)
                            {
                                GlobalPause = !GlobalPause;
                            }
                        } break;
#endif


                        default: break;
                    }
                }

                dev_keys *DevKeys = GlobalDevKeys;
                if(DevKeys)
                {
                    switch(KeyCode)
                    {
                        case OSX_VK_F1: OSXProcessButtonMessage(&DevKeys->F1, IsDown); break;
                        case OSX_VK_F2: OSXProcessButtonMessage(&DevKeys->F2, IsDown); break;
                        case OSX_VK_F3: OSXProcessButtonMessage(&DevKeys->F3, IsDown); break;
                        case OSX_VK_F4: OSXProcessButtonMessage(&DevKeys->F4, IsDown); break;
                        case OSX_VK_F5: OSXProcessButtonMessage(&DevKeys->F5, IsDown); break;
                        case OSX_VK_F6: OSXProcessButtonMessage(&DevKeys->F6, IsDown); break;
                        case OSX_VK_F7: OSXProcessButtonMessage(&DevKeys->F7, IsDown); break;
                        case OSX_VK_F8: OSXProcessButtonMessage(&DevKeys->F8, IsDown); break;
                        case OSX_VK_F9: OSXProcessButtonMessage(&DevKeys->F9, IsDown); break;
                        case OSX_VK_F10: OSXProcessButtonMessage(&DevKeys->F10, IsDown); break;
                    }
                }

                if(IsDown && CommandIsDown && (KeyCode == OSX_VK_Q))
                {
                    GlobalRunning = false;
                } 

            } break;

            case NSEventTypeLeftMouseDown: case NSEventTypeLeftMouseUp:
            case NSEventTypeOtherMouseDown: case NSEventTypeOtherMouseUp:
            case NSEventTypeRightMouseDown: case NSEventTypeRightMouseUp:
            {
                b32 IsDown = ((Type == NSEventTypeLeftMouseDown) ||
                                (Type == NSEventTypeOtherMouseDown) ||
                                (Type == NSEventTypeRightMouseDown));
                
                if(GlobalMouse)
                {
                    game_button_state *Button = &GlobalMouse->Primary;
                    if((Type == NSEventTypeOtherMouseDown) || (Type == NSEventTypeOtherMouseUp))
                    {
                        Button = &GlobalMouse->WheelClick;
                    }
                    else if((Type == NSEventTypeRightMouseDown) || (Type == NSEventTypeRightMouseUp))
                    {
                        Button = &GlobalMouse->Secondary;
                    }

                    OSXProcessButtonMessage(Button, IsDown);
                }

                [App sendEvent:Event];
            } break;

            case NSEventTypeFlagsChanged:
            {
                // On Mac we use Command key instead of Control key as on Windows
                b32 CmdIsDown = (([Event modifierFlags] & NSEventModifierFlagCommand) != 0);
                if(GlobalDevKeys)
                {
                    OSXProcessButtonMessage(&GlobalDevKeys->Ctrl, CmdIsDown);
                }
                [App sendEvent:Event];
            } break;
             
            default:
            {
                [App sendEvent:Event];
            } break;
        }
    }
}

static void
OSXAudioCallback(void *UserData, AudioQueueRef Queue, AudioQueueBufferRef Buffer)
{
    osx_audio *Audio = (osx_audio *)UserData;

    u32 SamplesToWrite = Buffer->mAudioDataBytesCapacity / (sizeof(s16) * 2);
    s16 *Dest = (s16 *)Buffer->mAudioData;

    u32 ReadCursor = Audio->RingReadCursor;
    u32 WriteCursor = Audio->RingWriteCursor;

    for(u32 SampleIdx = 0;
        SampleIdx < SamplesToWrite;
        ++SampleIdx)
    {
        if(ReadCursor < WriteCursor)
        {
            u32 RingIdx = ReadCursor % Audio->RingCapacity;
            *Dest++ = Audio->SampleRing[RingIdx*2 + 0];
            *Dest++ = Audio->SampleRing[RingIdx*2 + 1];
            ++ReadCursor;
        }
        else
        {
            *Dest++ = 0;
            *Dest++ = 0;
        }
    }

    Audio->RingReadCursor = ReadCursor;

    Buffer->mAudioDataByteSize = SamplesToWrite * sizeof(s16) * 2;
    AudioQueueEnqueueBuffer(Queue, Buffer, 0, 0);
}

static void
OSXAudioStart(osx_audio *Audio, u32 SamplesPerSecond)
{
    Audio->SamplesPerSecond = SamplesPerSecond;

    Audio->Format.mSampleRate = SamplesPerSecond;
    Audio->Format.mFormatID = kAudioFormatLinearPCM;
    Audio->Format.mFormatFlags = (kAudioFormatFlagIsSignedInteger |
                                  kAudioFormatFlagIsPacked);
    Audio->Format.mBitsPerChannel = 16;
    Audio->Format.mChannelsPerFrame = 2;
    Audio->Format.mBytesPerFrame = sizeof(s16) * 2;
    Audio->Format.mFramesPerPacket = 1;
    Audio->Format.mBytesPerPacket = Audio->Format.mBytesPerFrame;

    // (CLAUDE): ~20ms per callback buffer
    Audio->NumBufferSamples = SamplesPerSecond / 50;

    // (CLAUDE): One second of ring
    Audio->RingCapacity = SamplesPerSecond;
    Audio->SampleRing = (s16 *)calloc(Audio->RingCapacity * 2, sizeof(s16));
    Audio->RingWriteCursor = 0;
    Audio->RingReadCursor = 0;

    if(AudioQueueNewOutput(&Audio->Format, OSXAudioCallback, Audio,
                           0, 0, 0, &Audio->Queue) != noErr)
    {
        printf("AudioQueueNewOutput failed\n");
        return;
    }

    u32 BufferBytes = Audio->NumBufferSamples * sizeof(s16) * 2;
    for(int BufferIdx = 0;
        BufferIdx < ArrayCount(Audio->Buffers);
        ++BufferIdx)
    {
        AudioQueueAllocateBuffer(Audio->Queue, BufferBytes, &Audio->Buffers[BufferIdx]);
        Audio->Buffers[BufferIdx]->mAudioDataByteSize = BufferBytes;
        memset(Audio->Buffers[BufferIdx]->mAudioData, 0, BufferBytes);
        AudioQueueEnqueueBuffer(Audio->Queue, Audio->Buffers[BufferIdx], 0, 0);
    }

    AudioQueueStart(Audio->Queue, 0);
    Audio->IsPlaying = true;
}

static void
OSXAudioStop(osx_audio *Audio)
{
    if(Audio->IsPlaying)
    {
        AudioQueueStop(Audio->Queue, true);
        AudioQueueDispose(Audio->Queue, true);
        free(Audio->SampleRing);
        Audio->SampleRing = 0;
        Audio->IsPlaying = false;
    }
}

static u32
OSXGetNumSamplesToWrite(osx_audio *Audio)
{
    u32 Result = 0;

    u32 Pending = Audio->RingWriteCursor - Audio->RingReadCursor;

    u32 TargetPending = Audio->NumBufferSamples * 2;
    if(Pending < TargetPending)
    {
        Result = TargetPending - Pending;
    }

    return(Result);
}

static void
OSXPushSoundSamples(osx_audio *Audio, game_sound_output_buffer *SoundBuffer)
{
    s16 *Src = SoundBuffer->Samples;
    u32 WriteCursor = Audio->RingWriteCursor;

    for(int SampleIdx = 0;
        SampleIdx < SoundBuffer->SampleCount;
        ++SampleIdx)
    {
        u32 RingIdx = WriteCursor % Audio->RingCapacity;
        Audio->SampleRing[RingIdx*2 + 0] = *Src++;
        Audio->SampleRing[RingIdx*2 + 1] = *Src++;
        ++WriteCursor;
    }

    Audio->RingWriteCursor = WriteCursor;
}

@interface OSXWindowDelegate : NSObject<NSWindowDelegate>
@end

@implementation OSXWindowDelegate
- (BOOL)windowShouldClose:(id)Sender
{
    GlobalRunning = false;
    return NO;
}
@end

@interface OSXView : NSView
{
@public
    osx_offscreen_buffer *Backbuffer;
}
@end

@implementation OSXView
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)DirtyRect
{
    if(!Backbuffer || !Backbuffer->Memory)
    {
        return;
    }

    CGContextRef Ctx = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef ColorSpace = CGColorSpaceCreateDeviceRGB();

    CGDataProviderRef Provider = 
        CGDataProviderCreateWithData(0,
                                     Backbuffer->Memory,
                                     Backbuffer->Pitch * Backbuffer->Height,
                                     0);

    CGImageRef Image = CGImageCreate(Backbuffer->Width,
                                     Backbuffer->Height,
                                     8,             // bits per component
                                     32,            // bits per pixel
                                     Backbuffer->Pitch,
                                     ColorSpace,
                                     kCGBitmapByteOrder32Little|kCGImageAlphaNoneSkipFirst,
                                     Provider,
                                     0, false,
                                     kCGRenderingIntentDefault);

    CGContextSetInterpolationQuality(Ctx, kCGInterpolationNone);
    CGContextDrawImage(Ctx, [self bounds], Image);

    CGImageRelease(Image);
    CGDataProviderRelease(Provider);
    CGColorSpaceRelease(ColorSpace);
}
@end
    
int
main(int ArgC, char **ArgVector)
{
    @autoreleasepool
    {
        OSXGetEXEPath();
        mach_timebase_info_data_t TimebaseInfo;
        mach_timebase_info(&TimebaseInfo);
        GlobalNanosPerTick = (f32)TimebaseInfo.numer / (f32)TimebaseInfo.denom;

        NSApplication *App = [NSApplication sharedApplication];
        [App setActivationPolicy:NSApplicationActivationPolicyRegular];
        [App finishLaunching];

        int BackbufferWidth = 1920;
        int BackbufferHeight = 1080;
        OSXResizeBackbuffer(&GlobalBackbuffer, BackbufferWidth, BackbufferHeight);

        NSScreen *MainScreen = [NSScreen mainScreen];
        f32 ScaleFactor = (f32)[MainScreen backingScaleFactor];

        f32 WindowWidth = (f32)BackbufferWidth / ScaleFactor;
        f32 WindowHeight = (f32)BackbufferHeight / ScaleFactor;

        NSRect VisibleFrame = [MainScreen visibleFrame];
        f32 MaxWidth = (f32)VisibleFrame.size.width;
        f32 MaxHeight = (f32)VisibleFrame.size.height - 40.0f; // leave room for title bar
        if((WindowWidth > MaxWidth) || (WindowHeight > MaxHeight))
        {
            f32 Shrink = MaxWidth / WindowWidth;
            f32 ShrinkY = MaxHeight / WindowHeight;
            if(ShrinkY < Shrink)
            {
                Shrink = ShrinkY;
            }
            WindowWidth *= Shrink;
            WindowHeight *= Shrink;
        }

        NSRect ContentRect = NSMakeRect(0, 0, WindowWidth, WindowHeight);
        NSUInteger StyleMask = (NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable);

        NSWindow *Window = [[NSWindow alloc] initWithContentRect:ContentRect
                                                    styleMask:StyleMask
                                                    backing:NSBackingStoreBuffered
                                                    defer:NO];
        [Window setTitle:@"Tufty"];
        [Window setDelegate:[[OSXWindowDelegate alloc] init]];

        OSXView *View = [[OSXView alloc] initWithFrame:ContentRect];
        View->Backbuffer = &GlobalBackbuffer;
        [Window setContentView:View];
        [Window makeFirstResponder:View];

        [Window center];
        [Window makeKeyAndOrderFront:nil];
        [App activateIgnoringOtherApps:YES];

        int MonitorRefreshHz = 60;
        NSScreen *Screen = [Window screen];
        if(Screen && [Screen respondsToSelector:@selector(maximumFramesPerSecond)])
        {
            NSInteger RefreshRate = [Screen maximumFramesPerSecond];
            if(RefreshRate > 1)
            {
                MonitorRefreshHz = (int)RefreshRate;
            }
        }
        f32 GameUpdateHz = MonitorRefreshHz;
        f32 TargetSecondsPerFrame = 1.0f / GameUpdateHz;

        u64 TargetTicksPerFrame = (u64)((TargetSecondsPerFrame * 1.0e9f) / GlobalNanosPerTick);
        u64 SpinMarginTicks = (u64)(2000000.0f / GlobalNanosPerTick); // 2ms

        
        u64 LastCounter = ReadCpuTimer();

        char SourceGameCodeDylibFullPath[PATH_MAX];
        OSXBuildEXEPathFilename("tufty.dylib", sizeof(SourceGameCodeDylibFullPath), SourceGameCodeDylibFullPath);

        game_memory GameMemory = {};
        GameMemory.PermanentStorageSize = Megabytes(64);
        GameMemory.TransientStorageSize = Gigabytes(1);
        GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
        GameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
        GameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
        GameMemory.DEBUGPlatformGetFileSize = DEBUGPlatformGetFileSize;
        GameMemory.DEBUGPlatformReadFileInto = DEBUGPlatformReadFileInto;
        GameMemory.DEBUGPlatformGetFileWriteTime = DEBUGPlatformGetFileWriteTime;
        GameMemory.DEBUGPlatformGetDirWriteTime = DEBUGPlatformGetDirWriteTime;
        GameMemory.DEBUGPlatformGetListOfDirContents = DEBUGPlatformGetListOfDirContents;
        GameMemory.DEBUGPlatformGetFilepathFromDialog = DEBUGPlatformGetFilepathFromDialog;


        u64 TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;

#if TUFTY_INTERNAL
        void *BaseAddress = (void *)Terabytes(2);
#else
        void *BaseAddress = 0;
#endif

        void *GameMemoryBlock = mmap(BaseAddress, TotalSize, 
                                        PROT_READ|PROT_WRITE, 
                                        MAP_PRIVATE|MAP_ANONYMOUS, 
                                        -1, 0);
        Assert(GameMemoryBlock != MAP_FAILED);

        GameMemory.PermanentStorage = GameMemoryBlock;
        GameMemory.TransientStorage = ((u8 *)GameMemory.PermanentStorage + 
                                        GameMemory.PermanentStorageSize);

        game_input Input[2] = {};
        game_input *NewInput = Input;
        game_input *OldInput = Input + 1;

        osx_game_code Game = OSXLoadGameCode(SourceGameCodeDylibFullPath);

        OSXAudioStart(&GlobalAudio, 48000);
        s16 *AudioSamples = (s16 *)calloc(GlobalAudio.RingCapacity * 2, sizeof(s16));

        GlobalRunning = true;

        while(GlobalRunning)
        {
            @autoreleasepool
            {
                game_controller_input *OldKeyboardController = GetController(OldInput, 0);
                game_controller_input *NewKeyboardController = GetController(NewInput, 0);
                *NewKeyboardController = {};
                NewKeyboardController->IsConnected = true;
                for(int ButtonIdx = 0;
                    ButtonIdx < ArrayCount(NewKeyboardController->Buttons);
                    ++ButtonIdx)
                {
                    NewKeyboardController->Buttons[ButtonIdx].EndedDown = 
                        OldKeyboardController->Buttons[ButtonIdx].EndedDown;
                }

                dev_keys *OldDevKeys = &OldInput->DevKeys;
                dev_keys *NewDevKeys = &NewInput->DevKeys;
                *NewDevKeys = {};
                for(int FnKeyIdx = 0;
                    FnKeyIdx < ArrayCount(NewDevKeys->Keys);
                    ++FnKeyIdx)
                {
                    NewDevKeys->Keys[FnKeyIdx].EndedDown = OldDevKeys->Keys[FnKeyIdx].EndedDown;
                }

                game_mouse_input *OldMouse = &OldInput->Mouse;
                game_mouse_input *NewMouse = &NewInput->Mouse;
                *NewMouse = {};
                for(int ButtonIdx = 0;
                    ButtonIdx < ArrayCount(NewMouse->Buttons);
                    ++ButtonIdx)
                {
                    NewMouse->Buttons[ButtonIdx].EndedDown = OldMouse->Buttons[ButtonIdx].EndedDown;
                }

                NSPoint MouseWindowP = [Window mouseLocationOutsideOfEventStream];
                NSPoint MouseViewP = [View convertPoint:MouseWindowP fromView:nil];
                NSRect ViewBounds = [View bounds];

                f32 MouseXScale = (f32)GlobalBackbuffer.Width / (f32)ViewBounds.size.width;
                f32 MouseYScale = (f32)GlobalBackbuffer.Height / (f32)ViewBounds.size.height;

                NewMouse->X = (s32)(MouseViewP.x * MouseXScale);
                NewMouse->Y = (s32)(((f32)ViewBounds.size.height - MouseViewP.y) * MouseYScale);

                
                GlobalKeyboardController = NewKeyboardController;
                GlobalMouse = NewMouse;
                GlobalDevKeys = NewDevKeys;

                OSXProcessPendingEvents(App);

                NewInput->dtForFrame = TargetSecondsPerFrame;

                u64 NewDylibWriteTime = DEBUGPlatformGetFileWriteTime(SourceGameCodeDylibFullPath);
                if(NewDylibWriteTime != Game.DylibLastWriteTime)
                {
                    OSXUnloadGameCode(&Game);
                    Game = OSXLoadGameCode(SourceGameCodeDylibFullPath);
                }

                if(!GlobalPause)
                {
                    game_offscreen_buffer Buffer = {};
                    Buffer.Memory = GlobalBackbuffer.Memory;
                    Buffer.Width = GlobalBackbuffer.Width;
                    Buffer.Height = GlobalBackbuffer.Height;
                    Buffer.Pitch = GlobalBackbuffer.Pitch;
                    Buffer.BytesPerPixel = GlobalBackbuffer.BytesPerPixel;

                    if(Game.UpdateAndRender)
                    {
                        NewInput->CpuTimerReading = ReadCpuTimer();
                        Game.UpdateAndRender(&GameMemory, NewInput, &Buffer);
                        if(GlobalDialogWasOpen)
                        {
                            GlobalDialogWasOpen = false;
                            LastCounter = ReadCpuTimer();
                        }
                    }

                    u32 NumSamplesToWrite = OSXGetNumSamplesToWrite(&GlobalAudio);
                    if(NumSamplesToWrite)
                    {
                        game_sound_output_buffer SoundBuffer = {};
                        SoundBuffer.SamplesPerSecond = GlobalAudio.SamplesPerSecond;
                        SoundBuffer.SampleCount = NumSamplesToWrite;
                        SoundBuffer.Samples = AudioSamples;
                    
                        if(Game.GetSoundSamples)
                        {
                            Game.GetSoundSamples(&GameMemory, &SoundBuffer);
                        }

                        OSXPushSoundSamples(&GlobalAudio, &SoundBuffer);
                    }
                }
                
                [View display];

                u64 FrameDeadline = LastCounter + TargetTicksPerFrame;
                if(ReadCpuTimer() < FrameDeadline)
                {
                    if((FrameDeadline - ReadCpuTimer()) > SpinMarginTicks)
                    {
                        mach_wait_until(FrameDeadline - SpinMarginTicks);
                    }

                    while(ReadCpuTimer() < FrameDeadline)
                    {
                    }
                }
                else
                {
                    //TODO(Aaron): missed frame rate
                }
                
                u64 EndCounter = ReadCpuTimer();
                f32 SecondsPerFrame = OSXGetSecondsElapsed(LastCounter, EndCounter);
                LastCounter = EndCounter;

                OldInput->Fps = 1.0 / SecondsPerFrame;

                game_input *Temp = NewInput;
                NewInput = OldInput;
                OldInput = Temp;
            }
        }
        OSXAudioStop(&GlobalAudio);
        free(AudioSamples);
    }

    return(0);
}

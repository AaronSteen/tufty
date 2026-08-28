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

static b32 GlobalRunning;
static b32 GlobalPause;
static f32 GlobalNanosPerTick;
static osx_offscreen_buffer GlobalBackbuffer;
static char GlobalEXEPath[PATH_MAX];
static u32 GlobalTempDylibCounter;

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

DEBUG_PLATFORM_GET_FILE_SIZE(DEBUGPlatformGetFileSize)
{
    u32 Result = 0;

    struct stat FileStat;
    if(stat(Filename, &FileStat) == 0)
    {
        Result = SafeTruncateU64ToU32(FileStat.st_size);
    }

    return(Result);
}

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

DEBUG_PLATFORM_READ_FILE_INTO(DEBUGPlatformReadFileInto)
{
    u32 Result = 0;

    int FileHandle = open(Filename, O_RDONLY);
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

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
    if(Memory)
    {
        free(Memory);
    }
}

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

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
    b32 Result = false;

    int FileHandle = open(Filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(FileHandle != -1)
    {
        ssize_t BytesWritten = write(FileHandle, Memory, MemorySize);
        Result = (BytesWritten == (ssize_t)MemorySize);
        close(FileHandle);
    }

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

        memory_idx DirLength = OnePastLastSlash - PathBuffer;
        for(memory_idx Index = 0;
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

        OSXResizeBackbuffer(&GlobalBackbuffer, 960, 540);

        NSRect ContentRect = NSMakeRect(0, 0, GlobalBackbuffer.Width, GlobalBackbuffer.Height);
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

        GlobalRunning = true;

        while(GlobalRunning)
        {
            @autoreleasepool
            {
                for(;;)
                {
                    NSEvent *Event = [App nextEventMatchingMask:NSEventMaskAny
                                                        untilDate:nil
                                                        inMode:NSDefaultRunLoopMode
                                                        dequeue:YES];
                    if(!Event)
                    {
                        break;
                    }

                    [App sendEvent:Event];
                }

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
                        Game.UpdateAndRender(&GameMemory, NewInput, &Buffer);
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
    }

    return(0);
}

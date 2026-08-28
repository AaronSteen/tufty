#import <Cocoa/Cocoa.h>
#include <sys/mman.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

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

static b32 GlobalRunning;
static b32 GlobalPause;
static f32 GlobalNanosPerTick;
static osx_offscreen_buffer GlobalBackbuffer;

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
    
static void
OSXDrawTestRectangle(osx_offscreen_buffer *Buffer,
                     int MinX, int MinY, int MaxX, int MaxY,
                     u32 Color)
{
    u8 *Row = (u8 *)Buffer->Memory + MinY*Buffer->Pitch + MinX*Buffer->BytesPerPixel;
    for(int Y = MinY;
        Y < MaxY;
        ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for(int X = MinX; 
            X < MaxX;
            ++X)
        {
            *Pixel++ = Color;
        }
        Row += Buffer->Pitch;
    }
}

int
main(int ArgC, char **ArgVector)
{
    @autoreleasepool
    {
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
        u32 FrameCount = 0;

        {
            char *TestFile = "16x16faceright.bmp";
            u32 TestSize = DEBUGPlatformGetFileSize(TestFile);
            u64 TestTime = DEBUGPlatformGetFileWriteTime(TestFile);
            printf("%s: %u bytes, write time %llu\n", TestFile, TestSize, TestTime);
        }

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
                OSXDrawTestRectangle(&GlobalBackbuffer, 0, 0, 960, 540, 0x00204060);
                OSXDrawTestRectangle(&GlobalBackbuffer, 100, 50, 400, 200, 0x00FF8020);

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

                if((++FrameCount % 30) == 0)
                {
                    printf("%.02fms/f, %.02f FPS\n", 1000.0f*SecondsPerFrame, 1.0f/SecondsPerFrame);
                }
            }
        }
    }

    return(0);
}

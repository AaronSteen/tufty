/* ========================================================================
   $File: $
   $Date: $
   $Revision: $
   $Creator: Casey Muratori $
   $Notice: (C) Copyright 2014 by Molly Rocket, Inc. All Rights Reserved. $
   ======================================================================== */

/*
  TODO(casey):  THIS IS NOT A FINAL PLATFORM LAYER!!!

  - Saved game locations
  - Getting a handle to our own executable file
  - Asset loading path
  - Threading (launch a thread)
  - Raw Input (support for multiple keyboards)
  - Sleep/timeBeginPeriod
  - ClipCursor() (for multimonitor support)
  - Fullscreen support
  - WM_SETCURSOR (control cursor visibility)
  - QueryCancelAutoplay
  - WM_ACTIVATEAPP (for when we are not the active application)
  - Blit speed improvements (BitBlt)
  - Hardware acceleration (OpenGL or Direct3D or BOTH??)
  - GetKeyboardLayout (for French keyboards, international WASD support)

  Just a partial list of stuff!!
*/

#include "tufty_platform.h"

#include <windows.h>
#include <sys/stat.h>
#include <stdio.h>
#include <malloc.h>
#include <xinput.h>
#include <intrin.h>

// WASAPI
#include <initguid.h>
#include <objbase.h>
#include <uuids.h>       // for MEDIASUBTYPE_IEEE_FLOAT
#include <audioclient.h>
#include <mmdeviceapi.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient,         0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioClient3,        0x7ed4ee07, 0x8e67, 0x4cd4, 0x8c, 0x1a, 0x2b, 0x7a, 0x59, 0x87, 0xad, 0x42);
DEFINE_GUID(IID_IAudioRenderClient,   0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);

#define HR(stmt) do { HRESULT _hr = stmt; Assert(SUCCEEDED(_hr)); } while (0)

#pragma comment(lib, "ole32")

#include "win32_tufty_handmade.h"

// TODO(casey): This is a global for now.
static b32 GlobalRunning;
static b32 GlobalPause;
static win32_offscreen_buffer GlobalBackbuffer;
static win32_wasapi_audio WasapiAudio;
static u64 GlobalCpuFreq;
static HWND Window;
static b32 GlobalDialogWasOpen;
static win32_state *GlobalWin32State;
static game_controller_input *GlobalKeyboardController;
static dev_keys *GlobalDevKeys;
static game_mouse_input *GlobalMouse;

// NOTE(casey): XInputGetState
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
static x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

// NOTE(casey): XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
static x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

static void
Win32GetEXEFileName(win32_state *State)
{
    // NOTE(casey): Never use MAX_PATH in code that is user-facing, because it
    // can be dangerous and lead to bad results.
    DWORD SizeOfFilename = GetModuleFileNameA(0, State->EXEFileName, sizeof(State->EXEFileName));
    State->OnePastLastEXEFileNameSlash = State->EXEFileName;
    for(char *Scan = State->EXEFileName;
        *Scan;
        ++Scan)
    {
        if(*Scan == '\\')
        {
            State->OnePastLastEXEFileNameSlash = Scan + 1;
        }
    }
}

static int
StringLength(char *String)
{
    int Count = 0;
    while(*String++)
    {
        ++Count;
    }
    return(Count);
}

static void
Win32BuildEXEPathFileName(win32_state *State, char *FileName,
                          int DestCount, char *Dest)
{
    CatStrings(State->OnePastLastEXEFileNameSlash - State->EXEFileName, State->EXEFileName,
               StringLength(FileName), FileName,
               DestCount, Dest);
}

static void
DebugOutput(const char *Format, ...)
{
    char PrintBuffer[512];
    int PrintBufferSize = sizeof(PrintBuffer);

    va_list Args;
    va_start(Args, Format);
    int BytesWritten = vsnprintf(PrintBuffer, PrintBufferSize, Format, Args);
    va_end(Args);

    if(BytesWritten > PrintBufferSize)
    {
        OutputDebugStringA("\n\nERROR: DebugOutput function format string did not fit in print buffer\n\n");
    }
    OutputDebugStringA(PrintBuffer);
    OutputDebugStringA("\n");

    return;
}

DEBUG_PLATFORM_GET_FILE_SIZE(DEBUGPlatformGetFileSize)
{
    mem_idx Result = 0;
    struct __stat64 Stat;
    if(_stat64(Filepath, &Stat) == -1)
    {
        return(Result);
    }
    
    Result = Stat.st_size;
    return(Result);
}

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
    if(Memory)
    {
        VirtualFree(Memory, 0, MEM_RELEASE);
    }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
    debug_read_file_result Result = {};
    
    HANDLE FileHandle = CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if(GetFileSizeEx(FileHandle, &FileSize))
        {
            // NOTE(AARON): I don't know if it's actually necessary to worry about 32-bit file sizes; 
            //      think this may be artifact of early days of HMH when Casey wanted to ship on
            //      32-bit machines
            u32 FileSize32 = SafeTruncateU64ToU32(FileSize.QuadPart);
            Result.Contents = VirtualAlloc(0, FileSize32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            if(Result.Contents)
            {
                DWORD BytesRead;
                if(ReadFile(FileHandle, Result.Contents, FileSize32, &BytesRead, 0) &&
                   (FileSize32 == BytesRead))
                {
                    // NOTE(casey): File read successfully
                    Result.ContentsSize = FileSize32;
                }
                else
                {                    
                    // TODO(casey): Logging
                    DEBUGPlatformFreeFileMemory(Result.Contents);
                    Result.Contents = 0;
                }
            }
            else
            {
                // TODO(casey): Logging
            }
        }
        else
        {
            // TODO(casey): Logging
        }

        CloseHandle(FileHandle);
    }
    else
    {
        // TODO(casey): Logging
    }

    return(Result);
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
    b32 Result = false;
    
    HANDLE FileHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD BytesWritten;
        if(WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0))
        {
            // NOTE(casey): File read successfully
            Result = (BytesWritten == MemorySize);
        }
        else
        {
            // TODO(casey): Logging
        }

        CloseHandle(FileHandle);
    }
    else
    {
        // TODO(casey): Logging
    }

    return(Result);
}

// #define DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename)
DEBUG_PLATFORM_GET_FILE_WRITE_TIME(DEBUGPlatformGetFileWriteTime)
{
    u64 Result = 0;
    HANDLE Handle = CreateFileA(Filename,
                                GENERIC_READ,
                                FILE_SHARE_READ|FILE_SHARE_WRITE,
                                0,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                0);
    if(Handle != INVALID_HANDLE_VALUE)
    {
        FILETIME FileTime;
        if(GetFileTime(Handle, 0, 0, &FileTime))
        {
            ULARGE_INTEGER Big;
            Big.LowPart = FileTime.dwLowDateTime;
            Big.HighPart = FileTime.dwHighDateTime;
            if(Big.QuadPart)
            {
                Result = Big.QuadPart;
            }
        }
        CloseHandle(Handle);
    }
    return(Result);
}

// #define DEBUG_PLATFORM_READ_FILE_INTO(name) mem_idx name(char *Filepath, u32 DestSize, void *Dest)
DEBUG_PLATFORM_READ_FILE_INTO(DEBUGPlatformReadFileInto)
{
    u32 Result = 0;
    HANDLE FileHandle = CreateFileA(Filepath, // lpFileName
                                    GENERIC_READ, // dwDesiredAccess
                                    FILE_SHARE_READ, // dwShareMode
                                    0, // lpSecurityAttributes, optional
                                    OPEN_EXISTING, // dwCreationDisposition
                                    0, // dwFlagsAndAttributes, MSDN says this is not optional, but StackOverflow says pass 0 if you don't care.
                                    0);  // hTemplateFile, optional

    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if(GetFileSizeEx(FileHandle, &FileSize))
        {
            // NOTE(Aaron): Becuase we're using the basic ReadFile function, which takes a 32-bit DWORD for the third argument
            //      indicating the number of bytes to read, the file we're reading can't be larger than (2^32) - 1 = 4,294,967,295 bytes,
            //      so we use a u32 for the DestSize argument to DEBUGPlatformReadFileInto, and also a u32 for the return argument
            //      that tells the caller how many bytes were actually read.
            u32 FileSize32 = SafeTruncateU64ToU32(FileSize.QuadPart);
            if(FileSize32 <= DestSize)
            {
                DWORD BytesRead;
                if(ReadFile(FileHandle, Dest, FileSize32, &BytesRead, 0) &&
                   (FileSize32 == BytesRead))
                {
                    Result = FileSize32;
                }
            }
        }
        CloseHandle(FileHandle);
    }
    return(Result);
}


// DEBUG_PLATFORM_GET_DIR_WRITE_TIME(name) u64 name(char *Dirname)
DEBUG_PLATFORM_GET_DIR_WRITE_TIME(DEBUGPlatformGetDirWriteTime)
{
    u64 Result = 0;
    HANDLE DirHandle = CreateFileA(Dirname,
                                   FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                   0,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS,
                                   0);
    if(DirHandle != INVALID_HANDLE_VALUE)
    {
        BY_HANDLE_FILE_INFORMATION Info;
        if(GetFileInformationByHandle(DirHandle, &Info))
        {
            ULARGE_INTEGER Time;
            Time.LowPart = Info.ftLastWriteTime.dwLowDateTime;
            Time.HighPart = Info.ftLastWriteTime.dwHighDateTime;
            Result = Time.QuadPart;
        }
        CloseHandle(DirHandle);
    }

    return(Result);
}

// #define DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(buffer *GamePackedFilenames, char *DirName, u32 *NumFilesFound)
DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(DEBUGPlatformGetListOfDirContents)
{
    WIN32_FIND_DATAA FindData = {};
    char SearchTerm[MAX_PATH];
    char SearchTermSuffix[MAX_PATH];
    char *Extension = ".bmp";
    char *WildcardInSearchTerm = "/*";

    int ExtensionLength = StringLength(Extension);
    int WildcardLength = StringLength(WildcardInSearchTerm);
    CatStrings(WildcardLength, WildcardInSearchTerm, 
               ExtensionLength, Extension,
               MAX_PATH, SearchTermSuffix);

    if(StringLength(DirName) + StringLength(SearchTermSuffix) > MAX_PATH)
    {
        *NumFilesFound = 0;
        return;
    }

    CatStrings(StringLength(DirName), DirName, 
               StringLength(SearchTermSuffix), SearchTermSuffix, 
               MAX_PATH, SearchTerm);

    HANDLE SearchHandle = FindFirstFileA((LPCSTR)SearchTerm, &FindData);
    if(SearchHandle == INVALID_HANDLE_VALUE)
    {
        *NumFilesFound = 0;
        return;
    }

    u8 *GameCursor = GamePackedFilenames->Data;
    b32 KeepSearching = true;
    while(KeepSearching)
    {
        char *Win32Cursor = (char *)FindData.cFileName;

        int NameLength = StringLength(Win32Cursor);

        b32 IsBmp = ( (NameLength > ExtensionLength) &&
                      (_stricmp(Win32Cursor + NameLength - ExtensionLength, Extension) == 0) );
        if(IsBmp)
        {
            while(*Win32Cursor)
            {
                if(GameCursor+1 < GamePackedFilenames->Data + GamePackedFilenames->Size)
                {
                    *GameCursor++ = *Win32Cursor++;
                }
                else
                {
                    // This means we were about to overrun GamePackedFilenamesArray
                    *NumFilesFound = 0;
                    FindClose(SearchHandle);
                    return;
                }
            }
            *GameCursor++ = '\0';
            *NumFilesFound += 1;
            KeepSearching = FindNextFileA(SearchHandle, &FindData);
        }
    }
    FindClose(SearchHandle);
}

// Need forward declaration to call this here
static void
Win32ProcessPendingMessages(win32_state *State, game_controller_input *KeyboardController, dev_keys *DevKeys, game_mouse_input *Mouse);

// #define DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(name) int name(char *Dest, int DestSize, b32 IsSave)
DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(DEBUGPlatformGetFilepathFromDialog)
{
    // The purpose of this global is to ignore any keyboard messages that appear in our MainWindowCallback function,
    //      which will happen as we press keys while interacting with the file dialog, since the
    //      file dialog we open below will automatically dispatch them to MainWindowCallback. There
    //      is an assert in there that fires if a keyboard message is dispatched to MainWindowCallback.
    //      Normally we want this assert to fire if a keyboard message appears there, because it means
    //      we somehow failed to read keyboard input in our function for processing it. But
    //      while the file dialog is open, it will send them there without us being able to intervene,
    //      so we have this global variable that we can use in an if statement to guard against that.
    //
    //      AS, 9.17.26
    GlobalDialogWasOpen = true;

    OPENFILENAMEA Filename = {};
    Filename.lStructSize = sizeof(OPENFILENAMEA);
    Filename.hwndOwner = Window;
    Filename.lpstrFile = Dest;
    Filename.nMaxFile = DestSize;
    Filename.lpstrFilter = "Tufty project (.tufty)\0*.tufty\0All files\0*.*\0\0";
    Filename.lpstrDefExt = ".tufty";
    Filename.Flags = OFN_OVERWRITEPROMPT|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;

    int Result = 0;
    Win32ProcessPendingMessages(GlobalWin32State, GlobalKeyboardController, GlobalDevKeys, GlobalMouse);
    if(IsSave)
    {
        Result = GetSaveFileNameA(&Filename);
    }
    else
    {
        Result = GetOpenFileNameA(&Filename);
    }

    // It might be the case that a key got stuck in an EndedDown state in the process of
    //      pushing e.g. Ctrl+S to open the save dialog, so we zero all input here
    *GlobalKeyboardController = {};
    *GlobalDevKeys = {};
    *GlobalMouse = {};
    
    return(Result);
}

static u64
GetOsTimerFreq(void)
{
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    return(Freq.QuadPart);
}

static u64
ReadOsTimer(void)
{
    LARGE_INTEGER Counter;
    QueryPerformanceCounter(&Counter);
    return(Counter.QuadPart);
}

static u64
ReadCpuTimer(void)
{
    u64 Result = __rdtsc();
    return(Result);
}

static u64
GuessCpuTimerFreq(void)
{
    u64 Result = 0;
    u64 OsTimerFreq = GetOsTimerFreq();
    u64 OsWaitTime = 0.1f * OsTimerFreq;

    u64 CpuStart = ReadCpuTimer();
    u64 OsStart = ReadOsTimer();
    u64 OsEnd = 0;
    u64 OsElapsed = 0;

    while(OsElapsed < OsWaitTime)
    {
        OsEnd = ReadOsTimer();
        OsElapsed = OsEnd - OsStart;
    }
    u64 CpuEnd = ReadCpuTimer();
    u64 CpuElapsed = CpuEnd - CpuStart;

    if(OsElapsed)
    {
        Result = CpuElapsed * OsTimerFreq / OsElapsed;
    }

    return(Result);
}

inline FILETIME
Win32GetLastWriteTime(char *Filename)
{
    FILETIME LastWriteTime = {};

    WIN32_FILE_ATTRIBUTE_DATA Data;
    if(GetFileAttributesEx(Filename, GetFileExInfoStandard, &Data))
    {
        LastWriteTime = Data.ftLastWriteTime;
    }

    return(LastWriteTime);
}

static win32_game_code
Win32LoadGameCode(char *SourceDLLName, char *TempDLLName)
{
    win32_game_code Result = {};

    // TODO(casey): Need to get the proper path here!
    // TODO(casey): Automatic determination of when updates are necessary.

    Result.DLLLastWriteTime = Win32GetLastWriteTime(SourceDLLName);

    CopyFile(SourceDLLName, TempDLLName, FALSE);
    
    Result.GameCodeDLL = LoadLibraryA(TempDLLName);
    if(Result.GameCodeDLL)
    {
        Result.UpdateAndRender = (game_update_and_render *)
            GetProcAddress(Result.GameCodeDLL, "GameUpdateAndRender");
        
        Result.GetSoundSamples = (game_get_sound_samples *)
            GetProcAddress(Result.GameCodeDLL, "GameGetSoundSamples");

        Result.IsValid = (Result.UpdateAndRender &&
                          Result.GetSoundSamples);
    }

    if(!Result.IsValid)
    {
        Result.UpdateAndRender = 0;
        Result.GetSoundSamples = 0;
    }

    return(Result);
}

static void
Win32UnloadGameCode(win32_game_code *GameCode)
{
    if(GameCode->GameCodeDLL)
    {
        FreeLibrary(GameCode->GameCodeDLL);
        GameCode->GameCodeDLL = 0;
    }

    GameCode->IsValid = false;
    GameCode->UpdateAndRender = 0;
    GameCode->GetSoundSamples = 0;
}

static void
Win32LoadXInput(void)    
{
    // TODO(casey): Test this on Windows 8
    HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
    if(!XInputLibrary)
    {
        // TODO(casey): Diagnostic
        XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
    }
    
    if(!XInputLibrary)
    {
        // TODO(casey): Diagnostic
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }
    
    if(XInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        if(!XInputGetState) {XInputGetState = XInputGetStateStub;}

        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
        if(!XInputSetState) {XInputSetState = XInputSetStateStub;}

        // TODO(casey): Diagnostic

    }
    else
    {
        // TODO(casey): Diagnostic
    }
}

static void
WA_Start(win32_wasapi_audio *Audio)
{
    HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

    IMMDeviceEnumerator *Enumerator = nullptr;
    HR(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                        IID_IMMDeviceEnumerator, (LPVOID*)&Enumerator));

    IMMDevice *PlaybackDevice = nullptr;
    HR(Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &PlaybackDevice));
    Enumerator->Release();

    HR(PlaybackDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (LPVOID*)&Audio->Client));
    PlaybackDevice->Release();

    WAVEFORMATEXTENSIBLE FormatEx = {};
    FormatEx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    FormatEx.Format.nChannels       = 2;
    FormatEx.Format.nSamplesPerSec  = 48000;
    FormatEx.Format.wBitsPerSample  = (WORD)(8 * sizeof(float));
    FormatEx.Format.nBlockAlign     = (WORD)(FormatEx.Format.nChannels * sizeof(float));
    FormatEx.Format.nAvgBytesPerSec = FormatEx.Format.nSamplesPerSec * FormatEx.Format.nBlockAlign;
    FormatEx.Format.cbSize          = sizeof(FormatEx) - sizeof(FormatEx.Format);
    FormatEx.Samples.wValidBitsPerSample = (WORD)(8 * sizeof(float));
    FormatEx.dwChannelMask          = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    FormatEx.SubFormat              = MEDIASUBTYPE_IEEE_FLOAT;

    Audio->BufferFormat = (WAVEFORMATEX *)CoTaskMemAlloc(sizeof(FormatEx));
    CopyMemory(Audio->BufferFormat, &FormatEx, sizeof(FormatEx));

    REFERENCE_TIME Duration;
    HR(Audio->Client->GetDevicePeriod(&Duration, NULL));

    DWORD Flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HR(Audio->Client->Initialize(AUDCLNT_SHAREMODE_SHARED, Flags,
                                 Duration, 0, Audio->BufferFormat, NULL));

    HR(Audio->Client->GetBufferSize(&Audio->BufferSamples));

    HR(Audio->Client->GetService(IID_IAudioRenderClient, (LPVOID*)&Audio->RenderClient));
    HR(Audio->Client->Start());
}

static void
WA_Stop(win32_wasapi_audio *Audio)
{
    Audio->Client->Stop();
    Audio->RenderClient->Release();
    CoTaskMemFree(Audio->BufferFormat);
    Audio->Client->Release();
    CoUninitialize();
}

static win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;
    
    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return(Result);
}

static void
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    // TODO(casey): Bulletproof this.
    // Maybe don't free first, free after, then free first if that fails.

    if(Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;
    Buffer->BytesPerPixel = BytesPerPixel;

    // NOTE(casey): When the biHeight field is negative, this is the clue to
    // Windows to treat this bitmap as top-down, not bottom-up, meaning that
    // the first three bytes of the image are the color for the top left pixel
    // in the bitmap, not the bottom left!
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    // NOTE(casey): Thank you to Chris Hecker of Spy Party fame
    // for clarifying the deal with StretchDIBits and BitBlt!
    // No more DC for us.
    int BitmapMemorySize = (Buffer->Width*Buffer->Height)*BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width*BytesPerPixel;

    // TODO(casey): Probably clear this to black
}

static void
Win32DisplayBufferInWindow(win32_offscreen_buffer *Buffer,
                           HDC DeviceContext, int WindowWidth, int WindowHeight)
{
    PatBlt(DeviceContext, 0, 0, WindowWidth, WIN32_BACKBUFFER_OFFSET_Y, BLACKNESS);
    PatBlt(DeviceContext, 0, WIN32_BACKBUFFER_OFFSET_Y + Buffer->Height, WindowWidth, WindowHeight, BLACKNESS);
    PatBlt(DeviceContext, 0, 0, WIN32_BACKBUFFER_OFFSET_X, WindowHeight, BLACKNESS);
    PatBlt(DeviceContext, WIN32_BACKBUFFER_OFFSET_X + Buffer->Width, 0, WindowWidth, WindowHeight, BLACKNESS);
    
    // NOTE(casey): For prototyping purposes, we're going to always blit
    // 1-to-1 pixels to make sure we don't introduce artifacts with
    // stretching while we are learning to code the renderer!
    StretchDIBits(DeviceContext,
                  WIN32_BACKBUFFER_OFFSET_X, WIN32_BACKBUFFER_OFFSET_Y, Buffer->Width, Buffer->Height,
                  0, 0, Buffer->Width, Buffer->Height,
                  Buffer->Memory,
                  &Buffer->Info,
                  DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK
Win32MainWindowCallback(HWND Window,
                        UINT Message,
                        WPARAM WParam,
                        LPARAM LParam)
{       
    LRESULT Result = 0;

    switch(Message)
    {
        case WM_CLOSE:
        {
            // TODO(casey): Handle this with a message to the user?
            GlobalRunning = false;
        } break;

        case WM_ACTIVATEAPP:
        {
#if 0
            if(WParam == TRUE)
            {
                SetLayeredWindowAttributes(Window, RGB(0, 0, 0), 255, LWA_ALPHA);
            }
            else
            {
                SetLayeredWindowAttributes(Window, RGB(0, 0, 0), 64, LWA_ALPHA);
            }
#endif
        } break;

        case WM_DESTROY:
        {
            // TODO(casey): Handle this as an error - recreate window?
            GlobalRunning = false;
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            if(!GlobalDialogWasOpen)
            {
                Assert(!"Keyboard input came in through a non-dispatch message!");
            }
        } break;
        
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
            win32_window_dimension Dimension = Win32GetWindowDimension(Window);
            Win32DisplayBufferInWindow(&GlobalBackbuffer, DeviceContext,
                                       Dimension.Width, Dimension.Height);
            EndPaint(Window, &Paint);
        } break;

        default:
        {
//            OutputDebugStringA("default\n");
            Result = DefWindowProcA(Window, Message, WParam, LParam);
        } break;
    }
    
    return(Result);
}

static void
Win32ProcessKeyboardAndMouseMessage(game_button_state *NewState, b32 IsDown)
{
    if(NewState->EndedDown != IsDown)
    {
        NewState->EndedDown = IsDown;
        ++NewState->HalfTransitionCount;
    }
}

static void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
                                game_button_state *OldState, DWORD ButtonBit,
                                game_button_state *NewState)
{
    NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

static f32
Win32ProcessXInputStickValue(SHORT Value, SHORT DeadZoneThreshold)
{
    f32 Result = 0;

    if(Value < -DeadZoneThreshold)
    {
        Result = (f32)((Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold));
    }
    else if(Value > DeadZoneThreshold)
    {
        Result = (f32)((Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold));
    }

    return(Result);
}

static void
Win32GetInputFileLocation(win32_state *State, b32 InputStream,
                          int SlotIndex, int DestCount, char *Dest)
{
    char Temp[64];
    wsprintf(Temp, "loop_edit_%d_%s.hmi", SlotIndex, InputStream ? "input" : "state");
    Win32BuildEXEPathFileName(State, Temp, DestCount, Dest);
}

static win32_replay_buffer *
Win32GetReplayBuffer(win32_state *State, int unsigned Index)
{
    Assert(Index < ArrayCount(State->ReplayBuffers));
    win32_replay_buffer *Result = &State->ReplayBuffers[Index];
    return(Result);
}

static void
Win32BeginRecordingInput(win32_state *State, int InputRecordingIndex)
{
    win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(State, InputRecordingIndex);
    if(ReplayBuffer->MemoryBlock)
    {
        State->InputRecordingIndex = InputRecordingIndex;

        char FileName[WIN32_STATE_FILE_NAME_COUNT];
        Win32GetInputFileLocation(State, true, InputRecordingIndex, sizeof(FileName), FileName);
        State->RecordingHandle = CreateFileA(FileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

#if 0
        LARGE_INTEGER FilePosition;
        FilePosition.QuadPart = State->TotalSize;
        SetFilePointerEx(State->RecordingHandle, FilePosition, 0, FILE_BEGIN);
#endif
        
        CopyMemory(ReplayBuffer->MemoryBlock, State->GameMemoryBlock, State->TotalSize);
    }
}

static void
Win32EndRecordingInput(win32_state *State)
{
    CloseHandle(State->RecordingHandle);
    State->InputRecordingIndex = 0;
}

static void
Win32BeginInputPlayBack(win32_state *State, int InputPlayingIndex)
{
    win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(State, InputPlayingIndex);
    if(ReplayBuffer->MemoryBlock)
    {
        State->InputPlayingIndex = InputPlayingIndex;

        char FileName[WIN32_STATE_FILE_NAME_COUNT];
        Win32GetInputFileLocation(State, true, InputPlayingIndex, sizeof(FileName), FileName);
        State->PlaybackHandle = CreateFileA(FileName, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);

#if 0
        LARGE_INTEGER FilePosition;
        FilePosition.QuadPart = State->TotalSize;
        SetFilePointerEx(State->PlaybackHandle, FilePosition, 0, FILE_BEGIN);
#endif
        
        CopyMemory(State->GameMemoryBlock, ReplayBuffer->MemoryBlock, State->TotalSize);
    }
}

static void
Win32EndInputPlayBack(win32_state *State)
{
    CloseHandle(State->PlaybackHandle);
    State->InputPlayingIndex = 0;
}

static void
Win32RecordInput(win32_state *State, game_input *NewInput)    
{
    DWORD BytesWritten;
    WriteFile(State->RecordingHandle, NewInput, sizeof(*NewInput), &BytesWritten, 0);
}

static void
Win32PlayBackInput(win32_state *State, game_input *NewInput)
{
    DWORD BytesRead = 0;
    if(ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, 0))
    {
        if(BytesRead == 0)
        {
            // NOTE(casey): We've hit the end of the stream, go back to the beginning
            int PlayingIndex = State->InputPlayingIndex;
            Win32EndInputPlayBack(State);
            Win32BeginInputPlayBack(State, PlayingIndex);
            ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, 0);
        }
    }
}

static void
Win32ProcessPendingMessages(win32_state *State, game_controller_input *KeyboardController, dev_keys *DevKeys, game_mouse_input *Mouse)
{
    MSG Message;
    while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
    {
        switch(Message.message)
        {
            case WM_QUIT:
            {
                GlobalRunning = false;
            } break;
            
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                u32 VKCode = (u32)Message.wParam;

                // NOTE(casey): Since we are comparing WasDown to IsDown,
                // we MUST use == and != to convert these bit tests to actual
                // 0 or 1 values.
                b32 WasDown = ((Message.lParam & (1 << 30)) != 0);
                b32 IsDown = ((Message.lParam & (1 << 31)) == 0);
                if(WasDown != IsDown)
                {
                    if(VKCode == 'W')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->MoveUp, IsDown);
                    }
                    else if(VKCode == 'A')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->MoveLeft, IsDown);
                    }
                    else if(VKCode == 'S')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->MoveDown, IsDown);
                    }
                    else if(VKCode == 'D')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->MoveRight, IsDown);
                    }
                    else if(VKCode == 'Q')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->LeftShoulder, IsDown);
                    }
                    else if(VKCode == 'E')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->RightShoulder, IsDown);
                    }
                    else if(VKCode == 'I')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->ActionUp, IsDown);
                    }
                    else if(VKCode == 'J')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->ActionLeft, IsDown);
                    }
                    else if(VKCode == 'K')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->ActionDown, IsDown);
                    }
                    else if(VKCode == 'L')
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->ActionRight, IsDown);
                    }
                    else if(VKCode == VK_ESCAPE)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->Start, IsDown);
                    }
                    else if(VKCode == VK_SPACE)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&KeyboardController->Back, IsDown);
                    }
#if TUFTY_INTERNAL
                    else if(VKCode == 'P')
                    {
                        if(IsDown)
                        {
                            GlobalPause = !GlobalPause;
                        }
                    }
                    // else if(VKCode == 'L')
                    // {
                    //     if(IsDown)
                    //     {
                    //         if(State->InputPlayingIndex == 0)
                    //         {
                    //             if(State->InputRecordingIndex == 0)
                    //             {
                    //                 Win32BeginRecordingInput(State, 1);
                    //             }
                    //             else
                    //             {
                    //                 Win32EndRecordingInput(State);
                    //                 Win32BeginInputPlayBack(State, 1);
                    //             }
                    //         }
                    //         else
                    //         {
                    //             Win32EndInputPlayBack(State);
                    //         }
                    //     }
                    // }
                    //     VK_F2 	0x71 	F2 key
                    //     VK_F3 	0x72 	F3 key
                    //     VK_F4 	0x73 	F4 key
                    //     VK_F5 	0x74 	F5 key
                    //     VK_F6 	0x75 	F6 key
                    //     VK_F7 	0x76 	F7 key
                    //     VK_F8 	0x77 	F8 key
                    //     VK_F9 	0x78 	F9 key
                    //     VK_F10 	0x79 	F10 key
                // Dev keys
                    else if(VKCode == VK_F1)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F1, IsDown);
                    }
                    else if(VKCode == VK_F2)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F2, IsDown);
                    }
                    else if(VKCode == VK_F3)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F3, IsDown);
                    }
                    else if(VKCode == VK_F4)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F4, IsDown);
                    }
                    else if(VKCode == VK_F5)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F5, IsDown);
                    }
                    else if(VKCode == VK_F6)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F6, IsDown);
                    }
                    else if(VKCode == VK_F7)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F7, IsDown);
                    }
                    else if(VKCode == VK_F8)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F8, IsDown);
                    }
                    else if(VKCode == VK_F9)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F9, IsDown);
                    }
                    else if(VKCode == VK_F10)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->F10, IsDown);
                    }
                    else if(VKCode == VK_CONTROL)
                    {
                        Win32ProcessKeyboardAndMouseMessage(&DevKeys->Ctrl, IsDown);
                    }
#endif
                }

                b32 AltKeyWasDown = (Message.lParam & (1 << 29));
                if((VKCode == VK_F4) && AltKeyWasDown)
                {
                    GlobalRunning = false;
                }
            } break;

            // TODO(Aaron): This is rudimentary; for example, we do not handle clicking and dragging, double clicks,
            //      the button sticking if we click down inside our window but release outside it.
            //      Claude says check SetCapture/ReleaseCapture will handle this.
            //      Also we do not handle anything from the mousewheel other than it being clicked.
            //

            case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            {
                b32 IsDown = ((Message.message == WM_LBUTTONDOWN) ||
                              (Message.message == WM_MBUTTONDOWN) ||
                              (Message.message == WM_RBUTTONDOWN));

                // Primary is left button for right-handed mouse, right button for a mouse the user
                //      has set up to be left-handed with Windows; vice-versa for Secondary
                game_button_state *Button = &Mouse->Primary;
                if((Message.message == WM_MBUTTONDOWN) || (Message.message == WM_MBUTTONUP))
                {
                    Button = &Mouse->WheelClick;
                }
                else if((Message.message == WM_RBUTTONDOWN) || (Message.message == WM_RBUTTONUP))
                {
                    Button = &Mouse->Secondary;
                }
                Win32ProcessKeyboardAndMouseMessage(Button, IsDown);
            } break;

            default:
            {
                TranslateMessage(&Message);
                DispatchMessageA(&Message);
            } break;
        }
    }
}

inline f32
GetSecondsElapsed(u64 Start, u64 End)
{
    f32 Result = ((f32)(End - Start) / (f32)GlobalCpuFreq);
    return(Result);
}

#if 0

static void
Win32DebugDrawVertical(win32_offscreen_buffer *Backbuffer,
                       int X, int Top, int Bottom, u32 Color)
{
    if(Top <= 0)
    {
        Top = 0;
    }

    if(Bottom > Backbuffer->Height)
    {
        Bottom = Backbuffer->Height;
    }
    
    if((X >= 0) && (X < Backbuffer->Width))
    {
        u8 *Pixel = ((u8 *)Backbuffer->Memory +
                        X*Backbuffer->BytesPerPixel +
                        Top*Backbuffer->Pitch);
        for(int Y = Top;
            Y < Bottom;
            ++Y)
        {
            *(u32 *)Pixel = Color;
            Pixel += Backbuffer->Pitch;
        }
    }
}

inline void
Win32DrawSoundBufferMarker(win32_offscreen_buffer *Backbuffer,
                           win32_sound_output *SoundOutput,
                           f32 C, int PadX, int Top, int Bottom,
                           DWORD Value, u32 Color)
{
    f32 Xf32 = (C * (f32)Value);
    int X = PadX + (int)Xf32;
    Win32DebugDrawVertical(Backbuffer, X, Top, Bottom, Color);
}

static void
Win32DebugSyncDisplay(win32_offscreen_buffer *Backbuffer,
                      int MarkerCount, win32_debug_time_marker *Markers,
                      int CurrentMarkerIndex,
                      win32_sound_output *SoundOutput, f32 TargetSecondsPerFrame)
{
    int PadX = 16;
    int PadY = 16;

    int LineHeight = 64;
    
    f32 C = (f32)(Backbuffer->Width - 2*PadX) / (f32)SoundOutput->SecondaryBufferSize;
    for(int MarkerIndex = 0;
        MarkerIndex < MarkerCount;
        ++MarkerIndex)
    {
        win32_debug_time_marker *ThisMarker = &Markers[MarkerIndex];
        Assert(ThisMarker->OutputPlayCursor < SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputWriteCursor < SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputLocation < SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputByteCount < SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->FlipPlayCursor < SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->FlipWriteCursor < SoundOutput->SecondaryBufferSize);

        DWORD PlayColor = 0xFFFFFFFF;
        DWORD WriteColor = 0xFFFF0000;
        DWORD ExpectedFlipColor = 0xFFFFFF00;
        DWORD PlayWindowColor = 0xFFFF00FF;

        int Top = PadY;
        int Bottom = PadY + LineHeight;
        if(MarkerIndex == CurrentMarkerIndex)
        {
            Top += LineHeight+PadY;
            Bottom += LineHeight+PadY;

            int FirstTop = Top;
            
            Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputPlayCursor, PlayColor);
            Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputWriteCursor, WriteColor);

            Top += LineHeight+PadY;
            Bottom += LineHeight+PadY;

            Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputLocation, PlayColor);
            Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputLocation + ThisMarker->OutputByteCount, WriteColor);

            Top += LineHeight+PadY;
            Bottom += LineHeight+PadY;

            Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, FirstTop, Bottom, ThisMarker->ExpectedFlipPlayCursor, ExpectedFlipColor);
        }        
        
        Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipPlayCursor, PlayColor);
        Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipPlayCursor + 480*SoundOutput->BytesPerSample, PlayWindowColor);
        Win32DrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipWriteCursor, WriteColor);
    }
}

#endif

int CALLBACK
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR CommandLine,
        int ShowCode)
{
    win32_state Win32State = {};
    GlobalWin32State = &Win32State;

    GlobalCpuFreq = GuessCpuTimerFreq();

    Win32GetEXEFileName(&Win32State);

    char SourceGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
    Win32BuildEXEPathFileName(&Win32State, "tufty.dll",
                              sizeof(SourceGameCodeDLLFullPath), SourceGameCodeDLLFullPath);

    char TempGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
    Win32BuildEXEPathFileName(&Win32State, "handmade_temp.dll",
                              sizeof(TempGameCodeDLLFullPath), TempGameCodeDLLFullPath);

    // NOTE(casey): Set the Windows scheduler granularity to 1ms
    // so that our Sleep() can be more granular.
    UINT DesiredSchedulerMS = 1;
    b32 SleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);
    
    Win32LoadXInput();
    
    WNDCLASSA WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackbuffer, 1920, 1080);
    
    WindowClass.style = CS_HREDRAW|CS_VREDRAW;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.hCursor = LoadCursorA(0, IDC_ARROW);
//    WindowClass.hIcon;
    WindowClass.lpszClassName = "TuftyWindowClass";

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(RegisterClassA(&WindowClass))
    {
        RECT WindowRect = {};
        WindowRect.left = 0;
        WindowRect.top = 0;
        WindowRect.right = GlobalBackbuffer.Width + 2*WIN32_BACKBUFFER_OFFSET_X;
        WindowRect.bottom = GlobalBackbuffer.Height + 2*WIN32_BACKBUFFER_OFFSET_Y;
        
        DWORD WindowStyle = WS_OVERLAPPEDWINDOW|WS_VISIBLE;
        AdjustWindowRect(&WindowRect, WindowStyle, FALSE); 

        int WindowWidth = WindowRect.right - WindowRect.left;
        int WindowHeight = WindowRect.bottom - WindowRect.top;

        Window =
            CreateWindowExA(
                0, // WS_EX_TOPMOST|WS_EX_LAYERED,
                WindowClass.lpszClassName,
                "Tufty",
                WindowStyle,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                WindowWidth,
                WindowHeight,
                0,
                0,
                Instance,
                0);
        if(Window)
        {
            UINT DPI = GetDpiForWindow(Window);

            RECT ClientRect = {};
            ClientRect.right = GlobalBackbuffer.Width + 2*WIN32_BACKBUFFER_OFFSET_X;
            ClientRect.bottom = GlobalBackbuffer.Height + 2*WIN32_BACKBUFFER_OFFSET_Y;
            AdjustWindowRectExForDpi(&ClientRect, WindowStyle, FALSE, 0, DPI);

            SetWindowPos(Window, 0, 0, 0,
                         ClientRect.right - ClientRect.left,
                         ClientRect.bottom - ClientRect.top,
                         SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);

            win32_sound_output SoundOutput = {};
            WA_Start(&WasapiAudio);

            // TODO(casey): How do we reliably query on this on Windows?
            int MonitorRefreshHz = 60;
            HDC RefreshDC = GetDC(Window);
            int Win32RefreshRate = GetDeviceCaps(RefreshDC, VREFRESH);
            ReleaseDC(Window, RefreshDC);
            if(Win32RefreshRate > 1)
            {
                MonitorRefreshHz = Win32RefreshRate;
            }
            f32 GameUpdateHz = MonitorRefreshHz;
            f32 TargetSecondsPerFrame = 1.0f / (f32)GameUpdateHz;

            SoundOutput.SamplesPerSecond = WasapiAudio.BufferFormat->nSamplesPerSec;
            SoundOutput.BytesPerSample = sizeof(s16)*2;

            GlobalRunning = true;

            // TODO(casey): Pool with bitmap VirtualAlloc
            s16 *Samples = (s16 *)VirtualAlloc(0, WasapiAudio.BufferFormat->nSamplesPerSec * sizeof(s16) * 2,
                                                   MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

            
#if TUFTY_INTERNAL
            LPVOID BaseAddress = (LPVOID)Terabytes(2);
#else
            LPVOID BaseAddress = 0;
#endif
            
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

            // TODO(casey): Handle various memory footprints (USING SYSTEM METRICS)
            // TODO(casey): Use MEM_LARGE_PAGES and call adjust token
            // privileges when not on Windows XP?
            Win32State.TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
            Win32State.GameMemoryBlock = VirtualAlloc(BaseAddress, (size_t)Win32State.TotalSize,
                                                      MEM_RESERVE|MEM_COMMIT,
                                                      PAGE_READWRITE);
            GameMemory.PermanentStorage = Win32State.GameMemoryBlock;
            GameMemory.TransientStorage = ((u8 *)GameMemory.PermanentStorage +
                                           GameMemory.PermanentStorageSize);

            for(int ReplayIndex = 0;
                ReplayIndex < ArrayCount(Win32State.ReplayBuffers);
                ++ReplayIndex)
            {
                win32_replay_buffer *ReplayBuffer = &Win32State.ReplayBuffers[ReplayIndex];

                // TODO(casey): Recording system still seems to take too long
                // on record start - find out what Windows is doing and if
                // we can speed up / defer some of that processing.
                
                Win32GetInputFileLocation(&Win32State, false, ReplayIndex,
                                          sizeof(ReplayBuffer->FileName), ReplayBuffer->FileName);

                ReplayBuffer->FileHandle =
                    CreateFileA(ReplayBuffer->FileName,
                                GENERIC_WRITE|GENERIC_READ, 0, 0, CREATE_ALWAYS, 0, 0);

                LARGE_INTEGER MaxSize;
                MaxSize.QuadPart = Win32State.TotalSize;
                ReplayBuffer->MemoryMap = CreateFileMapping(
                    ReplayBuffer->FileHandle, 0, PAGE_READWRITE,
                    MaxSize.HighPart, MaxSize.LowPart, 0);

                ReplayBuffer->MemoryBlock = MapViewOfFile(
                    ReplayBuffer->MemoryMap, FILE_MAP_ALL_ACCESS, 0, 0, Win32State.TotalSize);
                if(ReplayBuffer->MemoryBlock)
                {
                }
                else
                {
                    // TODO(casey): Diagnostic
                }
            }

            if(Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage)
            {
                game_input Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[1];
    
                // this is now a global because we need to reset it in case a dialog was open this frame
                u64 LastCounter = ReadCpuTimer();

                win32_game_code Game = Win32LoadGameCode(SourceGameCodeDLLFullPath,
                                                         TempGameCodeDLLFullPath);
                u32 LoadCounter = 0;

                while(GlobalRunning)
                {
                    NewInput->dtForFrame = TargetSecondsPerFrame;
                    
                    FILETIME NewDLLWriteTime = Win32GetLastWriteTime(SourceGameCodeDLLFullPath);
                    if(CompareFileTime(&NewDLLWriteTime, &Game.DLLLastWriteTime) != 0)
                    {
                        Win32UnloadGameCode(&Game);
                        Game = Win32LoadGameCode(SourceGameCodeDLLFullPath,
                                                 TempGameCodeDLLFullPath);
                        LoadCounter = 0;
                    }

                    // TODO(casey): Zeroing macro
                    // TODO(casey): We can't zero everything because the up/down state will
                    // be wrong!!!
                    game_controller_input *OldKeyboardController = GetController(OldInput, 0);
                    game_controller_input *NewKeyboardController = GetController(NewInput, 0);
                    *NewKeyboardController = {};
                    NewKeyboardController->IsConnected = true;
                    for(int ButtonIndex = 0;
                        ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                        ++ButtonIndex)
                    {
                        NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                            OldKeyboardController->Buttons[ButtonIndex].EndedDown;
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

                    POINT MouseP;
                    GetCursorPos(&MouseP);
                    ScreenToClient(Window, &MouseP);
                    NewMouse->X = MouseP.x - WIN32_BACKBUFFER_OFFSET_X;
                    NewMouse->Y = MouseP.y - WIN32_BACKBUFFER_OFFSET_Y;

                    dev_keys *OldDevKeys = &OldInput->DevKeys;
                    dev_keys *NewDevKeys = &NewInput->DevKeys;
                    *NewDevKeys = {};
                    for(int FnKeyIdx = 0;
                        FnKeyIdx < ArrayCount(NewDevKeys->Keys);
                        ++FnKeyIdx)
                    {
                        NewDevKeys->Keys[FnKeyIdx].EndedDown = OldDevKeys->Keys[FnKeyIdx].EndedDown;
                    }

                    Win32ProcessPendingMessages(&Win32State, NewKeyboardController, NewDevKeys, NewMouse);

                    // NOTE(Aaron): We hoist these to globals so that we can clean them up
                    //      in the case that a file dialog ran this frame
                    GlobalKeyboardController = NewKeyboardController;
                    GlobalDevKeys = NewDevKeys;
                    GlobalMouse = NewMouse;

                    if(!GlobalPause)
                    {
                        // TODO(casey): Need to not poll disconnected controllers to avoid
                        // xinput frame rate hit on older libraries...
                        // TODO(casey): Should we poll this more frequently
                        DWORD MaxControllerCount = XUSER_MAX_COUNT;
                        if(MaxControllerCount > (ArrayCount(NewInput->Controllers) - 1))
                        {
                            MaxControllerCount = (ArrayCount(NewInput->Controllers) - 1);
                        }
                
                        for (DWORD ControllerIndex = 0;
                             ControllerIndex < MaxControllerCount;
                             ++ControllerIndex)
                        {
                            DWORD OurControllerIndex = ControllerIndex + 1;
                            game_controller_input *OldController = GetController(OldInput, OurControllerIndex);
                            game_controller_input *NewController = GetController(NewInput, OurControllerIndex);
                    
                            XINPUT_STATE ControllerState;
                            if(XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                            {
                                NewController->IsConnected = true;
                                NewController->IsAnalog = OldController->IsAnalog;
                           
                                // NOTE(casey): This controller is plugged in
                                // TODO(casey): See if ControllerState.dwPacketNumber increments too rapidly
                                XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                                // TODO(casey): This is a square deadzone, check XInput to
                                // verify that the deadzone is "round" and show how to do
                                // round deadzone processing.
                                NewController->StickAverageX = Win32ProcessXInputStickValue(
                                    Pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                                NewController->StickAverageY = Win32ProcessXInputStickValue(
                                    Pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                                if((NewController->StickAverageX != 0.0f) ||
                                   (NewController->StickAverageY != 0.0f))
                                {
                                    NewController->IsAnalog = true;
                                }

                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)
                                {
                                    NewController->StickAverageY = 1.0f;
                                    NewController->IsAnalog = false;
                                }
                            
                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
                                {
                                    NewController->StickAverageY = -1.0f;
                                    NewController->IsAnalog = false;
                                }
                            
                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
                                {
                                    NewController->StickAverageX = -1.0f;
                                    NewController->IsAnalog = false;
                                }
                            
                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
                                {
                                    NewController->StickAverageX = 1.0f;
                                    NewController->IsAnalog = false;
                                }

                                f32 Threshold = 0.5f;
                                Win32ProcessXInputDigitalButton(
                                    (NewController->StickAverageX < -Threshold) ? 1 : 0,
                                    &OldController->MoveLeft, 1,
                                    &NewController->MoveLeft);
                                Win32ProcessXInputDigitalButton(
                                    (NewController->StickAverageX > Threshold) ? 1 : 0,
                                    &OldController->MoveRight, 1,
                                    &NewController->MoveRight);
                                Win32ProcessXInputDigitalButton(
                                    (NewController->StickAverageY < -Threshold) ? 1 : 0,
                                    &OldController->MoveDown, 1,
                                    &NewController->MoveDown);
                                Win32ProcessXInputDigitalButton(
                                    (NewController->StickAverageY > Threshold) ? 1 : 0,
                                    &OldController->MoveUp, 1,
                                    &NewController->MoveUp);

                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionDown, XINPUT_GAMEPAD_A,
                                                                &NewController->ActionDown);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionRight, XINPUT_GAMEPAD_B,
                                                                &NewController->ActionRight);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionLeft, XINPUT_GAMEPAD_X,
                                                                &NewController->ActionLeft);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionUp, XINPUT_GAMEPAD_Y,
                                                                &NewController->ActionUp);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                                &NewController->LeftShoulder);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                                &NewController->RightShoulder);

                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->Start, XINPUT_GAMEPAD_START,
                                                                &NewController->Start);
                                Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->Back, XINPUT_GAMEPAD_BACK,
                                                                &NewController->Back);
                            }
                            else
                            {
                                // NOTE(casey): The controller is not available
                                NewController->IsConnected = false;
                            }
                        }

                        game_offscreen_buffer Buffer = {};
                        Buffer.Memory = GlobalBackbuffer.Memory;
                        Buffer.Width = GlobalBackbuffer.Width; 
                        Buffer.Height = GlobalBackbuffer.Height;
                        Buffer.Pitch = GlobalBackbuffer.Pitch;
                        Buffer.BytesPerPixel = GlobalBackbuffer.BytesPerPixel;

                        if(Win32State.InputRecordingIndex)
                        {
                            Win32RecordInput(&Win32State, NewInput);
                        }

                        if(Win32State.InputPlayingIndex)
                        {
                            Win32PlayBackInput(&Win32State, NewInput);
                        }
                        if(Game.UpdateAndRender)
                        {
                            // HACK: I am not sure if this is the correct place to do this,
                            //      but for now this is one way to get a suitably random value 
                            //      that we need for seeding the random series in the game code 
                            //      when initializing the game (AS, 9/4/26)
                            NewInput->CpuTimerReading = ReadCpuTimer();
                            Game.UpdateAndRender(&GameMemory, NewInput, &Buffer);
                            if(GlobalDialogWasOpen)
                            {
                                // Cleanup in the case that the save/load dialog was open.
                                //      We need to reset LastCounter since the game is 
                                //      locked on the frame in which the save/load dialog
                                //      was opened, and when we try to compute a FPS value based on 
                                //      the LastCounter value corresponding to that frame, we'll 
                                //      get a misleading reading suggesting that one frame took multiple seconds to
                                //      run.
                                //
                                //      AS, 9.26.26
                                GlobalDialogWasOpen = false;
                                LastCounter = ReadCpuTimer();
                            }
                        }

                        u32 Padding;
                        HR(WasapiAudio.Client->GetCurrentPadding(&Padding));
                        u32 WriteCount = WasapiAudio.BufferSamples - Padding;

                        game_sound_output_buffer SoundBuffer = {};
                        SoundBuffer.SamplesPerSecond = WasapiAudio.BufferFormat->nSamplesPerSec;
                        SoundBuffer.SampleCount = WriteCount;
                        SoundBuffer.Samples = Samples;
                        if(Game.GetSoundSamples)
                        {
                            Game.GetSoundSamples(&GameMemory, &SoundBuffer);
                        }

                        BYTE *Output;
                        HR(WasapiAudio.RenderClient->GetBuffer(WriteCount, &Output));
                        float *Dest = (float *)Output;
                        s16 *Src = Samples;
                        for(u32 SampleIndex = 0;
                            SampleIndex < WriteCount * 2;
                            ++SampleIndex)
                        {
                            Dest[SampleIndex] = Src[SampleIndex] * (1.0f / 32768.0f);
                        }
                        HR(WasapiAudio.RenderClient->ReleaseBuffer(WriteCount, 0));
                    
                        u64 WorkCounter = ReadCpuTimer();
                        f32 WorkSecondsElapsed = GetSecondsElapsed(LastCounter, WorkCounter);

                        // TODO(casey): NOT TESTED YET!  PROBABLY BUGGY!!!!!
                        f32 SecondsElapsedForFrame = WorkSecondsElapsed;
                        if(SecondsElapsedForFrame < TargetSecondsPerFrame)
                        {                        
                            if(SleepIsGranular)
                            {
                                DWORD SleepMS = (DWORD)(1000.0f * (TargetSecondsPerFrame -
                                                                   SecondsElapsedForFrame));
                                if(SleepMS > 0)
                                {
                                    Sleep(SleepMS);
                                }
                            }

                            f32 TestSecondsElapsedForFrame = GetSecondsElapsed(LastCounter, ReadCpuTimer());
                            if(TestSecondsElapsedForFrame < TargetSecondsPerFrame)
                            {
                                // TODO(casey): LOG MISSED SLEEP HERE
                            }
                        
                            while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                            {                            
                                SecondsElapsedForFrame = GetSecondsElapsed(LastCounter, ReadCpuTimer());
                            }
                        }
                        else
                        {
                            // TODO(casey): MISSED FRAME RATE!
                            // TODO(casey): Logging
                        }
                
                        u64 EndCounter = ReadCpuTimer();
                        OldInput->Fps = (f32)GlobalCpuFreq / (EndCounter - LastCounter);
                        LastCounter = EndCounter;
                        

                        win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                        HDC DeviceContext = GetDC(Window);
                        Win32DisplayBufferInWindow(&GlobalBackbuffer, DeviceContext,
                                                   Dimension.Width, Dimension.Height);
                        ReleaseDC(Window, DeviceContext);


                        game_input *Temp = NewInput;
                        NewInput = OldInput;
                        OldInput = Temp;
                    }
                }
            }
            else
            {
                // TODO(casey): Logging
            }

            WA_Stop(&WasapiAudio);
        }
        else
        {
            // TODO(casey): Logging
        }
    }
    else
    {
        // TODO(casey): Logging
    }

    return(0);
}

#include <intrin.h>

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
    LARGE_INTEGER Value;
    QueryPerformanceCounter(&Value);
    return(Value.QuadPart);
}

static u64
ReadCpuTimer(void)
{
    return __rdtsc();
}

static u64
GuessCpuFreq(void)
{
    u64 Result = 0;

    u64 MsToWait = 100;
    u64 OsFreq = GetOsTimerFreq();

    u64 CpuStart = ReadCpuTimer();
    u64 OsStart = ReadOsTimer();
    u64 OsEnd = 0;
    u64 OsElapsed = 0;
    u64 OsWaitTime = OsFreq * MsToWait / 1000;

    while(OsElapsed < OsWaitTime)
    {
        OsEnd = ReadOsTimer();
        OsElapsed = OsEnd - OsStart;
    }
    u64 CpuEnd = ReadCpuTimer();
    u64 CpuElapsed = CpuEnd - CpuStart;

    if(OsElapsed) // to avoid any case of divide by 0
    {
        Result = CpuElapsed * OsFreq / OsElapsed;
    }

    return(Result);
}

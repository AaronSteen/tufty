static void
PrintTimeElapsed(u64 TotalElapsed, profile_anchor *pAnchor)
{
    f64 ExclusivePercent = 100.0 * (f64)pAnchor->TscElapsedExclusive / (f64)TotalElapsed;
    if (pAnchor->TscElapsedExclusive != pAnchor->TscElapsedInclusive) {
        f64 InclusivePercent = 100.0 * (f64)pAnchor->TscElapsedInclusive / (f64)TotalElapsed;
        DebugOutput("   %s[%llu]: %llu (%.2f%%, %.2f%% with children)", pAnchor->Label, pAnchor->HitCount, pAnchor->TscElapsedExclusive, ExclusivePercent, InclusivePercent);
    }
    else {
        DebugOutput("   %s[%llu]: %llu (%.2f%%)", pAnchor->Label, pAnchor->HitCount, pAnchor->TscElapsedExclusive, ExclusivePercent);
    }
}


static void
PrintAnchorData(u64 TotalCpuElapsed)
{
    for (int AnchorIdx = 1; AnchorIdx < MAX_ANCHORS; ++AnchorIdx) {
        profile_anchor *pAnchor = pgSharedProfData->gProfileAnchors + AnchorIdx;
        if (pAnchor->TscElapsedInclusive) {
            PrintTimeElapsed(TotalCpuElapsed, pAnchor);
        }
    }
}

struct profiler
{
    u64 StartTsc;
    u64 EndTsc;
};
static profiler gProfiler;

static void
BeginProfile(void)
{
    gProfiler.StartTsc = ReadCpuTimer();
}

static void
EndAndPrintProfile(void)
{
    gProfiler.EndTsc = ReadCpuTimer();
    u64 CpuFreq = EstimateCpuTimerFreq();

    u64 TotalCpuElapsed = gProfiler.EndTsc - gProfiler.StartTsc;

    if (CpuFreq) {
        f64 MsElapsed = 1000.0 * (f64)TotalCpuElapsed / (f64)CpuFreq;
        DebugOutput("Total time: %.4fms (Cpu freq %llu); Total elapsed: %llu", MsElapsed, CpuFreq, TotalCpuElapsed);
    }

    PrintAnchorData(TotalCpuElapsed);
}

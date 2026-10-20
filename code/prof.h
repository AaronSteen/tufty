#ifndef PROFILER
#define PROFILER 0
#endif

static global_shared_prof_data *pgSharedProfData;

#if PROFILER

struct profile_block
{
    char const *Label;
    u64 OldTscElapsedInclusive;
    u64 StartTsc;
    u32 ParentIdx;
    u32 AnchorIdx;

    profile_block(char const *Label_, u32 AnchorIdx_)
    {
        ParentIdx = pgSharedProfData->gProfilerParent;
        AnchorIdx = AnchorIdx_;
        Label = Label_;
        pgSharedProfData->gProfilerParent = AnchorIdx;
        OldTscElapsedInclusive = pgSharedProfData->gProfileAnchors[AnchorIdx].TscElapsedInclusive;
        StartTsc = ReadCpuTimer();
    }

    ~profile_block(void)
    {
        u64 Elapsed = ReadCpuTimer() - StartTsc;
        pgSharedProfData->gProfilerParent = ParentIdx;

        profile_anchor *pParent = pgSharedProfData->gProfileAnchors + ParentIdx;
        profile_anchor *pAnchor = pgSharedProfData->gProfileAnchors + AnchorIdx;

        pParent->TscElapsedExclusive -= Elapsed;
        pAnchor->TscElapsedExclusive += Elapsed;
        pAnchor->TscElapsedInclusive = OldTscElapsedInclusive + Elapsed;
        ++pAnchor->HitCount;

        pAnchor->Label = Label;
    }
};

#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define TimeBlock(Name) profile_block NameConcat(Block, __LINE__)(Name, PROFILE_ANCHORS_BASE + __COUNTER__ + 1);
#define ProfilerEndOfCompilationUnit static_assert((PROFILE_ANCHORS_BASE + __COUNTER__ < PROFILE_ANCHORS_LIMIT), "Number of profile points exceeds size of profiler::Anchors array")

// Will expand to, e.g.
//     profile_block Block176(__func__, 2 + 1);; (TimeFunction)
//     profile_block Block192("Lookup and Convert", 3 + 1);; (TimeBlock)
//
//      Block is stringized by NameConcat with the line number to produce Block176
//      The parentheses following are literal parentheses from this: 
//          (Name, __COUNTER__ + 1)
//      Name becomes either __func__ in the case of TimeFunction, (which is copied as the label in the the anchor struct),
//          or whatever string is passed in. 
//      Then, when the constructor below runs, __COUNTER__ + 1 becomes the index for writing into gProfiler->Anchors,
//          current gProfilerParent is temporarily stored, and replaced by AnchorIdx while while the block is running,
//          since it is the current scope.
//      profile_block(char const *Label_, u32 AnchorIdx_)
//      {
//          ParentIdx = gProfilerParent;
//          AnchorIdx = AnchorIdx_;
//          Label = Label_;
//          gProfilerParent = AnchorIdx;
//          StartTsc = ReadCpuTimer();
//      }
//      Finally, when the profile_block struct is destroyed:
//      ~profile_block(void)
//      {
//          u64 Elapsed = ReadCpuTimer() - StartTsc;
//          gProfilerParent = ParentIdx;
//          profile_anchor *pParent = gProfileAnchors + ParentIdx;
//          profile_anchor *pAnchor = gProfileAnchors + AnchorIdx;
//          pParent->TscElapsedChildren += Elapsed;
//          pAnchor->TscElapsed += Elapsed;
//          ++pAnchor->HitCount;
//          pAnchor->Label = Label;
//      }
//
//      gProfilerParent is restored to ParentIdx, and pParent's Elapsed children is updated.
#else

#define TimeBlock(...)
#define PrintAnchorData(...)
#define ProfilerEndOfCompilationUnit

#endif

#define TimeFunction TimeBlock(__func__)

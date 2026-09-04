// Potentially useful code snippets that I don't want to keep in the game code but
//      don't want to throw away.

// This code tested that we were properly reading function key, e.g., F1, F2 etc., properly. 
//      When a function key was pressed, it drew that function key on the screen
//      at a given position, then faded it out over an arbitrary number of frames (in this case, 180, or
//      3 seconds if the game was running at 60 FPS). It might work as a prototype for a simple animation
//      system, because it varies the way something is drawn based on a running frame count. For example,
//      if an animation has 10 frames and lasts 1 second, and our game runs at 60 frames per second, 
//      we could initiate a counter that ticks once per frame, and advance the animation to the next frame 
//      based on the counter:
//          - The first frame of animation is shown when 0 <= counter <= 5
//          - The second frame is shown when 6 <= counter <= 11
//          - The third frame is shown when 12 <= counter <= 17
//          - Fourth frame, 18 <= counter <= 23
//          - Fifth frame, 24 <= counter <= 29
//          - Sixth frame, 30 <= counter <= 35
//          - Seventh frame, 36 <= counter <= 41
//          - Eighth frame, 42 <= counter <= 47
//          - Ninth frame, 48 <= counter <= 53
//          - Tenth frame, 54 <= counter <= 59
//  
//  ... in the code below, when a function key is pressed, the counter for that function key
//      is set to zero. Any function keys with a frame count of less than 180 are drawn on the screen,
//      and their opacity is determined by 1 - FrameCount / 180. Thus on frame zero they have
//      100% opacity, and on frame 180 they have 0 opacity (not visible).
//
//      (AS, 9/4/26)


struct fn_key_attributes
{
    u64 FrameCount;
    f32 Opacity;
};

static fn_key_attributes FnKeyAttributes[10];
static b32 FnKeysInit = false;
if(!FnKeysInit)
{
    for(int i = 0;
        i < ArrayCount(FnKeyAttributes);
        ++i)
    {
        FnKeyAttributes[i].FrameCount = 180;
    }

    FnKeysInit = true;
}

for(int FnKeyIdx = 0;
    FnKeyIdx < ArrayCount(Input->FunctionKeys.Keys);
    ++FnKeyIdx)
{
    fn_key_attributes *It = FnKeyAttributes + FnKeyIdx;
    if(Input->FunctionKeys.Keys[FnKeyIdx].EndedDown && Input->FunctionKeys.Keys[FnKeyIdx].HalfTransitionCount == 1)
    {
        It->FrameCount = 0;
    }

    if(It->FrameCount < 180)
    {
        It->Opacity = 1.0f - ((f32)It->FrameCount / 180.0f);
        f32 PrintWidth = (f32)Buffer->Width * 0.1f;
        f32 X = 30.0f + (f32)FnKeyIdx * PrintWidth;
        f32 Y = Buffer->Height - (Buffer->Height * 0.33f);
        char Temp[255];
        snprintf(Temp, 255, "F%d", FnKeyIdx+1); 
        DEBUGDrawText(Buffer, X, Y, Temp, DebugState->DebugTextBuf, 0.1f, 0.1f, 1, It->Opacity);
    }

    ++It->FrameCount;
}


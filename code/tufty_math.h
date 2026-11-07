#pragma once
#include "tufty.h"

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

static f32
Square(f32 A)
{
    f32 Result = A * A;

    return Result;
}

static f32
Dot(v2 A, v2 B)
{
    f32 Result = A.X * B.X + A.Y * B.Y;

    return Result;
}



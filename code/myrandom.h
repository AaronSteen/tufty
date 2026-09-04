struct random_series
{
    u64 A, B, C, D;
};

static u64 
RotateLeft(u64 V, int Shift)
{

    // e.g., V = 1111 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0001 
    //
    // Rotate left 7
    //
    // (V << Shift) = (V << 7) = 
    //          0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 1000 0000 
    //
    // (V >> (64 - Shift)) = (V >> (64 - 7)) = (V >> 57) = 
    //          0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0111 1000     
    //
    // (OR together)
    //          0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 1000 0000 
    //                                              OR
    //          0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0111 1000     
    //
    //        = 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 1111 1000     
    u64 Result = ((V << Shift) | (V >> (64 - Shift)));
    return(Result);
}

static u64
RandomU64(random_series *Series)
{
    u64 A = Series->A;
    u64 B = Series->B;
    u64 C = Series->C;
    u64 D = Series->D;

    u64 E = A - RotateLeft(B, 27);

    A = (B ^ RotateLeft(C, 17));
    B = (C + D);
    C = (D + E);
    D = (E + A);

    Series->A = A;
    Series->B = B;
    Series->C = C;
    Series->D = D;

    return(D);
}

random_series
SeedRandomSeries(u64 Value)
{
    random_series Series = {};

    Series.A = 0xf1ea5eed;
    Series.B = Value;
    Series.C = Value;
    Series.D = Value;

    u32 Count = 20;
    while(Count--)
    {
        RandomU64(&Series);
    }

    return(Series);
}

static f64
RandomF64InRange(random_series *Series, f64 Min, f64 Max)
{
    // NOTE: We have our own lerp function but since we're just grabbing this
    //      from another project that defined it this way, and it works
    //      with the rest of the functions in this file, we just keep this as is.
    //
    //      AS, 9/4/26
    //
    f64 t = (f64)RandomU64(Series) / (f64)UINT64_MAX;
    f64 Result = Min + t*(Max - Min);

    return(Result);
}

static s32
RandomS32InRange(random_series *Series, int Min, int Max)
{
    s32 Result;
    f64 Double = RandomF64InRange(Series, (f64)Min, (f64)Max);
    Result = RoundF32ToS32((f32)Double);
    
    return(Result);
}

static f64
RandomDegree(random_series *Series, f64 Center, f64 Radius, f64 MaxAllowed)
{
    f64 Min = Center - Radius;
    if(Min < -MaxAllowed)
    {
        Min = -MaxAllowed;
    }

    f64 Max = Center + Radius;
    if(Max > MaxAllowed)
    {
        Max = MaxAllowed;
    }

    f64 Result = RandomF64InRange(Series, Min, Max);
    return(Result);
}

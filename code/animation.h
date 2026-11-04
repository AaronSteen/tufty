static b32
IsAnimationSlotEmpty(animation Animation)
{
    b32 Result = false;
    if (strlen(Animation.Filepath) == 0) {
        Result = true;
    }
    return Result;
}

static int
GetNextFreeAnimationSlot(const arr_animation *pArrAnimation)
{
    int Result = 0;
    for (int Slot = 1; Slot <= MAX_ANIMATIONS_PER_FACING; ++Slot) {
        animation Animation = pArrAnimation->Animations[Slot];
        if (IsAnimationSlotEmpty(Animation)) {
            Result = Slot;
            break;
        }
    }
    return Result;
}

static int
LoadAnimation(arr_animation *pAnimationArray, const char *pFilepath)
{
    // terminology.
    //      "Cels" comprise "Animations". Cels are just bitmaps.
    //      When an animation is played, the first Cel is shown on the screen for
    //          its specified ms. When that ms has elapsed, the next Cel is shown, etc.
    //      cute_aseprite uses "Cel" differently: To it, a "cel" is a single layer, in
    //          the photoshop sense. Multiple "cels" can be layered on top of each other 
    //          in Aseprite to form a "frame".
    //      So, our "cel" corresponds to cute_aseprite's "frame". But we don't
    //          want to use the term frame since that term already means
    //          something different in the context of our engine.
    int NextFreeSlot = GetNextFreeAnimationSlot(pAnimationArray); 
    if (NextFreeSlot == NIL) { return NIL; }

    ase_t *pAseprite = cute_aseprite_load_from_file(pFilepath, NULL);
    if (pAseprite == nullptr) { 
        return NIL; 
    }
    if (pAseprite->frame_count > MAX_ANIMATION_CELS) { 
        return NIL; 
    }
    if (pAseprite->mode != ASE_MODE_RGBA) {
        return NIL;
    }

    animation *pNewAnimation = pAnimationArray->Animations + NextFreeSlot;
    strncpy(pNewAnimation->Filepath, pFilepath, STRING_LEN + 1);
    pNewAnimation->Width = pAseprite->w;
    pNewAnimation->Height = pAseprite->h;

    int BytesPerPixel = 4;
    // Aseprite indexes from 0; we reserve 0 for NIL
    for (int AsepriteIdx = 0; AsepriteIdx < pAseprite->frame_count; ++AsepriteIdx) {
        ase_frame_t *pAsepriteCel = pAseprite->frames + AsepriteIdx;
        int GameIdx = AsepriteIdx + 1;
        pNewAnimation->MsForCels[GameIdx] = pAsepriteCel->duration_milliseconds;

        mem_idx NumBytesForPixels = pNewAnimation->Width * pNewAnimation->Height * BytesPerPixel;
        pNewAnimation->Cels[GameIdx].Pixels = PushArray(pAnimationArray->pContainingArena, u8, NumBytesForPixels);
        memcpy(pNewAnimation->Cels[GameIdx].Pixels, pAsepriteCel->pixels, NumBytesForPixels);
    }

    cute_aseprite_free(pAseprite);
    return NextFreeSlot;
}

static animation
GetAnimation(const arr_animation AnimationArray, int AnimationIdx)
{
    if (AnimationIdx < 1 || 
        AnimationIdx > MAX_ANIMATIONS_PER_FACING) {
            return AnimationArray.Animations[0];  // return the empty animation at idx 0
    }
    else {
        return AnimationArray.Animations[AnimationIdx];
    }
}

#if 0
static void
ResetAnimationState(animation_state *pAnimationState)
{
    pAnimationState->CurrCelIdx = 1;
    pAnimationState->MsInCurrCel = 0;
}
#endif

static void
UpdatePlayerAnimationCel(player *pPlayer, f32 dtForFrame)
{
    animation_state *pState = &pPlayer->AnimationState;
    // This function only updates the player's CurrAnimationCel and MsInCurrCel, based on
    //      how much time has passed since the last frame was shown.
    if (pPlayer->OldFacing != pPlayer->IsFacing) {
        // If player changed direction, reset the current cel to 1
        //      and the MsInCurrCel to 0
        pState->CurrCelIdx = 1;
        pState->MsInCurrCel = 0;
    }
    else {
        arr_animation AnimArray = pPlayer->FacingAnimationArray[pPlayer->IsFacing];
        pState->MsInCurrCel += dtForFrame;
        animation CurrAnimation = GetAnimation(AnimArray, pState->CurrAnimationIdx);
        Assert(IsAnimationSlotEmpty(CurrAnimation) == false);
        if (pState->MsInCurrCel > CurrAnimation.MsForCels[pState->CurrCelIdx]) {
            pState->MsInCurrCel -= CurrAnimation.MsForCels[pState->CurrCelIdx];

            int CelsInThisAnimation = 0;
            for (int NthCel = 1; NthCel <= MAX_ANIMATION_CELS; ++NthCel) {
                if (IsBitmapPresent(CurrAnimation.Cels[NthCel]) == false) {
                    break;
                }
                ++CelsInThisAnimation;
            }
            int NewCelIdx = pState->MsInCurrCel + 1;
            NewCelIdx = (NewCelIdx % CelsInThisAnimation) + 1;
            pState->MsInCurrCel = NewCelIdx;
        }
    }
}

static bitmap
GetPlayerAnimationCelForThisFrame(player Player)
{
    animation CurrAnimation = GetAnimation(Player.FacingAnimationArray[Player.IsFacing], 
                                           Player.AnimationState.CurrAnimationIdx);
    if (IsAnimationSlotEmpty(CurrAnimation)) {
        return pGlobalBagel->Bitmap;
    }
    else {
        return CurrAnimation.Cels[Player.AnimationState.CurrCelIdx]; 
    }
}


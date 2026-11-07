#pragma once
#include "tufty.h"

static b32
IsAnimationSlotEmpty(const animation *pAnimation)
{
    b32 Result = false;
    if (strlen(pAnimation->Filepath) == 0) {
        Result = true;
    }
    return Result;
}

static int
GetNextFreeAnimationSlot(arr_animation *pArrAnimation)
{
    int Result = 0;
    for (int Slot = 1; Slot <= MAX_ANIMATIONS_PER_FACING; ++Slot) {
        animation *pAnimation = pArrAnimation->Animations + Slot;
        if (IsAnimationSlotEmpty(pAnimation)) {
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
    //      An array of "Cels" comprises an "Animation". Cels are just bitmaps.
    //      When an animation is played, the first Cel is shown on the screen for
    //          its specified ms. When that ms has elapsed, the next Cel is shown, etc.
    //      cute_aseprite uses "Cel" differently: To it, a "cel" is a single layer, in
    //          the photoshop sense. Multiple "cels" can be layered on top of each other 
    //          in Aseprite to form a "frame".
    //      So, our "cel" corresponds to cute_aseprite's "frame". But we don't
    //          want to use the term "frame" since that term already means
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
    int BytesPerPixel = 4; // if mode is ASE_MODE_RGBA, bpp == 4

    animation *pNewAnimation = pAnimationArray->Animations + NextFreeSlot;
    strncpy(pNewAnimation->Filepath, pFilepath, STRING_LEN + 1);

    // Aseprite indexes from 0; we reserve 0 for NIL
    for (int AsepriteIdx = 0; AsepriteIdx < pAseprite->frame_count; ++AsepriteIdx) {
        ase_frame_t *pAsepriteCel = pAseprite->frames + AsepriteIdx;
        int GameIdx = AsepriteIdx + 1;
        pNewAnimation->MsForCels[GameIdx] = pAsepriteCel->duration_milliseconds;

        bitmap *pBitmap = pNewAnimation->Cels + GameIdx;
        pBitmap->Width = pAseprite->w;
        pBitmap->Height = pAseprite->h;
        pBitmap->BytesPerPixel = BytesPerPixel;
        pBitmap->Pitch = pBitmap->Width * pBitmap->BytesPerPixel;

        mem_idx NumBytesForPixels = pBitmap->Width * pBitmap->Height * BytesPerPixel;
        pBitmap->Pixels = PushArray(pAnimationArray->pContainingArena, u8, NumBytesForPixels);
        memcpy(pBitmap->Pixels, pAsepriteCel->pixels, NumBytesForPixels);
    }

    cute_aseprite_free(pAseprite);
    return NextFreeSlot;
}

static animation *
GetAnimation(arr_animation *pAnimationArray, int AnimationIdx)
{
    if (AnimationIdx < 1 || 
        AnimationIdx > MAX_ANIMATIONS_PER_FACING) {
            return (pAnimationArray->Animations + 0);  // return the empty animation at idx 0
    }
    else {
        return (pAnimationArray->Animations + AnimationIdx);
    }
}

static void
ResetAnimationStateCelParams(animation_state *pAnimationState)
{
    pAnimationState->CurrCelIdx = 1;
    pAnimationState->MsInCurrCel = 0;
}

static void
UpdatePlayerAnimationCel(player *pPlayer, f32 dtForFrame)
{
    f32 dtForFrameMs = dtForFrame * 1000.0f;
    animation_state *pState = &pPlayer->AnimationState;
    // This function only updates the player's CurrAnimationCel and MsInCurrCel, based on
    //      how much time has passed since the last frame was shown.
    if (pPlayer->OldFacing != pPlayer->IsFacing || pPlayer->TryingToMove == false) {
        // If player changed direction or has stopped moving, 
        //      reset the current cel to 1 and the MsInCurrCel to 0
        ResetAnimationStateCelParams(pState);
    }
    else {
        arr_animation *pAnimArray = &pPlayer->FacingAnimationArray[pPlayer->IsFacing];
        pState->MsInCurrCel += dtForFrameMs;
        animation *pCurrAnimation = GetAnimation(pAnimArray, pState->CurrAnimationIdx);
        assert(IsAnimationSlotEmpty(pCurrAnimation) == false);
        if (pState->MsInCurrCel > pCurrAnimation->MsForCels[pState->CurrCelIdx]) {
            pState->MsInCurrCel -= pCurrAnimation->MsForCels[pState->CurrCelIdx];

            int CelsInThisAnimation = 0;
            for (int NthCel = 1; NthCel <= MAX_ANIMATION_CELS; ++NthCel) {
                if (IsBitmapPresent(&pCurrAnimation->Cels[NthCel]) == false) {
                    break;
                }
                ++CelsInThisAnimation;
            }
            pState->CurrCelIdx = pState->CurrCelIdx % CelsInThisAnimation + 1;
        }
    }
}

static bitmap *
GetPlayerAnimationCelForThisFrame(player *pPlayer)
{
    animation *pCurrAnimation = GetAnimation(&pPlayer->FacingAnimationArray[pPlayer->IsFacing], 
                                           pPlayer->AnimationState.CurrAnimationIdx);
    if (IsAnimationSlotEmpty(pCurrAnimation)) {
        return &pGlobalBagel->Bitmap;
    }
    else {
        return &pCurrAnimation->Cels[pPlayer->AnimationState.CurrCelIdx]; 
    }
}


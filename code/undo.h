#pragma once
#include "tufty.h"

static int
GetCanonUndoIndex(int OriginIdx, int Step)
{
    // Passing a step further to the left of zero than
    //      the additive inverse of max undo steps is an error.
    // Realistically, we don't expect this function to be called 
    //      with step values that are not positive 1 or negative 1.
    assert(Step >= -MAX_UNDO_STEPS);

    int Result = ((OriginIdx + Step) % MAX_UNDO_STEPS);

    // Mod result takes sign of left operand, so if CurrIdx + step 
    //      evals to negative, we add max undo steps again to make it positive
    Result += MAX_UNDO_STEPS;

    // And lastly mod one more time to cover large positive step values.
    Result %= MAX_UNDO_STEPS;

    return Result;
}

static undo_step **
ReadUndoStep(undo_state *pUndoState, int Idx)
{
    undo_step **ppResult = &pUndoState->UndoStepsArr[Idx];
    return ppResult;
}

static void
WriteUndoStep(undo_step *pWriteHere, const s32 *pTileValues)
{
    memcpy(pWriteHere->TileValues, pTileValues, TILES_IN_WORLD_NUM_BYTES);
}

static undo_step *
PushNewUndoStep(arena *pArena)
{
    // The returned struct will have a valid TileValues pointer but 
    //      will not have any values in the array; caller must copy these in
    undo_step *pNew = PushStruct(pArena, undo_step);
    pNew->TileValues = PushArray(pArena, s32, TILES_IN_WORLD);
    return pNew;
}

static void
IncrementUndoHead(undo_state *pUndoState, undo_step ToPush)
{
    int NewIdx = GetCanonUndoIndex(pUndoState->HeadIdx, 1);
    undo_step **ppPushHere = ReadUndoStep(pUndoState, NewIdx);
    if (*ppPushHere == nullptr) {
        *ppPushHere = PushNewUndoStep(&pUndoState->UndoStepsArena);
    }
    WriteUndoStep(*ppPushHere, ToPush.TileValues);
    pUndoState->HeadIdx = NewIdx;
    pUndoState->CurrIdx = NewIdx;
}

static b32
IsUndoStepValid(undo_state *pUndoState, int ProspectiveStep)
{
    undo_step **ppCheck = ReadUndoStep(pUndoState, ProspectiveStep);
    if (*ppCheck == nullptr ||                            
        ProspectiveStep == pUndoState->HeadIdx) {
            return false;
    }
    else {
        return true;
    }
}

static b32
IsRedoStepValid(undo_state *pUndoState, int ProspectiveStep)
{
    undo_step **ppCheck = ReadUndoStep(pUndoState, ProspectiveStep);
    if (*ppCheck == nullptr || 
        ProspectiveStep == (pUndoState->HeadIdx)+1 ) {
            return false;
    }
    else {
        return true;
    }
}

static b32
ShouldPushUndoStep(undo_step LastFrameState, undo_step ThisFrameState)
{
    for (int TileIdx = 0; TileIdx < TILES_IN_WORLD_Y * TILES_IN_WORLD_X; ++TileIdx) {
        if (LastFrameState.TileValues[TileIdx] != ThisFrameState.TileValues[TileIdx]) {
            return true;
        }
    }
    return false;
}

static undo_step *
UndoRedoAndGetTheStep(undo_state *pUndoState, int NewUndoStepIdx)
{
    pUndoState->CurrIdx = NewUndoStepIdx;
    return pUndoState->UndoStepsArr[NewUndoStepIdx];
}

static void
UpdateTileMapAfterEditorExit(tile_map *pTileMap, undo_step UndoStepToCopy)
{
    memcpy(pTileMap->TileValues, UndoStepToCopy.TileValues, TILES_IN_WORLD_NUM_BYTES);
}

#pragma once
#include "tufty.h"

static void
DrawSimpleRect(bitmap *pDestBitmap,
               v2 Min, v2 Max,
               f32 R, f32 G, f32 B, f32 Alpha = 0.5f,
               b32 Outline = false)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    if (MinY < 0)
    {
        MinY = 0;
    }
    if (MaxY > pDestBitmap->Height)
    {
        MaxY = pDestBitmap->Height;
    }
    if (MinX < 0)
    {
        MinX = 0;
    }
    if (MaxX > pDestBitmap->Width)
    {
        MaxX = pDestBitmap->Width;
    }

    u32 Color = ((RoundF32ToU32(R * 255.0f) << 16) |
                 (RoundF32ToU32(G * 255.0f) << 8)  |
                 (RoundF32ToU32(B * 255.0f) << 0));

    u8 *Row = (u8 *)pDestBitmap->Pixels + (MinY * pDestBitmap->Pitch) + (MinX * pDestBitmap->BytesPerPixel);
    for (int Y = MinY;
         Y < MaxY;
         ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for (int X = MinX;
             X < MaxX;
             ++X)
        {
            *Pixel++ = Color;
        }
        Row += pDestBitmap->Pitch;
    }
}

static void
DrawSpecialRect(bitmap *pDestBitmap,
                v2 Min, v2 Max,
                color RectColor, color OutlineColor)
{
    s32 MinX = RoundF32ToS32(Min.X);
    s32 MinY = RoundF32ToS32(Min.Y);
    s32 MaxX = RoundF32ToS32(Max.X);
    s32 MaxY = RoundF32ToS32(Max.Y);

    b32 HasOutline = ((OutlineColor.R > 0) || (OutlineColor.G > 0) || (OutlineColor.B > 0) || (OutlineColor.A > 0));

    if (MinY < 0) {
        MinY = 0;
    }
    if (MaxY > pDestBitmap->Height) {
        MaxY = pDestBitmap->Height;
    }
    if (MinX < 0) {
        MinX = 0;
    }
    if (MaxX > pDestBitmap->Width) {
        MaxX = pDestBitmap->Width;
    }

    u32 RectBlue = RoundF32ToU32(RectColor.B * 255.0f);
    u32 RectGreen = RoundF32ToU32(RectColor.G * 255.0f);
    u32 RectRed = RoundF32ToU32(RectColor.R * 255.0f);

    u32 PackedOutlineColor = 0;
    if (HasOutline) {
        PackedOutlineColor = ( (RoundF32ToU32(OutlineColor.R * 255.0f) << 16) |
                               (RoundF32ToU32(OutlineColor.G * 255.0f) << 8 ) | 
                               (RoundF32ToU32(OutlineColor.B * 255.0f) << 0 ) );
    }

    u8 *pRow = pDestBitmap->Pixels + (MinY * pDestBitmap->Pitch) + (MinX * BPP);
    for (int Y = MinY; Y < MaxY; ++Y) {
        u8 *pPixel = pRow;
        for (int X = MinX; X < MaxX; ++X) {
            if ( ((X == MinX) && HasOutline)   || ((Y == MinY) && HasOutline)   ||
                 ((X == MaxX-1) && HasOutline) || ((Y == MaxY-1) && HasOutline) ) {
                    *(u32 *)pPixel = PackedOutlineColor;
            }
            else {
                // Blue
                pPixel[0] = LerpS32(pPixel[0], RectBlue, RectColor.A);

                // Green
                pPixel[1] = LerpS32(pPixel[1], RectGreen, RectColor.A);

                // Red
                pPixel[2] = LerpS32(pPixel[2], RectRed, RectColor.A);

            }
            pPixel += sizeof(u32);
        }
        pRow += pDestBitmap->Pitch;
    }
}

static void
DEBUGDrawText(bitmap *pDestBitmap,
              f32 X, f32 Y,
              char *StringText, arena *DebugTextArena,
              color Color,
              f32 Scale = 3.5f)
{
    buffer QuadBuf = {};
    QuadBuf.Size = 300 * strlen(StringText);
    QuadBuf.Data = PushArray(DebugTextArena, u8, QuadBuf.Size);
    int NumQuads = stb_easy_font_print(0, 0, StringText, NULL, QuadBuf.Data, QuadBuf.Size);
    for (int QuadIdx = 0; QuadIdx < NumQuads; ++QuadIdx) {
        quad *ThisQuad = (quad *)(QuadBuf.Data + QuadIdx * sizeof(quad));
        v2 Min = {(X + ThisQuad->TopLeft.X*Scale), (Y + ThisQuad->TopLeft.Y*Scale)};
        v2 Max = {(X + ThisQuad->BottomRight.X*Scale), (Y + ThisQuad->BottomRight.Y*Scale)};
        if (Color.A == 1.0f) {
            DrawSimpleRect(pDestBitmap, Min, Max, Color.R, Color.G, Color.B);
        }
        else {
            DrawSpecialRect(pDestBitmap, Min, Max, Color, color{0, 0, 0, 0});
        }
    }
}

static void
PrintCameraCoords(bitmap *pDestBitmap, v2 CameraPos, arena *pDebugTextArena)
{
    char Temp[512];
    snprintf(Temp, sizeof(Temp), "Camera X = %f\nCamera Y = %f", CameraPos.X, CameraPos.Y);
    DEBUGDrawText(pDestBitmap, (pDestBitmap->Width * 0.1f), 30, Temp, pDebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
}

static void
DEBUGPrintFps(bitmap *pDestBitmap, f32 NewFpsReading, arena *pDebugTextArena)
{
#define FPS_SNAPS 30
    static f32 FpsSnaps[FPS_SNAPS] = {};
    static int FpsPrintCounter = 0;
    f32 FpsAvg = 0;
    FpsSnaps[FpsPrintCounter] = NewFpsReading;
    ++FpsPrintCounter;
    for (int SnapIdx = 0;
         SnapIdx < FPS_SNAPS;
         ++SnapIdx)
    {
        FpsAvg += FpsSnaps[SnapIdx];
    }
    FpsAvg /= FPS_SNAPS;

    if (FpsPrintCounter == FPS_SNAPS)
    {
        FpsPrintCounter = 0;
    }
    char Temp[256];
    snprintf(Temp, 256, "%d FPS", RoundF32ToS32(FpsAvg));
    DEBUGDrawText(pDestBitmap, (pDestBitmap->Width * 0.9f), 30, Temp, pDebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
#undef FPS_SNAPS
}

// static void
// DEBUGPrintRowsCols(bitmap *Backbuf, arena *DebugTextArena, tile_map *TileMap)
// {
//     int OffsetFromGridX = 15;
//     int OffsetFromGridY = 15;
//     // 0, 0
//     DEBUGDrawText(Backbuf, OffsetFromGridX, OffsetFromGridY, "0", DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
//
//     // Cols
//     for (int Col = 1;
//          Col < TileMap->NumCols;
//          ++Col)
//     {
//         int PrintX = Col * TileMap->TileSideInPixels + OffsetFromGridX; 
//         char Temp[3];
//         snprintf(Temp, 3, "%d", Col);
//         DEBUGDrawText(Backbuf, PrintX, OffsetFromGridY, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
//     }
//
//     // Rows
//     for (int Row = 1;
//          Row < TileMap->NumRows;
//          ++Row)
//     {
//         int PrintY = Row * TileMap->TileSideInPixels + OffsetFromGridY; 
//         char Temp[3];
//         snprintf(Temp, 3, "%d", Row);
//         DEBUGDrawText(Backbuf, OffsetFromGridX, PrintY, Temp, DebugTextArena, color{0.9f, 0.2f, 0.5f, 1.0f});
//     }
// }
//
static void
DrawFilepathTooltip(bitmap *pDestBitmap, arena *pDebugTextArena, 
                    v2 MouseCoords, meta_bitmap *pHoveredBitmap)
{
    f32 TooltipWidth = ((f32)strlen(pHoveredBitmap->Filepath)) * 11.2f;
    v2 TooltipMin = MouseCoords + v2{20.0f, -30.0f};
    if (TooltipMin.X + TooltipWidth >= pDestBitmap->Width) {
        TooltipMin.X -= TooltipWidth;
    }
    v2 TooltipMax = TooltipMin + v2{TooltipWidth, 30.0f};
    DrawSimpleRect(pDestBitmap, TooltipMin, TooltipMax, 1, 1, 0.88f, 0.75f);
    DEBUGDrawText(pDestBitmap, TooltipMin.X + 5.0f, TooltipMin.Y + 6.0f, 
                  pHoveredBitmap->Filepath, pDebugTextArena, color{0, 0, 0, 1}, 2.0f);
}

static void
DEBUGDrawFadingText(bitmap *pDestBitmap, fading_text *pFadingText, 
                    arena *pDebugTextArena, color Color = {1, 1, 1, 1})
{
    if (pFadingText->FrameCount >= pFadingText->FrameDuration) {
        return;
    }
    else {
        f32 InverseOpacity = (f32)pFadingText->FrameCount / (f32)pFadingText->FrameDuration;
        Color = {Color.R, Color.G, Color.B, 1 - InverseOpacity};
        DEBUGDrawText(pDestBitmap, pFadingText->X, pFadingText->Y, pFadingText->Text, pDebugTextArena, Color);
        ++pFadingText->FrameCount;
    }
}

static void
UpdateCursorStateHoveredFrames(cursor_state *pCursorState)
{
    pCursorState->LastFrameCoords = pCursorState->ThisFrameCoords;
    pCursorState->ThisFrameCoords = v2{(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    if (pCursorState->ThisFrameCoords == pCursorState->LastFrameCoords) {
        ++pCursorState->HoveredFrames;
    }
    else {
        pCursorState->HoveredFrames = 0;
    }
}


static void
DrawPlayerEditor(bitmap *pDestBitmap, game_state *pGameState, arena *pTransArena)
{
    // editor_state *pEditorState = &pGameState->EditorState;
    player *pPlayer = &pGameState->Player;
    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pDestBitmap->Width * 0.5f), 0};
    v2 BrowserMax = {(f32)(pDestBitmap->Width), (f32)(pDestBitmap->Height)};
    b32 BagelActive = false;

    // Get an array of menu_tile structs accommodating the maximum number of player bitmaps the game supports.
    //      Note that this is different than the length of the actual arr_meta_bitmaps.Data array for
    //      player bitmaps because that array contains the special nil bitmap at the front
    menu_bitmap *pMenuBitmaps = PushArray(pTransArena, menu_bitmap, 4 * (MAX_FACING_BITMAPS));

    DrawSpecialRect(pDestBitmap, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{0, 0, 0, 0});

    DEBUGDrawText(pDestBitmap, BrowserMin.X + 300, 60, "Player Bitmaps Menu", pTransArena, color{1, 1, 1, 1});

    char *Dirs[] = {"East", "North", "West", "South"};
    menu_bitmap *pMenuHovered = nullptr;
    f32 VerticalSpaceBetweenSections = 220;
    v2 HeaderTextStart = {BrowserMin.X + 30, 120};
    f32 CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    menu_bitmap *pMenuBitmapsCursor = pMenuBitmaps;
    for (int NthFace = 0; NthFace < 4; ++NthFace) {
        arr_meta_bitmap *pMetaBitmaps = &pPlayer->FacingBitmaps[NthFace].MetaBitmaps;
        char Temp[STRING_LEN];
        snprintf(Temp, STRING_LEN, "Facing %s", Dirs[NthFace]);
        DEBUGDrawText(pDestBitmap, HeaderTextStart.X, HeaderTextStart.Y, Temp, pTransArena, color{0.9, 0.9, 0.9, 1}, 2.4);

        f32 XDrawCoord = HeaderTextStart.X;
        for (int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
            meta_bitmap *pIt = pMetaBitmaps->Get(Slot);

            if (pIt->IsEmpty()) { continue; }

            pMenuBitmapsCursor->FacingIdx = NthFace;
            pMenuBitmapsCursor->Slot = Slot;
            if (pPlayer->FacingBitmaps[NthFace].DrawThis == Slot) {
                pMenuBitmapsCursor->DrawThis = true;
            }

            f32 Width, Height;
            if (pIt->IsMissing()) {
                BagelActive = true;
                Width = pGlobalBagel->Bitmap.Width;
                Height = pGlobalBagel->Bitmap.Height;
            }
            else {
                Width = pIt->Bitmap.Width;
                Height = pIt->Bitmap.Height;
            }

            pMenuBitmapsCursor->TileMin = v2{(f32)XDrawCoord, CenterAroundThisVerticalLine - Height * 0.5f};
            pMenuBitmapsCursor->TileMax = pMenuBitmapsCursor->TileMin + v2{Width, Height};

            if (IsInRect(MouseCoords, pMenuBitmapsCursor->TileMin, pMenuBitmapsCursor->TileMax)) {
                pMenuHovered = pMenuBitmapsCursor;
            }

            XDrawCoord = pMenuBitmapsCursor->TileMax.X + 30.0f;
            ++pMenuBitmapsCursor;
        }
        HeaderTextStart += v2{0, VerticalSpaceBetweenSections};
        CenterAroundThisVerticalLine = HeaderTextStart.Y + VerticalSpaceBetweenSections * 0.5f;
    }

    int NumMenuBitmaps = pMenuBitmapsCursor - pMenuBitmaps;
    pMenuBitmapsCursor = pMenuBitmaps;
    for (int NthBitmap = 0; NthBitmap < NumMenuBitmaps; ++NthBitmap, ++pMenuBitmapsCursor) {
        meta_bitmap *pToDraw = 
            GetPlayerMetaBitmapPointer(pPlayer, pMenuBitmapsCursor->FacingIdx, pMenuBitmapsCursor->Slot);
        bitmap *pBitmap;
        if (pToDraw->IsMissing()) {
            pBitmap = &pGlobalBagel->Bitmap;
        }
        else {
            pBitmap = &pToDraw->Bitmap;
        }
        ScaleAndBlitBitmap(pDestBitmap, pMenuBitmapsCursor->TileMin, pMenuBitmapsCursor->TileMax, pBitmap);
        v2 OutlineMin = {pMenuBitmapsCursor->TileMin.X-1, pMenuBitmapsCursor->TileMin.Y-1};
        v2 OutlineMax = {pMenuBitmapsCursor->TileMax.X+1, pMenuBitmapsCursor->TileMax.Y+1};
        if (pMenuBitmapsCursor->DrawThis) {
            DrawSpecialRect(pDestBitmap, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0, 0.6, 0.5, 0.8});
        }
        else {
            DrawSpecialRect(pDestBitmap, OutlineMin, OutlineMax, color{0, 0, 0, 0}, color{0.1, 0.1, 0.1, 0.8});        
        }
    }

    cursor_state *pCursorState = &pGameState->EditorState.CursorState;
    UpdateCursorStateHoveredFrames(pCursorState);
    // Highlight hovered tile and draw tooltip filepath text
    if (pMenuHovered != nullptr) {
        v2 OutlineMin = {pMenuHovered->TileMin.X-1, pMenuHovered->TileMin.Y-1};
        v2 OutlineMax = {pMenuHovered->TileMax.X+1, pMenuHovered->TileMax.Y+1};
        DrawSpecialRect(pDestBitmap, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
        if (pCursorState->HoveredFrames >= 30) {
            meta_bitmap *pHoveredBitmap = 
                GetPlayerMetaBitmapPointer(pPlayer, pMenuHovered->FacingIdx, pMenuHovered->Slot);
            DrawFilepathTooltip(pDestBitmap, pTransArena, MouseCoords, pHoveredBitmap);
        }
    }

    /*********** UPDATE CURSOR STATE **************/


    // If ended up always go to idle
    if (pGlobalMouse->Primary.EndedDown == false) {
        pCursorState->PrimaryMode = IDLE;
    }

    // If the user clicked, always go to consumed
    else if (pCursorState->PrimaryMode == IDLE &&
             pGlobalMouse->Primary.EndedDown &&
             pGlobalMouse->Primary.HalfTransitionCount == 1) {

            pCursorState->PrimaryMode = CONSUMED;

            // If the user clicked on an actual bmp
            if (pMenuHovered != nullptr) {
                if (BagelActive) {
                    // If the tile they clicked on is valid, replace
                    //      current held tile with clicked bmp
                    meta_bitmap *pHoveredBitmap = 
                        GetPlayerMetaBitmapPointer(pPlayer, pMenuHovered->FacingIdx, pMenuHovered->Slot);
                    if (pHoveredBitmap->IsPresent()) {
                        ChangeHeldPlayerMetaBitmap(pPlayer, pCursorState, pMenuHovered->FacingIdx, pMenuHovered->Slot);
                    }
                    // If they clicked a bagel, a bmp was missing. If they are
                    //      holding a valid bmp, replace the clicked
                    //      bmp with the held one, and set held bmp to nil
                    if (pHoveredBitmap->IsMissing() && pCursorState->HeldFacingIdx == pMenuHovered->FacingIdx) {
                        // NOTE(aaron): Dead code(?):
                            // meta_bitmap *pHeldBitmap =
                            //     GetPlayerMetaBitmapPointer(pPlayer, pCursorState->HeldFacingIdx, pCursorState->HeldSlot);
                        arr_meta_bitmap *pHoveredContainingArr = GetPlayerArrMetaBitmapPtr(pPlayer, pMenuHovered->FacingIdx);
                        assert(pHoveredContainingArr != nullptr);
                        pHoveredContainingArr->ReplaceSlotAWithSlotB(pMenuHovered->Slot, pCursorState->HeldSlot);
                        SetHeldBitmapToNil(pCursorState);
                    }
                }
                // Else if no bagel is active, all tiles are present, and clicking one
                //      changes the player's facing direction in game and the DrawThis
                //      index to match the clicked bmp
                else {
                    pPlayer->FacingBitmaps[pMenuHovered->FacingIdx].DrawThis = pMenuHovered->Slot;
                    pPlayer->IsFacing = pMenuHovered->FacingIdx;
                }
            }
    }

    if (pGlobalMouse->Secondary.EndedDown == false) {
        pCursorState->SecondaryMode = IDLE;
    }
    // Right clicking while holding a player bmp drops it
    else if (pCursorState->SecondaryMode == IDLE &&
             pGlobalMouse->Secondary.EndedDown &&
             pGlobalMouse->Secondary.HalfTransitionCount == 1) {
            SetHeldBitmapToNil(pCursorState);
    }

    // Draw held bmp
    arr_meta_bitmap *pHeldContainingArr = GetPlayerArrMetaBitmapPtr(pPlayer, pCursorState->HeldFacingIdx);
    if (pHeldContainingArr != nullptr) {
        meta_bitmap *pIt = pHeldContainingArr->Get(pCursorState->HeldSlot);
        if (pIt->IsPresent()) {
            bitmap *pBitmap = &pIt->Bitmap;
            DrawHeldTile(pDestBitmap, MouseCoords, pBitmap);
        }
    }

    // Draw player to left of browser if player editor is active, selecting
    //      whichever bmp matches the DrawThis index for the current facing
    facing_bitmaps *pCurrentFacing = pPlayer->FacingBitmaps + pPlayer->IsFacing;
    arr_meta_bitmap *pArrForCurrentFacing = GetPlayerArrMetaBitmapPtr(pPlayer, pPlayer->IsFacing);
    if (pArrForCurrentFacing->CountNonEmptySlots() > 0)
    {
        meta_bitmap *pToDraw = pArrForCurrentFacing->Get(pCurrentFacing->DrawThis);
        if (pToDraw->IsPresent()) {
            bitmap *pBitmap = &pToDraw->Bitmap;

            f32 PlayerWidth = pBitmap->Width;
            f32 PlayerHeight = pBitmap->Height;

            v2 PlayerMin = v2{(f32)pDestBitmap->Width * 0.25f - PlayerWidth * 0.5f, 
                (f32)pDestBitmap->Height * 0.5f - PlayerHeight * 0.5f};
            v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};
            ScaleAndBlitBitmap(pDestBitmap, PlayerMin, PlayerMax, pBitmap);
        }
    }
}

static undo_step
DrawTileEditor(bitmap *pBackbuf, game_state *pGameState, arena *pTransArena)
{
    editor_state *pEditorState = &pGameState->EditorState;
    undo_state *pUndoState = &pEditorState->UndoState;
    tile_map *pTileMap = &pGameState->TileMap;
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;

    ResetArena(&pUndoState->SingleFrameUndoArena);

    undo_step *pLastFrameState = PushNewUndoStep(&pUndoState->SingleFrameUndoArena);
    WriteUndoStep(pLastFrameState, pTileMap->TileValues);
    undo_step *pThisFrameState = PushNewUndoStep(&pUndoState->SingleFrameUndoArena);
    WriteUndoStep(pThisFrameState, pTileMap->TileValues);

    v2 MouseCoords = {(f32)pGlobalMouse->X, (f32)pGlobalMouse->Y};
    v2 BrowserMin = {(f32)(pBackbuf->Width * 0.8f), 0};
    v2 BrowserMax = {(f32)(pBackbuf->Width), (f32)(pBackbuf->Height)};

    menu_bitmap *pMenuTiles = PushArray(pTransArena, menu_bitmap, MAX_TILE_TYPES);

    // Panel
    DrawSpecialRect(pBackbuf, BrowserMin, BrowserMax, color{0.5f, 0.5f, 0.5f, 0.85f}, color{});

    DEBUGDrawText(pBackbuf, BrowserMin.X + 120, 80, "Tile Menu", pTransArena, color{1, 1, 1, 1}, 3.5);

    /***********MAKE ARRAY OF TILES TO DRAW**************/
    f32 Y = 160;
    f32 InnerPadding = 40;
    f32 OuterPadding = 56;
    f32 StartX = BrowserMin.X + OuterPadding;
    f32 X = StartX;
    int TilesDrawnInThisRow = 0;
    menu_bitmap *pMenuTilesCursor = pMenuTiles;
    menu_bitmap *pMenuHovered = nullptr; 
    for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTileTypes->Get(Slot);
        if (pIt->IsEmpty()) { continue; }

        pMenuTilesCursor->Slot = Slot;
        pMenuTilesCursor->TileMin = {X, Y};
        pMenuTilesCursor->TileMax = pMenuTilesCursor->TileMin + v2{pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};

        if (IsInRect(MouseCoords, pMenuTilesCursor->TileMin, pMenuTilesCursor->TileMax)) {
            pMenuHovered = pMenuTilesCursor;
        }

        ++TilesDrawnInThisRow;
        if (TilesDrawnInThisRow == 3)
        {
            TilesDrawnInThisRow = 0;
            X = StartX;
            Y += pTileMap->TileSideInPixels + InnerPadding;
        }
        else
        {
            X += pTileMap->TileSideInPixels + InnerPadding;
        }

        ++pMenuTilesCursor;
    }

    /*********** DRAW THE MENU TILES AND DRAW BOXES AROUND THEM **************/
    int NumToDraw = pMenuTilesCursor - pMenuTiles;
    pMenuTilesCursor = pMenuTiles;
    for (int NthMenuTile = 0; NthMenuTile < NumToDraw; ++NthMenuTile, ++pMenuTilesCursor) {
        meta_bitmap *pToDraw = GetTileTypePointer(pTileMap, pMenuTilesCursor->Slot);
        bitmap *pBitmap = 0;
        if (pToDraw->IsMissing()) {
            pBitmap = &pGlobalBagel->Bitmap;
        }
        else {
            pBitmap = &pToDraw->Bitmap;
        }
        ScaleAndBlitBitmap(pBackbuf, pMenuTilesCursor->TileMin, pMenuTilesCursor->TileMax, pBitmap);
        v2 OutlineMin = {pMenuTilesCursor->TileMin.X-1, pMenuTilesCursor->TileMin.Y-1};
        v2 OutlineMax = {pMenuTilesCursor->TileMax.X+1, pMenuTilesCursor->TileMax.Y+1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{}, color{0, 0, 0, 1});
    }

    cursor_state *pCursorState = &pGameState->EditorState.CursorState;
    UpdateCursorStateHoveredFrames(pCursorState);
    if (pMenuHovered != nullptr) {
        v2 OutlineMin = {pMenuHovered->TileMin.X-1, pMenuHovered->TileMin.Y-1};
        v2 OutlineMax = {pMenuHovered->TileMax.X+1, pMenuHovered->TileMax.Y+1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
        if (pCursorState->HoveredFrames >= 30) {
            meta_bitmap *pHoveredBitmap = GetTileTypePointer(pTileMap, pMenuHovered->Slot);
            DrawFilepathTooltip(pBackbuf, pTransArena, MouseCoords, pHoveredBitmap);
        }
    }

    /*********** UPDATE CURSOR STATE **************/
    b32 InBackbuf = IsInRect(MouseCoords, v2{0,0}, v2{(f32)pBackbuf->Width, (f32)pBackbuf->Height});
    b32 InBrowser = IsInRect(MouseCoords, BrowserMin, BrowserMax);

    // Primary
    if (pGlobalMouse->Primary.EndedDown == false) {
        pCursorState->PrimaryMode = IDLE;
    }

    else if (pCursorState->PrimaryMode == IDLE &&
             pGlobalMouse->Primary.EndedDown &&
             pGlobalMouse->Primary.HalfTransitionCount == 1) {

            pCursorState->PrimaryMode = CONSUMED;
            // If the user clicked on a tile in the browser, pMenuHovered will be set
            if (pMenuHovered != nullptr) {
                // If they clicked the bagel, a tile was missing.
                //      If they are holding a valid tile, replace
                //      the missing tile with the held one, and
                //      set held tile to nil.
                meta_bitmap *pHoveredBitmap = GetTileTypePointer(pTileMap, pMenuHovered->Slot);
                meta_bitmap *pHeldBitmap = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
                if(pHoveredBitmap->IsMissing() && pHeldBitmap->IsPresent()) {
                    pTileMap->TileTypes.ReplaceSlotAWithSlotB(pMenuHovered->Slot, pCursorState->HeldSlot);
                    SearchAndReplaceTileValue(pTileMap, pMenuHovered->Slot, pCursorState->HeldSlot);
                    SetHeldBitmapToNil(pCursorState);
                }
                // Else if the tile they clicked on is valid, replace current held tile
                //      with clicked tile
                else if (pHoveredBitmap->IsPresent()) {
                    ChangeHeldTileType(pTileMap, pCursorState, pMenuHovered->Slot);
                }
            }

            // Else if they clicked in the tilemap
            else if (InBackbuf && !InBrowser) {
                // Figure out which tile they clicked on
                v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
                // NOTE: We are getting these from the temp this frame state struct instead of the tilemap. don't forget!!!
                int HoveredIdx = Get1DTileCoordFrom2DCoord(TILES_IN_WORLD_X, TileAsV2);
                s32 TileValue = GetTileValueFrom1DCoord(pThisFrameState->TileValues, HoveredIdx);
                // If they clicked on an empty tile
                if (TileValue == 0) {
                    meta_bitmap *pHeldTile = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
                    // And they are holding a valid tile
                    if (pHeldTile->IsPresent()) {
                        // Start painting, unless user is already secondary painting
                        if (pCursorState->SecondaryMode != PAINTING) {
                            pCursorState->PrimaryMode = PAINTING;
                        }
                    }
                }
                // Else, they clicked on a valid tile. Pick it up.
                else {
                    ChangeHeldTileType(pTileMap, pCursorState, TileValue);
                }
            }
    }

    // Secondary cursor state
    if (pGlobalMouse->Secondary.EndedDown == false) {
        pCursorState->SecondaryMode = IDLE;
    }
    if (pCursorState->SecondaryMode==IDLE &&
        pGlobalMouse->Secondary.EndedDown &&
        pGlobalMouse->Secondary.HalfTransitionCount==1) {
            pCursorState->SecondaryMode = CONSUMED;
            b32 InBackbuf = IsInRect(MouseCoords, v2{0,0}, v2{(f32)pBackbuf->Width, (f32)pBackbuf->Height});
            b32 InBrowser = IsInRect(MouseCoords, BrowserMin, BrowserMax);
            meta_bitmap *pHeldTileType = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
            if (pHeldTileType->IsPresent()) {
                SetHeldBitmapToNil(pCursorState);
            }
            else {
                if (InBackbuf && !InBrowser) {
                    if (pCursorState->PrimaryMode != PAINTING) {
                        pCursorState->SecondaryMode = PAINTING;
                    }
                }
            }
    }

    // Drawing

    if (InBackbuf == true && InBrowser == false) {
        v2 TileCoords = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        v2 TileMin = {TileCoords.X *pTileMap->TileSideInPixels, TileCoords.Y *pTileMap->TileSideInPixels};
        v2 TileMax = TileMin + v2{pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};
        v2 OutlineMin = {TileMin.X+1, TileMin.Y+1};
        v2 OutlineMax = {TileMax.X-1, TileMax.Y-1};
        DrawSpecialRect(pBackbuf, OutlineMin, OutlineMax, color{1, 1, 1, 0.5f}, color{0, 0, 0, 0});
    }

    meta_bitmap *pHeldTile = GetTileTypePointer(pTileMap, pCursorState->HeldSlot);
    if (pHeldTile->IsPresent())
    {
        DrawHeldTile(pBackbuf, MouseCoords, &pHeldTile->Bitmap);
    }
    if (pCursorState->PrimaryMode == PAINTING && InBrowser == false && pHeldTile->IsPresent()) {
        v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        int HoveredIdx = Get1DTileCoordFrom2DCoord(TILES_IN_WORLD_X, TileAsV2);
        SetTileValue(pThisFrameState->TileValues, HoveredIdx, pCursorState->HeldSlot);
    }
    else if (pCursorState->SecondaryMode == PAINTING && InBrowser == false) {
        v2 TileAsV2 = GetTileCoordsFromMouseCoords(pTileMap->TileSideInPixels);
        int HoveredIdx = Get1DTileCoordFrom2DCoord(TILES_IN_WORLD_X, TileAsV2);
        SetTileValue(pThisFrameState->TileValues, HoveredIdx, 0);
    }

    // static fading_text PushedText = {"Pushed", 1300.0f, pBackbuf->Height - 360.0f, 100, 100};
    // DEBUGDrawFadingText(pBackbuf, &PushedText, pDebugTextArena);
    static fading_text UndoText = {"Undo", 1300.0f, pBackbuf->Height - 210.0f, 100, 100};
    DEBUGDrawFadingText(pBackbuf, &UndoText, pTransArena);
    static fading_text RedoText = {"Redo", 1300.0f, pBackbuf->Height - 60.0f, 100, 100};
    DEBUGDrawFadingText(pBackbuf, &RedoText, pTransArena);

    if (ShouldPushUndoStep(*pLastFrameState, *pThisFrameState)) {
        IncrementUndoHead(pUndoState, *pThisFrameState);
        // PushedText.FrameCount = 0;
    }
    else if (GlobalDevKeys->Ctrl.EndedDown && 
             GlobalDevKeys->Y.EndedDown && 
             GlobalDevKeys->Y.HalfTransitionCount == 1) {
            int ProspectiveStep = GetCanonUndoIndex(pUndoState->CurrIdx, 1);
            if (IsRedoStepValid(pUndoState, ProspectiveStep)) {
                int NewStep = ProspectiveStep;
                pThisFrameState = UndoRedoAndGetTheStep(pUndoState, NewStep);
                RedoText.FrameCount = 0;
            }
    }
    else if (GlobalDevKeys->Ctrl.EndedDown && 
             GlobalDevKeys->Z.EndedDown && 
             GlobalDevKeys->Z.HalfTransitionCount == 1) {
            int ProspectiveStep = GetCanonUndoIndex(pUndoState->CurrIdx, -1);
            if (IsUndoStepValid(pUndoState, ProspectiveStep)) {
                int NewStep = ProspectiveStep;
                pThisFrameState = UndoRedoAndGetTheStep(pUndoState, NewStep);
                UndoText.FrameCount = 0;
            }
    }

    if (pGameState->EditorState.PrintRowsCols) {
        // DEBUGPrintRowsCols(pBackbuf, pTransArena, pTileMap);
    }


// This draws all the undo state indices on the screen, marks the head index with an H, and
//      marks the current index with an arrow
#if 0
    X = 30.0f;
    Y = pBackbuf->Height - 30.0f;
    char Temp[STRING_LEN];
    for (int Step = 0; Step < MAX_UNDO_STEPS; ++Step) {
        snprintf(Temp, STRING_LEN, "%s %d %s", Step == pUndoState->HeadIdx ? "H" : "", Step, Step == pUndoState->CurrIdx ? "<-" : "");
        DEBUGDrawText(pBackbuf, X, Y, Temp, pDebugTextArena, color{1, 1, 1, 1});
        Y -= 30.0f;
        if (Y <= 30.0f) {
            Y = pBackbuf->Height - 30.0f;
            X += 200.0f;
        }
    }
#endif

    return *pThisFrameState;
}

static void
InitializeCursorState(cursor_state *pCursorState) 
{
    *pCursorState = {};
    SetHeldBitmapToNil(pCursorState);
}

// static animation *
// LoadAnimation(arena *pDestArena, char *Filename)
// {
//
//
// }


#if 0
static bitmap *
GetCurrentAnimationCel(facing_animations *pFacingAnimations, f32 dtForFrame)
{
    bitmap *pResult = 0;


    f32 MsPerGameFrame = dtForFrame * 1000.0f;

    animation *CurrAnimation = &pFacingAnimations->Animations[pFacingAnimations->CurrAnimationIdx];
    int FramesPerCel = RoundF32ToS32(MsPerAnimationCel / MsPerGameFrame);
    int CelIdxUnbounded = pFacingAnimations->FrameCounter / FramesPerCel;
    int NumCelsForCurrAnimation = CurrAnimation->NumCels;
    int CellIdx = (CelIdxUnbounded % NumCelsForCurrAnimation) + 1;

    assert(CurrAnimation->Cels[CellIdx].Pixels != nullptr);
    pResult = &CurrAnimation->Cels[CellIdx];
    
    return pResult;

    // eg FrameCounter = 0;
    // CelIdxUnbounded = FrameCounter / FramesPerCel = 0;
    // CelIdx = CelIdxUnbounded % WhichAnim.NumCels = 0 % 6 = 0;
    //
    // eg Framecounter = 4;
    // CelIdxUnbounded = FrameCounter / FramesPerCel = 4 / 6 = 0;
    // CellIdx = 0 % 6 = 0;
    //
    // eg FrameCounter = 7;
    // CelIdxUnbounded = FrameCounter / FramesPerCel = 7 / 6 = 1;
    // CellIdx = 1 % 6 = 1;
    //
    // eg FrameCounter = 23;
    // CelIdxUnbounded = FrameCounter / FramesPerCel = 23 / 6 = 3;
    // CellIdx = 3 % 6 = 3;
    //
    // eg FrameCounter = 150;
    // CellIdxUnbounded = FrameCounter / FramesPerCel = 150 / 6 = 25;
    // CellIdx = 25 % 6 = 4;
    //
}
#endif


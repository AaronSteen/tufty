#define _CRT_SECURE_NO_DEPRECATE

#include "tufty.h"
#define PROFILE_ANCHORS_BASE 2048
#define PROFILE_ANCHORS_LIMIT MAX_ANCHORS
#define PROFILER 1
#include "prof.h"

#define PLAYER_HEIGHT 64.0f
#define PLAYER_WIDTH 64.0f

// DEBUG_PLATFORM_GET_FILE_SIZE(name) u32 name(char *Filename)
// DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void *Memory)
// DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(char *Filename)
// DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(char *Filename, u33 MemorySize, void *Memory)
// DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) u64 name(char *Filename)
// DEBUG_PLATFORM_GET_DIR_WRITE_TIME(name) u64 name(char *SubDirName)
// DEBUG_PLATFORM_GET_LIST_OF_DIR_CONTENTS(name) void name(buffer *GamePackedFilenames, char *SubDirName, int *NumFilesFound)
//
// returns either the number of bytes read, or 0 if the file was missing, too big, or locked
// DEBUG_PLATFORM_READ_FILE_INTO(name) u32 name(char *Filename, u32 DestSize, void *Dest)
//
// IsSave = true if saving, false if loading
// #define DEBUG_PLATFORM_GET_FILE_PATH_FROM_DIALOG(name) int name(char *Dest, int DestSize, b32 IsSave)
//

// static facing_animation gWestAnimations;

// u64 static gFrameCounter;

static void
GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz)
{
    s16 ToneVolume = 3000;
    // NOTE(aaron): Dead code(?):
        // int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    s16 *SampleOut = SoundBuffer->Samples;
    for (int SampleIdx = 0;
        SampleIdx < SoundBuffer->SampleCount;
        ++SampleIdx)
    {
#if 0
        f32 SineValue = sinf(GameState->tSine);
        s16 SampleValue = (s16)SineVavlue * ToneVolume;
#else
        s16 SampleValue = 0;

        // To test with square wave:
        // s16 SampleValue = ((SampleIdx / (WavePeriod/2)) %2) ? ToneVolume : -ToneVolume;
#endif
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;
#if 0
        GameState->tSine += 2.0f*Pi32*1.0f/(f32)WavePeriod;

        if (GameState->tSine > 2.0f*Pi32)
        {
            GameState->tSine -= 2.0f*Pi32;
        }
#endif
    }
}

b32
IsPlayerTryingToMove(void)
{
    b32 Result = (GlobalKeyboardController->MoveLeft.EndedDown ||
                  GlobalKeyboardController->MoveUp.EndedDown ||
                  GlobalKeyboardController->MoveRight.EndedDown ||
                  GlobalKeyboardController->MoveDown.EndedDown);
    return Result;
}

// Resolution of backbuffer or framebuffer is 1920 x 1080

// #define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    game_state *pGameState = (game_state *)Memory->PermanentStorage;
    arena *pTransArena = (arena *)Memory->TransientStorage;

    // Convenience pointers
    tile_map *pTileMap = &pGameState->TileMap;
    player *pPlayer = &pGameState->Player;
    undo_state *pUndoState = &pGameState->EditorState.UndoState;

    pgSharedProfData = Memory->pgSharedProfData;
    GetFileSize = Memory->DEBUGPlatformGetFileSize;
    FreeFileMemory = Memory->DEBUGPlatformFreeFileMemory;
    ReadEntireFile = Memory->DEBUGPlatformReadEntireFile;
    WriteEntireFile = Memory->DEBUGPlatformWriteEntireFile;
    GetFileWriteTime = Memory->DEBUGPlatformGetFileWriteTime;
    GetDirWriteTime = Memory->DEBUGPlatformGetDirWriteTime;
    GetListOfDirContents = Memory->DEBUGPlatformGetListOfDirContents;
    ReadFileInto = Memory->DEBUGPlatformReadFileInto;
    GetFilepathFromDialog = Memory->DEBUGPlatformGetFilepathFromDialog;
    DebugOutput = Memory->DebugOutput;
    ReadCpuTimer = Memory->DEBUGReadCpuTimer;

    // Global input pointers
    GlobalKeyboardController = GetController(Input, 0);
    GlobalDevKeys = &Input->DevKeys;
    pGlobalMouse = &Input->Mouse;

    // @init
    if (!Memory->IsInitialized)
    {
        // Seed random number generator
        pGameState->RandomSeries = SeedRandomSeries(Input->CpuTimerReading);

// ARENAS
        // World
        arena *pWorldArena = &pGameState->WorldArena;
        InitializeArena(pWorldArena, (u8 *)Memory->PermanentStorage + sizeof(game_state), Memory->PermanentStorageSize - sizeof(game_state));
        InitSubArena(&pGameState->FailBitmapsArena, pWorldArena, Megabytes(1));
        

        InitSubArena(&pGameState->TileMap.TileTypes.BitmapsArena, pWorldArena, Megabytes(4));
        int BytesForAllTilesInWorld = BYTE_COUNT_FOR_ALL_TILE_VALUES; // 221 * 393 = 86853 tiles @ 4 bytes per = 347,412 bytes 
        // Round up to nearest kilobyte:
        int KbForAllTilesInWorld = BytesForAllTilesInWorld / Kilobytes(1);
        ++KbForAllTilesInWorld;
        InitSubArena(&pGameState->TileMap.TileValuesArena, pWorldArena, Kilobytes(KbForAllTilesInWorld));
        InitSubArena(&pGameState->EditorState.UndoState.UndoStepsArena, pWorldArena, Kilobytes(KbForAllTilesInWorld) * MAX_UNDO_STEPS);
        InitSubArena(&pGameState->EditorState.UndoState.SingleFrameUndoArena, pWorldArena, Kilobytes(KbForAllTilesInWorld) * 2);

        for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
            arena *pArena = &pPlayer->FacingBitmaps[FacingIdx].MetaBitmaps.BitmapsArena;
            InitSubArena(pArena, pWorldArena, Megabytes(1));
        }
        InitSubArena(&pPlayer->AnimationsArena, pWorldArena, Megabytes(1));

        // Transient
        InitializeArena(pTransArena, (u8 *)Memory->TransientStorage + sizeof(arena), Memory->TransientStorageSize - sizeof(arena));
        
// DATA STRUCTURES

// Tiles        
        // TileMap dimensions
        pTileMap->TileSideInPixels = 64.0f;
        pTileMap->TileSideInMeters = 1.4f;

        // Tile types, i.e., the tiles that populate the tiles menu in the editor
        arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
        pTileTypes->Data = PushArray(pWorldArena, meta_bitmap, MAX_TILE_TYPES+1);
        pTileTypes->Len = MAX_TILE_TYPES+1;
        pTileTypes->NextEmptySlot = 1;
        pTileMap->NumRows = CeilingF32ToS32((f32)Buffer->Height / pTileMap->TileSideInPixels);        
        pTileMap->NumCols = CeilingF32ToS32((f32)Buffer->Width / pTileMap->TileSideInPixels);        

        pTileMap->TileValues = PushArray(&pTileMap->TileValuesArena, s32, MAX_TILE_VALUES);

        pTileMap->ReloadState.WaitState = READY_TO_RELOAD;
        LoadTileBitmapsDir(pTileMap, pTransArena);

// Undo
        pUndoState->UndoStepsArr[0] = PushNewUndoStep(&pUndoState->UndoStepsArena);

// pPlayer
        for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
            facing_bitmaps *pFacingBitmaps = pPlayer->FacingBitmaps + FacingIdx;
            arr_meta_bitmap *pMetaBitmaps = &pFacingBitmaps->MetaBitmaps;
            pMetaBitmaps->Data = PushArray(&pMetaBitmaps->BitmapsArena, meta_bitmap, MAX_FACING_BITMAPS+1);
            pMetaBitmaps->Len = MAX_FACING_BITMAPS+1;
            pMetaBitmaps->NextEmptySlot = 1;
            pFacingBitmaps->ReloadState.WaitState = READY_TO_RELOAD;
            LoadPlayerBitmapsDir(pPlayer, pFacingBitmaps, pTransArena);
            if(pMetaBitmaps->Data[1].IsPresent()) {
                pFacingBitmaps->DrawThis = 1;
            }
        }
        for (int Facing = 0; Facing < 4; ++Facing) {
            pPlayer->FacingAnimationArray[Facing].pContainingArena = &pPlayer->AnimationsArena;
        }
        LoadAnimation(&pPlayer->FacingAnimationArray[EAST], "64x64faceright_walking.aseprite");
        LoadAnimation(&pPlayer->FacingAnimationArray[NORTH], "north.aseprite");
        LoadAnimation(&pPlayer->FacingAnimationArray[WEST], "64x64faceleft_walking.aseprite");
        LoadAnimation(&pPlayer->FacingAnimationArray[SOUTH], "64x64frontview_walking.aseprite");

        pPlayer->Pos.X = Buffer->Width / 2;
        pPlayer->Pos.Y = Buffer->Height / 2;
        pPlayer->IsFacing = WEST;
        ResetAnimationStateCelParams(&pPlayer->AnimationState);
        animation *pDefaultAnimation = GetAnimation(&pPlayer->FacingAnimationArray[pPlayer->IsFacing], 1);
        assert(IsAnimationSlotEmpty(pDefaultAnimation) == false);
        pPlayer->AnimationState.CurrAnimationIdx = 1;

// Bagel
        // TODO(AARON): ScaleAndBlitBitmap called with Bitmap == nullptr draw something other than bagel
        //      so we know when we called it with nullptr
        char *BagelFilepath = "bagel1.bmp";
        pGlobalBagel = PushStruct(&pGameState->FailBitmapsArena, meta_bitmap);
        snprintf(pGlobalBagel->Filepath, sizeof(pGlobalBagel->Filepath), "%s", BagelFilepath);
        mem_idx BagelSize = GetFileSize(pGlobalBagel->Filepath);
        if (BagelSize) {
            pGlobalBagel->Bitmap.Buffer.Size = BagelSize;
            pGlobalBagel->Bitmap.Buffer.Data = PushArray(&pGameState->FailBitmapsArena, u8, BagelSize);
        }
        ActuallyLoadBitmap(pGlobalBagel);
        
        Memory->IsInitialized = true;
    }
    
    // @Main

    // Reset the transient arena
    pTransArena->Used = 0;
    pPlayer->OldFacing = pPlayer->IsFacing;
    pPlayer->TryingToMove = IsPlayerTryingToMove();
    
    // Bitmap reloading notes:
    //      We have the following objectives:
    //          1. Be able to immediately add bitmaps to the game
    //          2. Be able to immediately remove bitmaps from the game
    //          3. Be able to make a change to a bitmap and have that change appear immediately in the game.
    //
    //      In each case, the simplest way to avoid bugs that result from trying to read a file while
    //          it's still being updated by the OS is to simply wait a few (we wait 3) frames after a change has been detected.
    //          Therefore, we store the write times for dirs and files of interest locally, and, once per frame,
    //          we compare these with the OS's write times (in GetUpdatedReloadState). If the times match, no update has been made.
    //          If the times differ, we store the new time and set a ReadyToReload variable to true.
    //          Then we reload after three frames have elapsed.
    u64 CheckTilesUpdateTime = GetDirWriteTime("tiles");
    pTileMap->ReloadState = GetUpdatedReloadState(CheckTilesUpdateTime, pTileMap->ReloadState);
    if (pTileMap->ReloadState.WaitState == READY_TO_RELOAD) {
        LoadTileBitmapsDir(pTileMap, pTransArena);
    }
    else {
        // If we're not planning to reload all bitmaps on the next frame, check each bitmap
        //      individually to see if that bitmap has been updated.
        arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
        for (int Slot = 0; Slot < pTileTypes->Len; ++Slot) {
            ReloadBitmapIfChanged(pTileTypes, Slot);
        }
    }

    // Do the same checks for the player bitmaps
    for (int FacingIdx = 0; FacingIdx < 4; ++FacingIdx) {
        facing_bitmaps *pThisFacing = &pPlayer->FacingBitmaps[FacingIdx];
        char ThisFacingBitmapsDirpath[STRING_LEN];
        WriteFacingBitmapsDirpathToBuffer(pPlayer, pThisFacing, ThisFacingBitmapsDirpath);
        u64 CheckUpdateTime = GetDirWriteTime(ThisFacingBitmapsDirpath);
        pThisFacing->ReloadState = GetUpdatedReloadState(CheckUpdateTime, pThisFacing->ReloadState);
        if (pThisFacing->ReloadState.WaitState == READY_TO_RELOAD) {
            LoadPlayerBitmapsDir(pPlayer, pThisFacing, pTransArena);
        }
        else {
            arr_meta_bitmap *pMetaBitmaps = &pThisFacing->MetaBitmaps;
            for(int Slot = 0; Slot < pMetaBitmaps->Len; ++Slot) {
                ReloadBitmapIfChanged(pMetaBitmaps, Slot);
            }
        }
    }

    // Controller
    for (int ControllerIdx = 0;
        ControllerIdx < ArrayCount(Input->Controllers);
        ++ControllerIdx)
    {
        game_controller_input *Controller = GetController(Input, ControllerIdx);
        if (Controller->IsAnalog) {

        }
        else {
            v2 Acceleration = {};
            if (Controller->MoveRight.EndedDown) {
                Acceleration.X += 1.0f;
                pPlayer->IsFacing = EAST;
            }
            if (Controller->MoveUp.EndedDown) {
                Acceleration.Y -= 1.0f;
                pPlayer->IsFacing = NORTH;
            }
            if (Controller->MoveLeft.EndedDown) {
                Acceleration.X -= 1.0f;
                pPlayer->IsFacing = WEST;
            }
            if (Controller->MoveDown.EndedDown && !Input->DevKeys.Ctrl.EndedDown) {
                Acceleration.Y += 1.0f;
                pPlayer->IsFacing = SOUTH;
            }
            if (pGameState->EditorState.WhichEditor == NO_EDITOR) {
                if ((Acceleration.X != 0) && (Acceleration.Y != 0)) {
                    Acceleration *= 0.707106781187f;
                }

                f32 PlayerSpeed = 20.0f;
                Acceleration *= PlayerSpeed;
                Acceleration += -3.0f * pPlayer->Velo;

                f32 MetersToPixels = pTileMap->TileSideInPixels / pTileMap->TileSideInMeters;

                pPlayer->Pos += (0.5f * Acceleration * Square(Input->dtForFrame) +
                                pPlayer->Velo * Input->dtForFrame) *
                                MetersToPixels;

                pPlayer->Velo += Acceleration * Input->dtForFrame;
                // f32 PlayerVeloSquaredMagnitude = Dot(pPlayer->Velo, pPlayer->Velo);
                // f32 SquaredMinMovementThreshold = 0.001; 
                // if (PlayerVeloSquaredMagnitude < SquaredMinMovementThreshold) {
                //     pPlayer->Velo = v2{0, 0};
                // }
            }
        }
    }

    if (GlobalDevKeys->F1.EndedDown && GlobalDevKeys->F1.HalfTransitionCount == 1)
    {
        // Zero cursor state before changing editor
        InitializeCursorState(&pGameState->EditorState.CursorState);
        if (pGameState->EditorState.WhichEditor == TILE)
        {
            pGameState->EditorState.WhichEditor = NO_EDITOR;
        }
        else
        {
            pGameState->EditorState.WhichEditor = TILE;
        }
    }
    if (GlobalDevKeys->F6.EndedDown && GlobalDevKeys->F6.HalfTransitionCount == 1)
    {
        InitializeCursorState(&pGameState->EditorState.CursorState);
        if (pGameState->EditorState.WhichEditor == PLAYER)
        {
            pGameState->EditorState.WhichEditor = NO_EDITOR;
        }
        else
        {
            pGameState->EditorState.WhichEditor = PLAYER;
        }
    }
    

    // Underlayer
    v2 ScreenMin = {0, 0};
    v2 ScreenMax = {(f32)Buffer->Width, (f32)Buffer->Height};
    DrawSimpleRect(Buffer, ScreenMin, ScreenMax, 0.5f, 0.55f, 0.6f);

    // RENDER
    // Tiles
    for (int Row = 0; Row < pTileMap->NumRows; ++Row) {
        for (int Col = 0; Col < pTileMap->NumCols; ++Col) {
            int OneDimensionalIdx = Get1DTileCoordFrom2DCoord(pTileMap->NumCols, v2{(f32)Col, (f32)Row});
            s32 TileValue = GetTileValueFrom1DCoord(pTileMap->TileValues, OneDimensionalIdx);
            v2 TileMin = {Col * pTileMap->TileSideInPixels, Row * pTileMap->TileSideInPixels};
            v2 TileMax = TileMin + (v2){pTileMap->TileSideInPixels, pTileMap->TileSideInPixels};

            if (TileValue > 0) {
                meta_bitmap *pToDraw = pTileMap->TileTypes.Get(TileValue);
                bitmap *pBitmap = &pToDraw->Bitmap;
                // For now bagel is 64x64 so we don't need to adjust the size if we're going to draw it,
                //      but if we change the tile size from 64x64 then we will
                if (pToDraw->IsPresent() == false) {
                    pBitmap = &pGlobalBagel->Bitmap;
                }
                ScaleAndBlitBitmap(Buffer, TileMin, TileMax, pBitmap);
            }
            if (pGameState->EditorState.WhichEditor == TILE)
            {
                DrawSpecialRect(Buffer, TileMin, TileMax, color{}, color{0.1, 0.1, 0.1, 1});
            }
        }
    }

    if (pGameState->EditorState.WhichEditor == TILE)
    {
        undo_step Result = DrawTileEditor(Buffer, pGameState, pTransArena);
        UpdateTileMapAfterEditorExit(pTileMap, Result);
    }
    else if (pGameState->EditorState.WhichEditor == PLAYER)
    {
        DrawPlayerEditor(Buffer, pGameState, pTransArena);
    }
    else
    {
        UpdatePlayerAnimationCel(pPlayer, Input->dtForFrame);
        bitmap *pToDraw = GetPlayerAnimationCelForThisFrame(pPlayer);

        v2 PlayerMin = {pPlayer->Pos.X - PLAYER_WIDTH * 0.5f, pPlayer->Pos.Y - PLAYER_HEIGHT};
        v2 PlayerMax = PlayerMin + v2{PLAYER_WIDTH, PLAYER_HEIGHT};
        ScaleAndBlitCel(Buffer, PlayerMin, PlayerMax, pToDraw);
    }
        // else {
            // facing_bitmaps *CurrentFacing = &pPlayer->FacingBitmaps[pPlayer->IsFacing];
            // meta_bitmap *pToDraw = CurrentFacing->MetaBitmaps.Get(CurrentFacing->DrawThis);
            // if (!pToDraw->IsPresent()) {
            //     pToDraw = pGlobalBagel;
            // }
            // f32 PlayerWidth = pToDraw->Bitmap.Width;
            // f32 PlayerHeight = pToDraw->Bitmap.Height;
            // v2 PlayerMin = {pPlayer->Position.X - PlayerWidth * 0.5f, 
            //     pPlayer->Position.Y - PlayerHeight};
            // v2 PlayerMax = PlayerMin + v2{PlayerWidth, PlayerHeight};
            //
            // // TODO(Aaron): doing it once per frame is bound to be very slow, and it seems like i can detect slightly jittery animation
            // //      in the game when moving character around. test this.
            // ScaleAndBlitBitmap(Buffer, PlayerMin, PlayerMax, &pToDraw->Bitmap);
        // }

    // I think we don't need to check half transition count for the keys with these save/load commands because subsequent EndedDown
    //      messages are routed to the message loop for the save/load dialog, not our usual
    //      message loop in the platform layer, and we have code in the platform layer to
    //      ensure that these are ignored (AS, 9/22)
    //
    // Note MoveDown is the "S" key
    if (GlobalKeyboardController->MoveDown.EndedDown && GlobalDevKeys->Ctrl.EndedDown) {
        SaveProject(pTileMap, pPlayer, pTransArena);
    }

    // and ActionRight is the "L" key
    if (GlobalKeyboardController->ActionRight.EndedDown && GlobalDevKeys->Ctrl.EndedDown) {
        LoadProject(pPlayer, pTileMap, pTransArena);
    }

    if (GlobalDevKeys->F2.EndedDown && GlobalDevKeys->F2.HalfTransitionCount == 1) {
        pGameState->EditorState.PrintRowsCols = !pGameState->EditorState.PrintRowsCols;
    }

    DEBUGPrintFps(Buffer, Input->Fps, pTransArena);

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    pgSharedProfData = Memory->pgSharedProfData;
    ReadCpuTimer = Memory->DEBUGReadCpuTimer;
    GameOutputSound(SoundBuffer, 400);
}
ProfilerEndOfCompilationUnit;

#pragma once
#include "tufty.h"

static mem_idx
SaveProject(tile_map *pTileMap, player *pPlayer, arena *pTransArena)
{
    // If we successfully got a filepath from the OS file dialog and wrote the project to a file,
    //      this function returns the number of bytes written to that file. If we didn't have
    //      anything to write (e.g., empty project), or some failure occurred along the way,
    //      we return 0. The caller can interpret this in whichever way they choose.

    mem_idx Result = 0;

    // On Windows we call GetSaveFilenameA. It returns either nonzero or zero.
    //      The return value is NONZERO if: 
    //              - the user specifies a filepath, and 
    //              - clicks the OK button, and
    //              - the function is successful.
    //      The return value is ZERO if:
    //              - the user cancels, or
    //              - the user closes the dialog box, or
    //              - an error, such as the file name buffer being too small, occurred.
    char *pSavepath = PushArray(pTransArena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(pSavepath, STRING_LEN, true);
    if (GetFilepathResult == 0) { 
        return 0;
    }

    arena OutfileArena;
    InitSubArena(&OutfileArena, pTransArena, Megabytes(1));
    arena *pOutfileArena = &OutfileArena;
    saved_project *pHeader = PushPackedStruct(pOutfileArena, saved_project);

    // Magic number
    char *pMagicNumber = "TUFT";
    mem_idx MagicNumberSize = strnlen(pMagicNumber, STRING_LEN);
    assert(MagicNumberSize == 4);
    for (int NthLetter = 0; NthLetter < MagicNumberSize; ++NthLetter) {
        pHeader->MagicNumber[NthLetter] = pMagicNumber[NthLetter];
    }

    // Fields we can just copy    
    pHeader->NumTileTypes = pTileMap->TileTypes.CountNonEmptySlots();

    // Tile type filepaths
    pHeader->TileTypeFilepathsOffset = pOutfileArena->Used;
    arr_meta_bitmap *pTypes = &pTileMap->TileTypes;
    for (int Slot = 0; Slot < pTypes->Len; ++Slot) {
        meta_bitmap *pIt = pTypes->Get(Slot);
        if (pIt->IsEmpty() == false) {
            mem_idx FilepathLen = strlen(pIt->Filepath);
            ++FilepathLen;
            char *FilepathBufferInOutfile = PushPackedArray(pOutfileArena, char, FilepathLen);
            snprintf(FilepathBufferInOutfile, FilepathLen, "%s", pIt->Filepath);
        }
    }

    // Tile values
    pHeader->TileValuesOffset = pOutfileArena->Used;
    s32 *TileValuesInOutfile = PushPackedArray(pOutfileArena, s32, TILES_IN_WORLD);
    memcpy(TileValuesInOutfile, pTileMap->TileValues, TILES_IN_WORLD_NUM_BYTES);

    // Write file
    mem_idx NumBytesToWrite = pOutfileArena->Used;

    // TODO(AARON): On Windows we have updated WriteEntireFile to return the number of bytes the file wrote,
    //      so that we can return that from this function.
    mem_idx BytesWritten = WriteEntireFile(pSavepath, NumBytesToWrite, pOutfileArena->pBase);
    if (BytesWritten != NumBytesToWrite) {
        Result = 0;
    }
    else {
        Result = BytesWritten;
    }

    return Result;
}

static mem_idx
LoadProject(player *pPlayer, tile_map *pTileMap, arena *pTransArena)
{
    char *pLoadedFileFilepath = PushArray(pTransArena, char, STRING_LEN);
    int GetFilepathResult = GetFilepathFromDialog(pLoadedFileFilepath, STRING_LEN, false);
    if (GetFilepathResult == 0) {
        return 0;
    }

    u32 LoadedFileSize = GetFileSize(pLoadedFileFilepath);
    if (LoadedFileSize == 0) {
        return 0;
    } 

    u8 *pLoadedDataStart = PushArray(pTransArena, u8, LoadedFileSize);
    mem_idx BytesReadIntoBuffer = ReadFileInto(pLoadedFileFilepath, LoadedFileSize, pLoadedDataStart);
    if (BytesReadIntoBuffer == 0) {
        return 0;
    } 

    saved_project *pHeader = (saved_project *)pLoadedDataStart;

    // char MagicNumber[4];
    char *pCompareMagicNumber = "TUFT";
    for (int LetterIdx = 0; LetterIdx < 4; ++LetterIdx) {
        if (pCompareMagicNumber[LetterIdx] != pHeader->MagicNumber[LetterIdx]) {
            return 0;
        }
    }

    char *pFilepathToCopyCursor = (char *)(pLoadedDataStart + pHeader->TileTypeFilepathsOffset);
    arr_meta_bitmap *pTileTypes = &pTileMap->TileTypes;
    pTileTypes->FullClear();
    for (int NthTileType = 0; NthTileType < pHeader->NumTileTypes; ++NthTileType) {
        int AddedToSlotN = pTileTypes->Add(pFilepathToCopyCursor);
        assert(AddedToSlotN);
        pFilepathToCopyCursor += strlen(pFilepathToCopyCursor) + 1;
    }

    // Tile values
    memcpy(pTileMap->TileValues, pLoadedDataStart + pHeader->TileValuesOffset, 
           GetByteCountForTypedArray(s32, TILES_IN_WORLD_Y * TILES_IN_WORLD_X));

    LoadTileBitmapsDir(pTileMap, pTransArena);

    return 0;
}


/*==============================================
	File created by Yohann NICOLAS.
	Add support 1.13d by L'Autour.

/*============================================*/
#pragma once

//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>

#include "../Commons/d2Constants.h"
#include "../Commons/d2BinFile.h"
#include "../Commons/d2Struct.h"
#include "../Commons/d2StringTblStruct.h"
#include "../Commons/D2UnitStruct.h"
#include "../Commons/updatingConst.h"
#include "../Commons/VersionInfo.h"

#include "error.h"
#include "modifMemory.h"
#include "parameters.h"
#include "d2wrapper.h"
#include "playerCustomData.h" // include common.h
#include "globalVariable.h" // include common.h

#include "d2functions.h"
#include "LocalizedStrings.h"

bool isOnRect(DWORD x, DWORD y, DWORD x0, DWORD y0, DWORD l, DWORD h);
void freeMessage(sWinMessage* msg);

#define FNV1A_INIT 0x811C9DC5
DWORD fnv1a(const BYTE* data, DWORD size, DWORD hash = FNV1A_INIT);
bool formatPath(char* path, DWORD maxSize, const char* format, ...);
bool getSaveFilePath(char* path, DWORD maxSize, const char* part1, const char* part2 = NULL, const char* part3 = NULL);
bool writeFileAtomic(const char* finalPath, const BYTE* data, DWORD size);
// 1: read into a D2FogMemAlloc buffer, 0: missing or empty, -1: exists but unreadable or shorter than minSize.
int readSaveFileData(const char* path, BYTE** data, DWORD* size, DWORD minSize);

/*================================= END OF FILE =================================*/
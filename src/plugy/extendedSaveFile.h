/*=================================================================
	File created by Yohann NICOLAS.

	Add an extra save file for each characters.

=================================================================*/
#pragma once

#include "common.h"

BYTE* readExtendedSaveFile(char* name, DWORD* size);
// reportFailure false: a failure goes to the log only, never to a box that would stall the game. Returns true when written.
bool writeExtendedSaveFile(char* name, BYTE* data, DWORD size, bool reportFailure = true);
// The .d2x as it is on disk, so a save can put it back with restoreExtendedSaveFile: 1 read (*data from
// D2FogMemAlloc), 0 missing or empty, -1 unreadable.
int readExtendedSaveFileCopy(char* name, BYTE** data, DWORD* size);
bool restoreExtendedSaveFile(char* name, int state, BYTE* data, DWORD size);

int loadExtendedSaveFile(Unit* ptChar, BYTE* data, DWORD maxSize);
void saveExtendedSaveFile(Unit* ptChar, BYTE** data, DWORD* maxSize, DWORD* curSize);

void backupExtendedSaveFile(char* name);

/*================================= END OF FILE =================================*/
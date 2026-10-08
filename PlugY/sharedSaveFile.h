/*=================================================================
	File created by Yohann NICOLAS.

	Add an extra save file shared by all own characters.

=================================================================*/
#pragma once

#include "common.h"

// filePath (MAX_PATH chars, optional) receives the shared file's path, or "" when it couldn't be built.
BYTE* readSharedSaveFile(char* name, DWORD* size, bool* fileExisted = NULL, char* filePath = NULL);
void writeSharedSaveFile(char* name, BYTE* data, DWORD size, bool isHardcore);

int loadSharedSaveFile(Unit* ptChar, BYTE* data, DWORD maxSize);
void saveSharedSaveFile(Unit* ptChar, BYTE** data, DWORD* maxSize, DWORD* curSize);

void initSharedStashBaseline(Unit* ptChar, DWORD diskHash, bool diskExisted);
void writeSharedSaveFileIfChanged(Unit* ptChar, BYTE* data, DWORD size, bool isHardcore);

void backupSharedSaveFile();

/*================================= END OF FILE =================================*/
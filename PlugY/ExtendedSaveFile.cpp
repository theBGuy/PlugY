/*=================================================================
	File created by Yohann NICOLAS.

	Add an extra save file for each characters.

=================================================================*/

#include "extendedSaveFile.h"
#include "infinityStash.h"
#include "customLibraries.h"
#include "extraOptions.h"
#include "common.h"
#include <stdio.h>

#define BUFFER_SIZE 0x4000
#define FILE_VERSION 0x3130			//"01"
#define FILE_EXTENDED 0x4D545343	//"CSTM"
#define FILE_MIN_SIZE 14			// tag, version, number of players, number of stash pages


// Returns NULL when the file exists but can't be read, so the next save never replaces it with an empty stash.
BYTE * readExtendedSaveFile(char* name, DWORD* size)
{
	char filename[MAX_PATH];
	BYTE* data = NULL;
	*size = 0;
	if (!name)
	{
		log_msg("readExtendedSaveFile : bad file name (NULL)\n");
		return NULL;
	}
	if (!getSaveFilePath(filename, MAX_PATH, name, ".d2x"))
		return NULL;

	log_msg("Extended file to read : %s\n",filename);

	int found = readSaveFileData(filename, &data, size, FILE_MIN_SIZE);
	if (found < 0)
	{
		log_msg("Can't read the extended save file %s, the character is not loaded\n", filename);
		return NULL;
	}
	if (!found)
	{
		log_msg("Can't open extented save file in mode \"rb\" (is not an error if it's a new player)\n");
		DWORD maxSize = 100;
		data = (BYTE*)D2FogMemAlloc(maxSize,__FILE__,__LINE__,0);
		if (!data)
		{
			log_msg("readExtendedSaveFile : memory allocation failed\n");
			return NULL;
		}
		*size = 14;
		*((DWORD*)&data[0])  = FILE_EXTENDED; //"CSTM"
		*((WORD *)&data[4])  = FILE_VERSION;
		*((DWORD*)&data[6])  = (BYTE)(nbPlayersCommandByDefault == 0 ? 0 : nbPlayersCommandByDefault - 1);
		*((DWORD*)&data[10]) = 0;// number of stash

		TCustomDll* currentDll = customDlls;
		while (currentDll)
		{
			currentDll->initExtendedSaveFile(&data, &maxSize, size);
			currentDll=currentDll->nextDll;
		}
	}
	return data;
}


int loadExtendedSaveFile(Unit* ptChar, BYTE data[], DWORD maxSize)
{
	if (!ptChar || !PCPY || !data) return 0;

	log_msg("Load extended file\n");

	DWORD curSize = 0;

	if (*(DWORD*)&data[curSize] != FILE_EXTENDED)
	{
		log_msg("loadExtendedSaveFile -> bad header\n");
		return 9;
	}
	curSize += 4;

	if (*(WORD *)&data[curSize] != FILE_VERSION)
	{
		log_msg("loadExtendedSaveFile -> bad file version\n");
		return 9;
	}
	curSize += 2;
	nbPlayersCommand = (*(BYTE*)&data[curSize]) + 1;
	curSize += 1;
	curSize += 3;

	int ret = loadStashList(ptChar, data, maxSize, &curSize, false);

	TCustomDll* currentDll = customDlls;
	while (!ret && currentDll)
	{
		ret = currentDll->loadExtendedSaveFile(ptChar, data, maxSize, &curSize);
		currentDll=currentDll->nextDll;
	}


	PCPY->selfStashIsOpened = true;
	return ret;
}


bool writeExtendedSaveFile(char* name, BYTE* data, DWORD size, bool reportFailure)
{
	char szSaveName[MAX_PATH];
	if (!getSaveFilePath(szSaveName, MAX_PATH, name, ".d2x"))
	{
		if (reportFailure)
			log_box("PlugY could not save the stash of %s: the save path is too long.\nThe previous .d2x file was left unchanged.", name);
		else
			log_msg("Could not save the stash of %s: the save path is too long.\n", name);
		return false;
	}
	log_msg("Extended file for saving : %s\n",szSaveName);

	// one box per character: a .d2x frozen read-only on purpose would otherwise interrupt every save
	static char lastBoxedName[64] = "";
	bool saved = writeFileAtomic(szSaveName, data, size);
	if (!saved)
	{
		if (!reportFailure)
			log_msg("Could not save %s.\n", szSaveName);
		else if (_stricmp(lastBoxedName, name))
		{
			strncpy(lastBoxedName, name, sizeof(lastBoxedName)-1);
			log_box("PlugY could not save %s (read-only, locked by another program, or disk full).\nThe file on disk was not changed. An unsaved copy may be kept next to it as a .tmp file.", szSaveName);
		} else
			log_msg("Could not save %s (already reported to the player).\n", szSaveName);
	}
	return saved;
}


int readExtendedSaveFileCopy(char* name, BYTE** data, DWORD* size)
{
	char szSaveName[MAX_PATH];
	*data = NULL;
	*size = 0;
	if (!getSaveFilePath(szSaveName, MAX_PATH, name, ".d2x"))
		return -1;
	return readSaveFileData(szSaveName, data, size, 1);
}


bool restoreExtendedSaveFile(char* name, int state, BYTE* data, DWORD size)
{
	char szSaveName[MAX_PATH];
	if (!getSaveFilePath(szSaveName, MAX_PATH, name, ".d2x"))
		return false;
	bool restored = state > 0 ? writeFileAtomic(szSaveName, data, size) : DeleteFileA(szSaveName) != 0;
	log_msg(restored ? "%s put back as it was before this save\n" : "Could not put %s back as it was before this save\n", szSaveName);
	return restored;
}


void saveExtendedSaveFile(Unit* ptChar, BYTE** data, DWORD* maxSize, DWORD* curSize)
{
	*(DWORD *)(*data + *curSize) = FILE_EXTENDED; //"CSTM"
	*curSize += 4;
	*(WORD *)(*data + *curSize) = FILE_VERSION;
	*curSize += 2;
	*(DWORD *)(*data + *curSize) = (BYTE)(nbPlayersCommand == 0 ? 0 : nbPlayersCommand - 1);
	*curSize += 4;

	saveStashList(ptChar, PCPY->selfStash, data, maxSize, curSize);

	TCustomDll* currentDll = customDlls;
	while (currentDll)
	{
		currentDll->saveExtendedSaveFile(ptChar, data, maxSize, curSize);
		currentDll=currentDll->nextDll;
	}
}

/*================================= END OF FILE =================================*/
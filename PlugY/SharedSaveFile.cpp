/*=================================================================
	File created by Yohann NICOLAS.

	Add an extra save file shared by all own characters.

=================================================================*/

#include "sharedSaveFile.h"
#include "infinityStash.h"
#include "customLibraries.h"
#include "common.h"
#include <stdio.h>

#define FILE_SHAREDSTASH 0x00535353	//"SSS "
#define BUFFER_SIZE 0x4000
#define FILE_VERSION 0x3230			//"02"
#define FILE_MIN_SIZE 10			// smallest valid file : tag, version "01", number of stash


static bool getSharedSaveFilePath(char* path, bool isHardcore)
{
	return getSaveFilePath(path, MAX_PATH, separateHardSoftStash && isHardcore ? "_LOD_HC_" : "_LOD_", sharedStashFilename, ".sss");
}


// The mutex is named after the file so every process sharing that file serializes on it.
static HANDLE lockSharedSaveFile(const char* path)
{
	char fullPath[MAX_PATH];
	DWORD len = GetFullPathNameA(path, MAX_PATH, fullPath, NULL);
	if (!len || len >= MAX_PATH)
	{
		strncpy(fullPath, path, MAX_PATH-1);
		fullPath[MAX_PATH-1] = NULL;
	}
	// 8.3 and long spellings of the same save folder must name the same mutex
	char longPath[MAX_PATH];
	len = GetLongPathNameA(fullPath, longPath, MAX_PATH);
	if (len && len < MAX_PATH)
		strcpy(fullPath, longPath);
	_strlwr(fullPath);

	char mutexName[64];
	sprintf(mutexName, "Local\\PlugY_SharedStash_%08X", fnv1a((BYTE*)fullPath, strlen(fullPath)));
	HANDLE hMutex = CreateMutexA(NULL, FALSE, mutexName);
	if (!hMutex)
	{
		log_msg("Can't create mutex %s (error %u), continue without lock\n", mutexName, GetLastError());
		return NULL;
	}
	// A holder can keep the lock about 15 s (read retries, replace retries, then its conflict file), so this waits longer.
	DWORD wait = WaitForSingleObject(hMutex, 20000);
	if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
		return hMutex;
	log_msg("Can't lock %s for %s (wait result %u, error %u), continue without lock\n", mutexName, fullPath, wait, GetLastError());
	CloseHandle(hMutex);
	return NULL;
}


static void unlockSharedSaveFile(HANDLE hMutex)
{
	if (!hMutex) return;
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
}


// Returns NULL when the shared file exists but can't be read, so it is never replaced by an empty stash.
BYTE* readSharedSaveFile(char* name, DWORD* size, bool* fileExisted, char* filePath)
{
	char filename[MAX_PATH];
	BYTE* data=NULL;
	BYTE isHardCore=0;
	int found=0;

	*size = 0;
	if (fileExisted)
		*fileExisted = false;
	if (filePath)
		filePath[0] = NULL;

	if (separateHardSoftStash)//Get hardcore flag
	{
		if (getSaveFilePath(filename, MAX_PATH, name, ".d2s"))
		{
			log_msg("Normal file to read if it's hardcore character : %s\n",filename);
			FILE* file = fopen(filename, "rb");
			if (file)
			{
				fseek(file, 0x24, SEEK_SET);
				DWORD nbRead = fread(&isHardCore, 1, 1, file);
				isHardCore = (nbRead==1)? ((isHardCore & 4) == 4) : 0;
				fclose(file);
			}
		} else if (active_sharedStash)
			return NULL;
		log_msg("%s is a HardCore character = %d\n",name,isHardCore);
	}

	if (active_sharedStash)
	{
		if (!getSharedSaveFilePath(filename, isHardCore != 0))
			return NULL;
		if (filePath)
			strcpy(filePath, filename);

		log_msg("Shared file to read : %s\n",filename);

		HANDLE hLock = lockSharedSaveFile(filename);
		found = readSaveFileData(filename, &data, size, FILE_MIN_SIZE);
		unlockSharedSaveFile(hLock);
		if (found < 0)
			return NULL;
	}

	if (found)
	{
		if (fileExisted)
			*fileExisted = true;
	} else {
		log_msg("Can't open shared save file in mode \"rb\" (is not an error if it's the first start of the mod)\n");
		*size = 14;
		DWORD maxSize = 100;
		data = (BYTE*)D2FogMemAlloc(maxSize,__FILE__,__LINE__,0);
		if (!data)
		{
			log_msg("readSharedSaveFile : memory allocation failed\n");
			*size = 0;
			return NULL;
		}
		*((DWORD *)&data[0]) = FILE_SHAREDSTASH;
		*((WORD *)&data[4]) = FILE_VERSION;
		*((DWORD *)&data[6]) = 0;
		*((DWORD *)&data[10]) = 0;// number of stash

		TCustomDll* currentDll = customDlls;
		while (currentDll)
		{
			currentDll->initSharedSaveFile(&data, &maxSize, size);
			currentDll=currentDll->nextDll;
		}
	}

	return data;
}


int loadSharedSaveFile(Unit* ptChar, BYTE data[], DWORD maxSize)
{
	if ( !ptChar || !data) return false;

	log_msg("Load shared file\n");

	DWORD curSize = 0;

	if (*(DWORD*)&data[curSize] != FILE_SHAREDSTASH)
	{
		log_msg("loadSharedSaveFile -> bad header\n");
		return 9;
	}
	curSize += 4;

	if (*(WORD *)&data[curSize] == 0x3130) {//"01"
		curSize += 2;
		PCPY->sharedGold = 0;
	} else if (*(WORD *)&data[curSize] == FILE_VERSION) {
		curSize += 2;
		PCPY->sharedGold = *(DWORD*)&data[curSize];
		curSize += 4;
	} else {
		log_msg("loadSharedSaveFile -> bad file version : %04X\n", *(WORD *)&data[curSize]);
		return 9;
	}

	int ret = loadStashList(ptChar, data, maxSize, &curSize, true);

	TCustomDll* currentDll = customDlls;
	while (!ret && currentDll)
	{
		ret = currentDll->loadSharedSaveFile(ptChar, data, maxSize, &curSize);
		currentDll=currentDll->nextDll;
	}

	PCPY->sharedStashIsOpened = true;
	return ret;
}


void writeSharedSaveFile(char* name, BYTE* data, DWORD size, bool isHardcore)
{
	char szSaveName[MAX_PATH];
	if (!getSharedSaveFilePath(szSaveName, isHardcore))
	{
		log_box("PlugY could not save the shared stash: the save path is too long.\nThe previous shared stash file was left unchanged.");
		return;
	}
	log_msg("Shared file for saving : %s\n", szSaveName);

	HANDLE hLock = lockSharedSaveFile(szSaveName);
	bool saved = writeFileAtomic(szSaveName, data, size);
	unlockSharedSaveFile(hLock);
	// The box is raised only after unlocking, so a player reading it never blocks other games.
	if (!saved)
		log_box("PlugY could not save the shared stash %s (read-only, locked by another program, or disk full).\nThe file on disk was not changed. An unsaved copy may be kept next to it as a .tmp file.", szSaveName);
}


static bool isSaveFileMissing(const char* path)
{
	WIN32_FILE_ATTRIBUTE_DATA info;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info))
	{
		DWORD error = GetLastError();
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
	}
	return !info.nFileSizeLow && !info.nFileSizeHigh;
}


void initSharedStashBaseline(Unit* ptChar, DWORD diskHash, bool diskExisted)
{
	PCPY->sharedStateFlags = 0;
	PCPY->sharedDiskExisted = diskExisted;
	PCPY->sharedDiskHash = diskExisted ? diskHash : 0;
	PCPY->sharedBaselineHash = 0;
	PCPY->sharedConflictStamp[0] = NULL;

	DWORD baselineHash;
	if (!getSharedStashCanonicalHash(ptChar, &baselineHash))
	{
		log_msg("initSharedStashBaseline : can't compute the shared stash hash, every save counts as modified\n");
		return;
	}
	PCPY->sharedBaselineHash = baselineHash;
	PCPY->sharedBaselineValid = 1;
	log_msg("Shared stash baseline : %08X, file hash : %08X, file existed : %d\n", PCPY->sharedBaselineHash, PCPY->sharedDiskHash, diskExisted);
}


// Writes this game's version next to the shared file; one name per game, so later saves of the same game replace it.
static bool writeSharedConflictFile(Unit* ptChar, BYTE* data, DWORD size, bool isHardcore, char* szConflictName)
{
	if (!PCPY->sharedConflictStamp[0])
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		_snprintf(PCPY->sharedConflictStamp, sizeof(PCPY->sharedConflictStamp), "%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		PCPY->sharedConflictStamp[sizeof(PCPY->sharedConflictStamp)-1] = NULL;
	}
	char suffix[64];
	sprintf(suffix, ".conflict-%s-%u.sss", PCPY->sharedConflictStamp, GetCurrentProcessId());
	if (!getSaveFilePath(szConflictName, MAX_PATH, separateHardSoftStash && isHardcore ? "_LOD_HC_" : "_LOD_", sharedStashFilename, suffix))
	{
		strcpy(szConflictName, "(path too long)");
		return false;
	}
	return writeFileAtomic(szConflictName, data, size);
}


static const char* conflictTail(bool saved, const char* szConflictName, char* tail)
{
	sprintf(tail, saved ? "This game's version was saved to %s; merge the two by hand." : "This game's version could not be saved either (%s).", szConflictName);
	return tail;
}


// Never overwrites a shared file that another process saved after this game loaded it.
void writeSharedSaveFileIfChanged(Unit* ptChar, BYTE* data, DWORD size, bool isHardcore)
{
	DWORD newHash = fnv1a(data, size);
	DWORD canonicalHash;
	bool canonicalValid = getSharedStashCanonicalHash(ptChar, &canonicalHash);

	char szSaveName[MAX_PATH];
	char message[1024];
	message[0] = NULL;
	if (!getSharedSaveFilePath(szSaveName, isHardcore))
	{
		_snprintf(message, sizeof(message), "PlugY could not save the shared stash: the save path is too long.\nThe shared stash file on disk was not changed.");
	}
	// once this game wrote a conflict file it is kept current, or merging it later could duplicate items taken back
	else if (PCPY->sharedBaselineValid && canonicalValid && canonicalHash == PCPY->sharedBaselineHash
	 && !isSaveFileMissing(szSaveName) && !PCPY->sharedConflictStamp[0])
	{
		log_msg("shared stash unchanged since load; not rewritten\n");
		return;
	}
	else
	{
		HANDLE hLock = lockSharedSaveFile(szSaveName);
		char szConflictName[MAX_PATH];
		char tail[MAX_PATH + 64];
		const char* conflictResult;

		if (!hLock)
		{
			// Without the lock another game may be replacing the file right now, so only this game's own file is written.
			conflictResult = conflictTail(writeSharedConflictFile(ptChar, data, size, isHardcore, szConflictName), szConflictName, tail);
			_snprintf(message, sizeof(message), "PlugY could not lock the shared stash %s (another game kept it busy), so it was not overwritten.\n%s", szSaveName, conflictResult);
		} else {
			BYTE* diskData = NULL;
			DWORD diskSize = 0;
			DWORD diskHash = 0;
			int diskState = readSaveFileData(szSaveName, &diskData, &diskSize, FILE_MIN_SIZE);
			if (diskState > 0)
			{
				diskHash = fnv1a(diskData, diskSize);
				D2FogMemDeAlloc(diskData,__FILE__,__LINE__,0);
			}

			// A missing file has nothing left to protect; an unreadable one or one another game saved does.
			if (!diskState || (diskState > 0 && PCPY->sharedDiskExisted && diskHash == PCPY->sharedDiskHash))
			{
				log_msg("Shared file for saving : %s\n", szSaveName);
				if (writeFileAtomic(szSaveName, data, size))
				{
					PCPY->sharedDiskHash = newHash;
					PCPY->sharedDiskExisted = 1;
					PCPY->sharedBaselineHash = canonicalValid ? canonicalHash : 0;
					PCPY->sharedBaselineValid = canonicalValid;
				} else {
					conflictResult = conflictTail(writeSharedConflictFile(ptChar, data, size, isHardcore, szConflictName), szConflictName, tail);
					_snprintf(message, sizeof(message), "PlugY could not save the shared stash %s (read-only, locked by another program, or disk full); it was not changed.\n%s", szSaveName, conflictResult);
				}
			} else {
				conflictResult = conflictTail(writeSharedConflictFile(ptChar, data, size, isHardcore, szConflictName), szConflictName, tail);
				_snprintf(message, sizeof(message), "Another game saved the shared stash %s after this game loaded it (or it can no longer be read), so it was not overwritten.\n%s", szSaveName, conflictResult);
			}
		}
		unlockSharedSaveFile(hLock);
	}

	// One box per game: bots saving every few seconds would otherwise stack them. Raised after unlocking, so it never blocks other games.
	if (!message[0])
		return;
	message[sizeof(message)-1] = NULL;
	if (PCPY->sharedInConflict)
		log_msg("%s\n", message);
	else
	{
		PCPY->sharedInConflict = 1;
		log_box("%s", message);
	}
}

void saveSharedSaveFile(Unit* ptChar, BYTE** data, DWORD* maxSize, DWORD* curSize)
{
	*(DWORD *)(*data + *curSize) = FILE_SHAREDSTASH;
	*curSize += 4;
	if (PCPY->sharedGold)
	{
		*(WORD *)(*data + *curSize) = FILE_VERSION;
		*curSize += 2;

		*(DWORD *)(*data + *curSize) = PCPY->sharedGold;
		*curSize += 4;
	} else {
		*(WORD *)(*data + *curSize) = 0x3130;
		*curSize += 2;
	}
	saveStashList(ptChar, PCPY->sharedStash, data, maxSize, curSize);

	TCustomDll* currentDll = customDlls;
	while (currentDll)
	{
		currentDll->saveSharedSaveFile(ptChar, data, maxSize, curSize);
		currentDll=currentDll->nextDll;
	}
}


/*================================= END OF FILE =================================*/
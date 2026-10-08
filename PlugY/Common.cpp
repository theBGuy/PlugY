/*=================================================================
	File created by Yohann NICOLAS.

	Common functions.

=================================================================*/

#include "common.h"
#include <stdio.h>
#include <stdarg.h>

bool isOnRect(DWORD x, DWORD y, DWORD x0, DWORD y0, DWORD l, DWORD h)
	{return (x>=x0) && (x<x0+l) && (y>y0-h) && (y<=y0);}


void freeMessage(sWinMessage* msg)
{
	msg->managed=1;
	msg->unmanaged=0;
	D2FreeWinMessage(msg);
}


DWORD fnv1a(const BYTE* data, DWORD size, DWORD hash)
{
	for (DWORD i = 0; i < size; i++)
	{
		hash ^= data[i];
		hash *= 0x01000193;
	}
	return hash;
}


bool formatPath(char* path, DWORD maxSize, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int len = _vsnprintf(path, maxSize, format, args);
	va_end(args);
	path[maxSize-1] = NULL;
	if (len < 0 || (DWORD)len >= maxSize)
	{
		log_msg("Path too long (more than %u characters) : %s...\n", maxSize-1, path);
		return false;
	}
	return true;
}


bool getSaveFilePath(char* path, DWORD maxSize, const char* part1, const char* part2, const char* part3)
{
	char savePath[MAX_PATH];
	D2FogGetSavePath(savePath, MAX_PATH);
	savePath[MAX_PATH-1] = NULL;
	return formatPath(path, maxSize, "%s%s%s%s", savePath, part1, part2 ? part2 : "", part3 ? part3 : "");
}


// A failed call can leave GetLastError at 0, which must never read as success.
static DWORD lastErrorOrFailure()
{
	DWORD error = GetLastError();
	return error ? error : ERROR_GEN_FAILURE;
}


// CREATE_NEW never truncates an existing temp file: one kept by an earlier failed save may be the only copy of that data.
static HANDLE createTempFile(char* tempPath, const char* finalPath)
{
	if (formatPath(tempPath, MAX_PATH, "%s.%u.tmp", finalPath, GetCurrentProcessId()))
	{
		HANDLE hFile = CreateFileA(tempPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
			return hFile;
		log_msg("writeFileAtomic : can't create %s (error %u), trying another name\n", tempPath, GetLastError());
	}
	if (!formatPath(tempPath, MAX_PATH, "%s.%u.%u.tmp", finalPath, GetCurrentProcessId(), GetTickCount()))
		return INVALID_HANDLE_VALUE;
	HANDLE hFile = CreateFileA(tempPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		log_msg("writeFileAtomic : can't create %s (error %u)\n", tempPath, GetLastError());
	return hFile;
}


// Temps this process kept after a failed replace. Only these are ever deleted: matching by name could hit a
// temp kept by a dead process whose pid was reused, and that file may be the only copy of its game's data.
// Save paths run on one thread, so the list needs no lock.
#define MAX_KEPT_TEMPS 16
static char keptTempPaths[MAX_KEPT_TEMPS][MAX_PATH];
static int nbKeptTemps = 0;

static void rememberKeptTemp(const char* tempPath)
{
	if (nbKeptTemps < MAX_KEPT_TEMPS)
	{
		strncpy(keptTempPaths[nbKeptTemps], tempPath, MAX_PATH-1);
		keptTempPaths[nbKeptTemps][MAX_PATH-1] = NULL;
		nbKeptTemps++;
	}
}

// A kept temp is superseded once a newer save of the same file succeeded.
static void deleteOwnOlderTempFiles(const char* finalPath)
{
	int len = strlen(finalPath);
	for (int i = 0; i < nbKeptTemps; )
	{
		if (!_strnicmp(keptTempPaths[i], finalPath, len) && keptTempPaths[i][len] == '.')
		{
			if (DeleteFileA(keptTempPaths[i]))
				log_msg("writeFileAtomic : deleted the older temporary file %s\n", keptTempPaths[i]);
			else
				log_msg("writeFileAtomic : can't delete the older temporary file %s (error %u)\n", keptTempPaths[i], GetLastError());
			nbKeptTemps--;
			if (i < nbKeptTemps)
				memcpy(keptTempPaths[i], keptTempPaths[nbKeptTemps], MAX_PATH);
		}
		else
			i++;
	}
}


bool writeFileAtomic(const char* finalPath, const BYTE* data, DWORD size)
{
	char tempPath[MAX_PATH];
	HANDLE hFile = createTempFile(tempPath, finalPath);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD error = 0;
	DWORD written = 0;
	while (!error && written < size)
	{
		DWORD nbWritten = 0;
		if (!WriteFile(hFile, data + written, size - written, &nbWritten, NULL))
			error = lastErrorOrFailure();
		else if (!nbWritten)
			error = ERROR_WRITE_FAULT;
		written += nbWritten;
	}
	if (!error && !FlushFileBuffers(hFile))
		error = lastErrorOrFailure();
	if (!CloseHandle(hFile) && !error)
		error = lastErrorOrFailure();
	if (error)
	{
		log_msg("writeFileAtomic : can't write %s (error %u)\n", tempPath, error);
		DeleteFileA(tempPath);
		return false;
	}

	// A read-only target was frozen by the user on purpose: it is respected, not retried.
	DWORD attributes = GetFileAttributesA(finalPath);
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
	{
		log_msg("%s is read-only, not saved\n", finalPath);
		DeleteFileA(tempPath);
		return false;
	}

	// Measured on Windows 10 NTFS: replacing a target another process holds open fails with ERROR_ACCESS_DENIED,
	// not a sharing violation, so both get the same window (read-only targets were refused above).
	for (int retry = 0; ; retry++)
	{
		if (MoveFileExA(tempPath, finalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			deleteOwnOlderTempFiles(finalPath);
			return true;
		}
		error = GetLastError();
		if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) || retry >= 100)
			break;
		Sleep(50);
	}
	rememberKeptTemp(tempPath);
	log_msg("writeFileAtomic : can't replace %s, the new data is kept in %s (error %u)\n", finalPath, tempPath, error);
	return false;
}


int readSaveFileData(const char* path, BYTE** data, DWORD* size, DWORD minSize)
{
	*data = NULL;
	*size = 0;
	HANDLE hFile;
	DWORD error;
	// Another process replacing the file holds it only briefly.
	for (int retry = 0; ; retry++)
	{
		hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
			break;
		error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
			return 0;
		if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) || retry >= 100)
		{
			log_msg("Can't open save file %s (error %u)\n", path, error);
			return -1;
		}
		Sleep(50);
	}

	DWORD fileSize = GetFileSize(hFile, NULL);
	if (fileSize == INVALID_FILE_SIZE)
	{
		log_msg("Can't get the size of save file %s (error %u)\n", path, GetLastError());
		CloseHandle(hFile);
		return -1;
	}
	// An empty file holds no data to protect, typically a save interrupted before anything was written.
	if (!fileSize)
	{
		log_msg("Save file %s is empty, it is handled as missing\n", path);
		CloseHandle(hFile);
		return 0;
	}
	if (fileSize < minSize)
	{
		log_msg("Save file %s is truncated (%u bytes, header needs %u)\n", path, fileSize, minSize);
		CloseHandle(hFile);
		return -1;
	}

	BYTE* buffer = (BYTE*)D2FogMemAlloc(fileSize,__FILE__,__LINE__,0);
	error = buffer ? 0 : ERROR_NOT_ENOUGH_MEMORY;
	DWORD total = 0;
	while (!error && total < fileSize)
	{
		DWORD nbRead = 0;
		if (!ReadFile(hFile, buffer + total, fileSize - total, &nbRead, NULL))
			error = lastErrorOrFailure();
		else if (!nbRead)
			error = ERROR_HANDLE_EOF;
		total += nbRead;
	}
	CloseHandle(hFile);
	if (error)
	{
		log_msg("Can't read save file %s (error %u)\n", path, error);
		if (buffer)
			D2FogMemDeAlloc(buffer,__FILE__,__LINE__,0);
		return -1;
	}
	*data = buffer;
	*size = fileSize;
	return 1;
}

/*================================= END OF FILE =================================*/
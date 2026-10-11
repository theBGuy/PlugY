/*=================================================================
	Update download: release assets to disk, SHA-256 checks.
	See UpdateDownload.h.
=================================================================*/

#include "UpdateDownload.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif


typedef struct
{
	HANDLE hFile;
	DWORD error;
} FileSink;

static int fileSinkWrite(void* context, const BYTE* data, DWORD size)
{
	FileSink* f = (FileSink*)context;
	while (size)
	{
		DWORD written = 0;
		if (!WriteFile(f->hFile, data, size, &written, NULL) || !written)
		{
			f->error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
			return UPDATE_FILE_ERROR;
		}
		data += written;
		size -= written;
	}
	return UPDATE_OK;
}

// A handle's own file, not a link target (opened with FILE_FLAG_OPEN_REPARSE_POINT): neither a directory nor a reparse point.
static bool handleIsPlainFile(HANDLE h)
{
	BY_HANDLE_FILE_INFORMATION fi;
	return GetFileInformationByHandle(h, &fi)
		&& !(fi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

static bool renameHandle(HANDLE h, const wchar_t* target)
{
	size_t len = wcslen(target);
	DWORD size = (DWORD)(sizeof(FILE_RENAME_INFO) + (len + 1) * sizeof(wchar_t));
	FILE_RENAME_INFO* renameInfo = (FILE_RENAME_INFO*)calloc(1, size);
	if (!renameInfo)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return false;
	}
	renameInfo->ReplaceIfExists = TRUE;
	renameInfo->RootDirectory = NULL;
	renameInfo->FileNameLength = (DWORD)(len * sizeof(wchar_t));
	memcpy(renameInfo->FileName, target, (len + 1) * sizeof(wchar_t));
	BOOL ok = SetFileInformationByHandle(h, FileRenameInfo, renameInfo, size);
	DWORD error = GetLastError();
	free(renameInfo);
	SetLastError(error);
	return ok != FALSE;
}

static void deleteHandle(HANDLE h)
{
	FILE_DISPOSITION_INFO disposition;
	disposition.DeleteFile = TRUE;
	SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition));
}

/* The updater may run elevated: the temp is created without following any
   planted link, renamed by its own handle so exactly the bytes written move,
   and a link or folder already at path is refused, not replaced. */
int UpdateDownloadToFileEx(const char* url, const char* path, DWORD maxBytes, DWORD flags, UpdateHttpInfo* info)
{
	if (info)
		memset(info, 0, sizeof(*info));
	if (!url || !path || !path[0] || !maxBytes || (flags & ~UPDATE_HTTP_CHECK_REVOCATION) || _strnicmp(url, "https://", 8))
		return UPDATE_BAD_ARGUMENT;

	wchar_t widePath[MAX_PATH];
	wchar_t tempPath[MAX_PATH];
	if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, widePath, MAX_PATH))
		return UPDATE_BAD_ARGUMENT;
	int n = _snwprintf(tempPath, MAX_PATH, L"%s.%u.%u.part", widePath, GetCurrentProcessId(), GetTickCount());
	if (n < 0 || n >= MAX_PATH)
		return UPDATE_BAD_ARGUMENT;
	DWORD attributes = GetFileAttributesW(widePath);
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{
		if (info)
			info->winError = ERROR_ACCESS_DENIED;
		return UPDATE_FILE_ERROR;
	}
	FileSink sink = { INVALID_HANDLE_VALUE, 0 };
	sink.hFile = CreateFileW(tempPath, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (sink.hFile == INVALID_HANDLE_VALUE)
	{
		if (info)
			info->winError = GetLastError();
		return UPDATE_FILE_ERROR;
	}

	int result = handleIsPlainFile(sink.hFile)
		? UpdateHttpGetEx(url, NULL, maxBytes, flags, fileSinkWrite, &sink, info)
		: UPDATE_FILE_ERROR;
	if (result == UPDATE_OK && (!FlushFileBuffers(sink.hFile) || !renameHandle(sink.hFile, widePath)))
	{
		sink.error = GetLastError();
		result = UPDATE_FILE_ERROR;
	}
	if (result != UPDATE_OK)
		deleteHandle(sink.hFile);
	CloseHandle(sink.hFile);
	if (result == UPDATE_FILE_ERROR && info)
		info->winError = sink.error;
	return result;
}

int UpdateDownloadToFile(const char* url, const char* path, DWORD maxBytes, UpdateHttpInfo* info)
{
	return UpdateDownloadToFileEx(url, path, maxBytes, 0, info);
}

static void toHex(const BYTE* bytes, DWORD size, char* hex)
{
	static const char digits[] = "0123456789abcdef";
	for (DWORD i = 0; i < size; i++)
	{
		hex[i * 2] = digits[bytes[i] >> 4];
		hex[i * 2 + 1] = digits[bytes[i] & 0x0F];
	}
	hex[size * 2] = 0;
}

bool UpdateSha256File(const char* path, char hex[UPDATE_SHA256_HEX])
{
	if (!hex)
		return false;
	hex[0] = 0;
	if (!path)
		return false;
	HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	// What is hashed must be the file at path itself, never what a planted link points to.
	if (!handleIsPlainFile(hFile))
	{
		CloseHandle(hFile);
		return false;
	}
	BCRYPT_ALG_HANDLE hAlg = NULL;
	BCRYPT_HASH_HANDLE hHash = NULL;
	BYTE digest[32];
	BYTE buffer[16384];
	bool ok = NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0))
		&& NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0));
	while (ok)
	{
		DWORD got = 0;
		if (!ReadFile(hFile, buffer, sizeof(buffer), &got, NULL))
			ok = false;
		else if (!got)
			break;
		else if (!NT_SUCCESS(BCryptHashData(hHash, buffer, got, 0)))
			ok = false;
	}
	if (ok)
		ok = NT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0));
	if (hHash)
		BCryptDestroyHash(hHash);
	if (hAlg)
		BCryptCloseAlgorithmProvider(hAlg, 0);
	CloseHandle(hFile);
	if (ok)
		toHex(digest, sizeof(digest), hex);
	return ok;
}

static bool isHexDigit(char ch)
{
	return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool UpdateFindSha256Sum(const char* sums, DWORD len, const char* fileName, char hex[UPDATE_SHA256_HEX])
{
	if (!hex)
		return false;
	hex[0] = 0;
	if (!sums || !fileName || !fileName[0])
		return false;
	DWORD nameLen = (DWORD)strlen(fileName);
	DWORD pos = 0;
	while (pos < len)
	{
		DWORD lineEnd = pos;
		while (lineEnd < len && sums[lineEnd] != '\n')
			lineEnd++;
		DWORD next = lineEnd < len ? lineEnd + 1 : len;
		if (lineEnd > pos && sums[lineEnd - 1] == '\r')
			lineEnd--;
		const char* line = sums + pos;
		DWORD lineLen = lineEnd - pos;
		pos = next;
		// sha256sum -c format: the hash, a space, then ' ' (text mode) or '*' (binary mode), then the name.
		if (lineLen != 66 + nameLen || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')
			|| memcmp(line + 66, fileName, nameLen))
			continue;
		bool allHex = true;
		for (int i = 0; i < 64 && allHex; i++)
			allHex = isHexDigit(line[i]);
		if (!allHex)
			continue;
		for (int i = 0; i < 64; i++)
			hex[i] = (line[i] >= 'A' && line[i] <= 'F') ? (char)(line[i] - 'A' + 'a') : line[i];
		hex[64] = 0;
		return true;
	}
	return false;
}

///////////////////////// END OF FILE ///////////////////////

/*=================================================================
	File created by Yohann NICOLAS.

	Error Logger implementation.

=================================================================*/

#include "parameters.h"
#include "error.h"
#include "../common/SessionLog.h"
#include <windows.h>
#include <stdio.h>			// fopen() fclose() fwrite() _vsnprintf() _vscprintf()
#include <stdlib.h>			// malloc() realloc() free()
#include <stdarg.h>			// ...
#include <string.h>			// strlen() strcpy()
#include <direct.h>			// _getcwd()

int active_logFile = 1;
// Stays 1 until loadParameters reads the ini: boxes raised before that keep the historical behavior.
int active_ShowErrorMessageBox = 1;

#define LOG_PATH_SIZE (MAX_PATH * 2)

static char log_file[LOG_PATH_SIZE] = "";
static bool log_init = false;
// One attempt per process: a failed open is not retried on every message, which would rescan the folder each time.
static bool log_tried = false;
static bool log_lineStart = true;
static FILE* fLogFile = NULL;

// D2 runs its game server on a thread of its own, and PlugY logs there (saves, stash changes) while the client thread
// logs too; the line-start state also spans calls. Recursive, so the header openLog writes through log_msg is fine.
static CRITICAL_SECTION log_lock;
static volatile LONG log_lockState = 0;		// 0 = not created, 1 = being created, 2 = ready

static void lockLog()
{
	if (log_lockState != 2)
	{
		if (InterlockedCompareExchange(&log_lockState, 1, 0) == 0)
		{
			InitializeCriticalSection(&log_lock);
			InterlockedExchange(&log_lockState, 2);
		}
		else
			while (log_lockState != 2)
				Sleep(0);
	}
	EnterCriticalSection(&log_lock);
}

static void unlockLog()
{
	LeaveCriticalSection(&log_lock);
}

static bool isDir(const char* path)
{
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Builds base\sub1\sub2 into dir, creating both levels when missing; true when the folder exists afterwards.
static bool makeLogDir(const char* base, const char* sub1, const char* sub2, char* dir, size_t size)
{
	size_t len = strlen(base);
	const char* sep = len && base[len - 1] == '\\' ? "" : "\\";
	int n = _snprintf(dir, size, "%s%s%s", base, sep, sub1);
	if (n < 0 || (size_t)n >= size)
		return false;
	CreateDirectoryA(dir, NULL);
	n = _snprintf(dir, size, "%s%s%s\\%s", base, sep, sub1, sub2);
	if (n < 0 || (size_t)n >= size)
		return false;
	CreateDirectoryA(dir, NULL);
	return isDir(dir);
}

// <working directory>\PlugY\logs: inside the PlugY data folder of the install the game runs from.
static bool gameLogDir(char* dir, size_t size)
{
	char cwd[MAX_PATH];
	if (!_getcwd(cwd, MAX_PATH))
		return false;
	return makeLogDir(cwd, "PlugY", "logs", dir, size);
}

// %LOCALAPPDATA%\PlugY4Kolbot\logs: for installs whose game folder this user cannot write to.
static bool appDataLogDir(char* dir, size_t size)
{
	char base[MAX_PATH];
	DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
		return false;
	return makeLogDir(base, "PlugY4Kolbot", "logs", dir, size);
}

// Removes the folder's session logs that SessionLogSelectRemovals picks; returns how many went. Only names with the
// session-log shape are considered, and a log another running game still holds open fails DeleteFile and stays.
static int removeOldLogs(const char* dir)
{
	char path[LOG_PATH_SIZE];
	int n = _snprintf(path, sizeof(path), "%s\\PlugY_*.log", dir);
	if (n < 0 || (size_t)n >= sizeof(path))
		return 0;

	WIN32_FIND_DATAA found;
	HANDLE hFind = FindFirstFileA(path, &found);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;

	SessionLogEntry* entries = NULL;
	char (*names)[MAX_PATH] = NULL;
	int count = 0;
	int capacity = 0;
	bool complete = true;
	do
	{
		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !SessionLogIsOurs(found.cFileName))
			continue;
		if (count == capacity)
		{
			int grown = capacity ? capacity * 2 : 64;
			SessionLogEntry* moreEntries = (SessionLogEntry*)realloc(entries, grown * sizeof(*entries));
			if (moreEntries)
				entries = moreEntries;
			char (*moreNames)[MAX_PATH] = (char (*)[MAX_PATH])realloc(names, grown * sizeof(*names));
			if (moreNames)
				names = moreNames;
			if (!moreEntries || !moreNames)
			{
				complete = false;
				break;
			}
			capacity = grown;
		}
		entries[count].lastWrite = found.ftLastWriteTime;
		strcpy(names[count], found.cFileName);
		count++;
	} while (FindNextFileA(hFind, &found));
	FindClose(hFind);

	int removed = 0;
	// A partial list could make the count rule remove logs that are not the oldest, so it removes nothing.
	if (complete && count)
	{
		FILETIME now;
		GetSystemTimeAsFileTime(&now);
		SessionLogSelectRemovals(entries, count, now, SESSION_LOG_KEEP_DAYS, SESSION_LOG_KEEP_MAX);
		for (int i = 0; i < count; i++)
		{
			if (!entries[i].remove)
				continue;
			n = _snprintf(path, sizeof(path), "%s\\%s", dir, names[i]);
			if (n >= 0 && (size_t)n < sizeof(path) && DeleteFileA(path))
				removed++;
		}
	}
	free(entries);
	free(names);
	return removed;
}

static bool openSessionLog(const char* dir, const char* name, int* removed)
{
	*removed = removeOldLogs(dir);
	int n = _snprintf(log_file, sizeof(log_file), "%s\\%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(log_file))
	{
		log_file[0] = 0;
		return false;
	}
	fLogFile = fopen(log_file, "w");
	return fLogFile != NULL;
}

// The caller holds log_lock.
static void openLog()
{
	if (log_init || log_tried)
		return;
	log_tried = true;

	SYSTEMTIME started;
	GetLocalTime(&started);
	DWORD pid = GetCurrentProcessId();
	char name[64];
	if (!SessionLogName(&started, pid, name, sizeof(name)))
		return;

	char dir[LOG_PATH_SIZE];
	int removed = 0;
	if (!(gameLogDir(dir, sizeof(dir)) && openSessionLog(dir, name, &removed))
		&& !(appDataLogDir(dir, sizeof(dir)) && openSessionLog(dir, name, &removed)))
	{
		log_file[0] = 0;
		return;
	}

	log_init = true;
	log_lineStart = true;

	log_msg("<----------------------------------------------->\n"
			"\n"
			"\t\tPlugY4Kolbot\n"
			"\t\tbased on PlugY by Yohann\n"
			"\t\tversion %s\n"
			"\n"
			,PLUGY_VERSION);
	log_msg("Session started %04u-%02u-%02u %02u:%02u:%02u, process %lu\n"
			"Log file: %s\n",
			started.wYear, started.wMonth, started.wDay, started.wHour, started.wMinute, started.wSecond,
			(unsigned long)pid, log_file);
	if (removed)
		log_msg("Removed %d earlier log(s): older than %d days or beyond the newest %d.\n",
			removed, SESSION_LOG_KEEP_DAYS, SESSION_LOG_KEEP_MAX);
	log_msg("\n"
			"<---------- Starting Diablo II Plugin ---------->\n\n\n");
}

void log_close()
{
	lockLog();
	if (fLogFile)
	{
		fclose(fLogFile);
		fLogFile = NULL;
	}
	unlockLog();
}

void log_box( const char* pFormat, ... )
{
	// Large enough for messages naming two full save paths.
	char buffer[1024];
	va_list lArgs;
	va_start( lArgs, pFormat );
	_vsnprintf( buffer, sizeof(buffer) - 1, pFormat, lArgs );
	buffer[sizeof(buffer) - 1] = 0;
	va_end(lArgs);

	log_msg("%s\n", buffer);
	if (active_ShowErrorMessageBox)
		MessageBox(NULL, buffer, "PlugY4Kolbot", MB_OK|MB_ICONEXCLAMATION);
}

static void writeToLog(const char* text, size_t len, void* ctx)
{
	fwrite(text, 1, len, (FILE*)ctx);
}

void log_msg( const char* pFormat, ... )
{
	if (!active_logFile)
		return;

	lockLog();

	if( !log_init )
		openLog();

	if( log_init && fLogFile == NULL )
	{
		fLogFile = fopen( log_file, "a" );
		if( fLogFile == NULL )
			log_init = false;
	}

	if( fLogFile != NULL )
	{
		va_list lArgs;
		va_start( lArgs, pFormat );
		int len = _vscprintf( pFormat, lArgs );
		va_end(lArgs);

		char stackText[1024];
		char* text = len >= 0 && len < (int)sizeof(stackText) ? stackText : len > 0 ? (char*)malloc(len + 1) : NULL;
		if( text )
		{
			va_start( lArgs, pFormat );
			_vsnprintf( text, len + 1, pFormat, lArgs );
			va_end(lArgs);
			text[len] = 0;

			SYSTEMTIME now;
			GetLocalTime(&now);
			char stamp[SESSION_LOG_CLOCK_SIZE];
			SessionLogClock(&now, stamp);
			SessionLogStamp(text, stamp, &log_lineStart, writeToLog, fLogFile);
			fflush(fLogFile);

			if( text != stackText )
				free(text);
		}
	}

	unlockLog();
}

void d2_assert( bool pCondition, char* pMessage, char* pLocation, int pLineNbr )
{
	if( pCondition )
	{
		log_msg("\n"
			"*-----------------------*\n"
			"Assertion fail at line %d of %s :\n"
			"%s\n"
			"*-----------------------*\n",
			pLineNbr, pLocation, pMessage);
		if (active_ShowErrorMessageBox)
			MessageBox(NULL, pMessage, "Diablo 2 Error", MB_OK|MB_ICONEXCLAMATION);
		exit(1);
	}
}

/*================================= END OF FILE =================================*/

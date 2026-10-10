/*=================================================================
	File created by Yohann NICOLAS.
	Add support 1.13d by L'Autour.

	Patch D2gfx.dll to install PlugY.

=================================================================*/
#pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup")

#include "../common/VersionInfo.h"
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>

/* Exit codes, documented in the readme: the installer and scripts run the
   patcher with --silent and read only these. */
enum ePatchExit
{
	PATCH_EXIT_DONE = 0,		// the file was patched or restored
	PATCH_EXIT_ALREADY = 1,		// it already was
	PATCH_EXIT_UNEXPECTED = 2,	// its bytes are neither the original nor PlugY's
	PATCH_EXIT_NO_GAME = 3,		// the --dir folder can't be opened, or holds no supported game (Game.exe, or D2gfx.dll before 1.14)
	PATCH_EXIT_IN_USE = 4,		// Diablo II (or another program) has the file open
	PATCH_EXIT_ACCESS = 5,		// Windows refused the write, or administrator rights were not given
	PATCH_EXIT_CANCELLED = 6,	// the user answered No
	PATCH_EXIT_ERROR = 7		// a bad option, a failed write, or another error
};

struct PatchOptions
{
	bool unpatch;
	bool silent;	// no message boxes and no elevation: the exit code is the whole answer
	bool elevated;	// the copy relaunchElevated started, which openTarget does only after the user confirmed
	LPCWSTR dir;
};

DWORD loadCallerAddr;
DWORD loadCallerLen;
BYTE loadCallerNew[6] = { 0x90,0xE8,0x00,0x00,0x00,0x00 }; // Call load PlugY library
BYTE loadCallerOld[6] = { 0xFF,0x15,0x00,0x00,0x00,0x00 }; // CALL DWORD PTR DS:[<&KERNEL32.LoadLibrary>]

DWORD freeCallerAddr;
DWORD freeCallerLen;
BYTE freeCallerNew[6] = { 0x90,0xE8,0x00,0x00,0x00,0x00 }; // Call free PlugY library
BYTE freeCallerOld[6] = { 0xFF,0x15,0x00,0x00,0x00,0x00 }; // CALL DWORD PTR DS:[<&KERNEL32.FreeLibrary>]

DWORD loadDllAddr;
DWORD loadDllLen;
BYTE loadDll[] = {
	0xFF,0x74,0x24,0x04,			//PUSH DWORD PTR SS:[ESP+4]
	0xFF,0x15,0x40,0xC0,0xA7,0x6F,  //CALL DWORD PTR DS:[<&KERNEL32.LoadLibraryA>]      ; kernel32.LoadLibraryA
	0x50,							//PUSH EAX
	0x68,0x80,0xBE,0xA7,0x6F,		//PUSH d2gfx.6FA7BE80                               ; ASCII "PlugY.dll"
	0xFF,0x15,0x40,0xC0,0xA7,0x6F,  //CALL DWORD PTR DS:[<&KERNEL32.LoadLibraryA>]      ; kernel32.LoadLibraryA
	0xA3,0xFC,0xEF,0xA8,0x6F,		//MOV DWORD PTR DS:[6FA8EFFC],EAX
	0x85,0xC0,						//TEST EAX,EAX
	0x74,0x2B,						//JE SHORT d2gfx.6FA7BE33
	0x50,							//PUSH EAX
	0x68,0x90,0xBE,0xA7,0x6F,		//PUSH d2gfx.6FA7BE10                            ;Init String
	0x50,							//PUSH EAX
	0xFF,0x15,0x3C,0xC0,0xA7,0x6F,	//CALL DWORD PTR DS:[<&KERNEL32.GetProcAddress>]    ; kernel32.GetProcAddress
	0x85,0xC0,						//TEST EAX,EAX
	0x74,0x04,						//JE SHORT d2gfx.6FA7BDC1
	0x6A,0x00,						//PUSH 0
	0xEB,0x13,						//JMP SHORT d2gfx.6FA7BDC1
	0x68,0x10,0x27,0x00,0x00,		//PUSH 2710                                      ;Init Ordinal(10000)
	0xFF,0x74,0x24,0x04,			//PUSH DWORD PTR SS:[ESP+4]
	0xFF,0x15,0x3C,0xC0,0xA7,0x6F,	//CALL DWORD PTR DS:[<&KERNEL32.GetProcAddress>]    ; kernel32.GetProcAddress
	0x85,0xC0,						//TEST EAX,EAX
	0x74,0x02,						//JE SHORT d2gfx.6FA7BDC1
	0xFF,0xD0,						//CALL EAX
	0x58,							//POP EAX
	0x58,							//POP EAX
	0xC2,0x04,0x00,					//RETN 4
	0x00,0x00,0x00,0x00 };			//HANDLE var;

DWORD freeDllAddr;
DWORD freeDllLen;
BYTE freeDll[] = {
	0xFF,0x74,0x24,0x04,			//PUSH DWORD PTR SS:[ESP+4]
	0xFF,0x15,0x48,0xC0,0xA7,0x6F,  //CALL DWORD PTR DS:[<&KERNEL32.FreeLibrary>]       ; kernel32.FreeLibrary
	0x50,							//PUSH EAX
	0xA1,0xFC,0xEF,0xA8,0x6F,		//MOV EAX,DWORD PTR DS:[6FA8EFFC]
	0x85,0xC0,						//TEST EAX,EAX
	0x74,0x2D,						//JE SHORT d2gfx.6FA7BE74
	0x50,							//PUSH EAX
	0x68,0xA0,0xBE,0xA7,0x6F,		//PUSH d2gfx.6FA7BE20                            ;Release String
	0x50,							//PUSH EAX
	//0x33,0xC0,					//XOR EAX,EAX
	//0xA3,0xFC,0xEF,0xA8,0x6F,		//MOV DWORD PTR DS:[6FA8EFFC],EAX
	0xFF,0x15,0x3C,0xC0,0xA7,0x6F,	//CALL DWORD PTR DS:[<&KERNEL32.GetProcAdd>; kernel32.GetProcAddress
	0x85,0xC0,						//TEST EAX,EAX
	0x75,0x13,						//JNZ SHORT d2gfx.6FA7BDEF
	0x68,0x11,0x27,0x00,0x00,		//PUSH 2711                                      ;Release Ordinal(10001)
	0xFF,0x74,0x24,0x04,			//PUSH DWORD PTR SS:[ESP+4]
	0xFF,0x15,0x3C,0xC0,0xA7,0x6F,	//CALL DWORD PTR DS:[<&KERNEL32.GetProcAdd>; kernel32.GetProcAddress
	0x85,0xC0,						//TEST EAX,EAX
	0x74,0x02,						//JE SHORT d2gfx.6FA7BDEF
	0xFF,0xD0,						//CALL EAX
	0xFF,0x15,0x48,0xC0,0xA7,0x6F,  //CALL DWORD PTR DS:[<&KERNEL32.FreeLibrar>; kernel32.FreeLibrary
	0x58,							//POP EAX
	0xC2,0x04,0x00 };				//RETN 4

DWORD libraryNameAddr;
DWORD libraryNameLen;
LPCSTR libraryName = "PlugY.dll";

DWORD initNameAddr;
DWORD initNameLen;
LPCSTR initName = "_Init@4";

DWORD releaseNameAddr;
DWORD releaseNameLen;
LPCSTR releaseName = "_Release@0";

static bool silent = false;

// Shows nothing with --silent. Wide, and truncating: a message names full paths.
int msgBox(LPCWSTR boxName, UINT uType, LPCWSTR pFormat, ...)
{
	if (silent)
		return 0;
	wchar_t buffer[2048];
	va_list lArgs;
	va_start( lArgs, pFormat );
	_vsnwprintf_s( buffer, _countof(buffer), _TRUNCATE, pFormat, lArgs );
	va_end(lArgs);

	return MessageBoxW(NULL, buffer, boxName, uType);
}

static void errorText(DWORD error, wchar_t* out, DWORD maxChars)
{
	DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, out, maxChars, NULL);
	if (!n)
		_snwprintf_s(out, maxChars, _TRUNCATE, L"error %lu", error);
	else
		while (n > 0 && (out[n - 1] == L'\r' || out[n - 1] == L'\n' || out[n - 1] == L' ' || out[n - 1] == L'.'))
			out[--n] = 0;
}

static bool fileExists(LPCWSTR path)
{
	DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Before anything can load a DLL. The installer runs the patcher as
   administrator from the Diablo II folder, so the delay-loaded version.dll
   (not a KnownDLL) must come from System32, never from a copy planted there.
   The link also sets /DEPENDENTLOADFLAG for the static imports. */
static void restrictDllSearch()
{
	// Resolved at run time: Windows 7 without KB2533623 lacks it, and the patcher must still start there.
	typedef BOOL (WINAPI* SetDefaultDllDirectoriesFn)(DWORD);
	SetDefaultDllDirectoriesFn setDefaultDllDirectories =
		(SetDefaultDllDirectoriesFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
	if (!setDefaultDllDirectories || !setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
		SetDllDirectoryW(L"");	// at least drops the current folder from the search
}

static bool isProcessElevated()
{
	HANDLE token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation = { 0 };
	DWORD size = 0;
	bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
	CloseHandle(token);
	return elevated;
}

#ifdef RESTORE
static const wchar_t* USAGE = L"Usage: RestoreD2File.exe [--dir <Diablo II folder>] [--silent]";
#else
static const wchar_t* USAGE = L"Usage: PatchD2File.exe [-u] [--dir <Diablo II folder>] [--silent]";
#endif

static bool parseOptions(PatchOptions* o)
{
	memset(o, 0, sizeof(*o));
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);	// kept until exit: o->dir points into it
	if (!argv)
		return false;
	// First, so a bad option is reported without a box when --silent comes after it.
	for (int i = 1; i < argc; i++)
		if (!wcscmp(argv[i], L"--silent"))
			o->silent = silent = true;
	for (int i = 1; i < argc; i++)
	{
		if (!wcscmp(argv[i], L"-u"))	// RestoreD2File always removes, and accepts it too
			o->unpatch = true;
		else if (!wcscmp(argv[i], L"--silent"))
			continue;
		else if (!wcscmp(argv[i], L"--elevated"))
			o->elevated = true;
		else if (!wcscmp(argv[i], L"--dir"))
		{
			if (i + 1 >= argc || !argv[i + 1][0])
			{
				msgBox(L"PlugY4Kolbot", MB_OK | MB_ICONEXCLAMATION, L"--dir needs the Diablo II folder.\n\n%ls", USAGE);
				return false;
			}
			o->dir = argv[++i];
		}
		else
		{
			msgBox(L"PlugY4Kolbot", MB_OK | MB_ICONEXCLAMATION, L"Unknown option: %ls\n\n%ls", argv[i], USAGE);
			return false;
		}
	}
	return true;
}

/* The folder that holds Game.exe becomes the current folder, so every file
   below is opened by a ".\" path: GetFileVersionInfo with a bare file name
   would search this exe's folder first and could read another Game.exe.
   Without --dir: the current folder when it holds Game.exe, as upstream did,
   else this exe's own folder (a shortcut or an elevated start can begin elsewhere). */
static bool enterGameFolder(LPCWSTR dir)
{
	if (dir)
		return SetCurrentDirectoryW(dir) != 0;
	if (fileExists(L".\\Game.exe"))
		return true;
	wchar_t self[MAX_PATH];
	DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH);
	if (!n || n >= MAX_PATH)
		return false;
	wchar_t* slash = wcsrchr(self, L'\\');
	if (!slash)
		return false;
	slash[1] = 0;
	return SetCurrentDirectoryW(self) != 0;
}

/* Appends arg in quotes, so CommandLineToArgvW and the CRT give it back as is:
   backslashes before the closing quote are doubled. Paths hold no quote. */
static bool appendQuoted(wchar_t* cmd, size_t maxChars, LPCWSTR arg)
{
	size_t len = wcslen(cmd), argLen = wcslen(arg), slashes = 0;
	while (slashes < argLen && arg[argLen - 1 - slashes] == L'\\')
		slashes++;
	if (wcschr(arg, L'"') || len + argLen + slashes + 4 > maxChars)
		return false;
	cmd[len++] = L' ';
	cmd[len++] = L'"';
	wmemcpy(cmd + len, arg, argLen);
	len += argLen;
	for (size_t i = 0; i < slashes; i++)
		cmd[len++] = L'\\';
	cmd[len++] = L'"';
	cmd[len] = 0;
	return true;
}

/* Runs this exe again as administrator for the same file and waits for it;
   that copy shows the outcome. --dir carries the folder, so the copy does not
   depend on the folder Windows starts it in. Returns its exit code. */
static int relaunchElevated(const PatchOptions* o, LPCWSTR boxName, LPCWSTR folder, LPCWSTR targetPath)
{
	wchar_t self[MAX_PATH];
	DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH);
	wchar_t params[MAX_PATH + 64];
	wcscpy_s(params, o->unpatch ? L"-u --elevated --dir" : L"--elevated --dir");
	if (!n || n >= MAX_PATH || !appendQuoted(params, _countof(params), folder))
	{
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"The path of %ls is too long to restart as administrator.", targetPath);
		return PATCH_EXIT_ERROR;
	}
	SHELLEXECUTEINFOW sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.lpVerb = L"runas";
	sei.lpFile = self;
	sei.lpParameters = params;
	sei.lpDirectory = folder;
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei))
	{
		DWORD error = GetLastError();
		if (error == ERROR_CANCELLED)
		{
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"Windows needs administrator rights to change\n%ls\n\n"
				L"They were not given, so nothing was changed.", targetPath);
			return PATCH_EXIT_ACCESS;
		}
		wchar_t text[256];
		errorText(error, text, _countof(text));
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"Could not restart as administrator: %ls.\nNothing was changed.", text);
		return PATCH_EXIT_ERROR;
	}
	if (!sei.hProcess)
		return PATCH_EXIT_ERROR;
	WaitForSingleObject(sei.hProcess, INFINITE);
	DWORD code = PATCH_EXIT_ERROR;
	if (!GetExitCodeProcess(sei.hProcess, &code) || code > PATCH_EXIT_ERROR)
		code = PATCH_EXIT_ERROR;
	CloseHandle(sei.hProcess);
	return (int)code;
}

// Asks before changing the file; false when the user says No. The elevated copy never asks (see
// PatchOptions::elevated).
static bool confirmChange(const PatchOptions* o, LPCWSTR boxName, LPCWSTR targetPath)
{
	if (o->silent || o->elevated)
		return true;
	LPCWSTR slash = wcsrchr(targetPath, L'\\');
	if (msgBox(boxName, MB_YESNO | MB_ICONQUESTION,
			L"This program will modify\n%ls\n\n"
			L"Before you continue, you should back it up.\n\n"
			L"Do you want to modify %ls to %hs PlugY4Kolbot?",
			targetPath, slash ? slash + 1 : targetPath, o->unpatch ? "remove" : "install") == IDYES)
		return true;
	msgBox(boxName, MB_OK | MB_ICONASTERISK, L"No changes made.");
	return false;
}

// Opens the target for reading and writing, or explains why not and gives the exit code.
// confirmed: the user already said Yes; otherwise they are asked before a restart as administrator.
static FILE* openTarget(const PatchOptions* o, LPCWSTR boxName, LPCWSTR relativePath, LPCWSTR targetPath, LPCWSTR folder,
	bool confirmed, int* exitCode)
{
	DWORD attributes = GetFileAttributesW(relativePath);
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
	{
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
			L"%ls is read-only.\n\nClear its Read-only attribute (right-click it, Properties) and try again.", targetPath);
		*exitCode = PATCH_EXIT_ACCESS;
		return NULL;
	}
	HANDLE h = CreateFileW(relativePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();
		if (error == ERROR_ACCESS_DENIED && !o->silent && !o->elevated && !isProcessElevated())
			*exitCode = confirmed || confirmChange(o, boxName, targetPath)
				? relaunchElevated(o, boxName, folder, targetPath) : PATCH_EXIT_CANCELLED;
		else if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
		{
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"%ls is in use.\n\nClose Diablo II, and every D2Bot game started from this folder, then try again.", targetPath);
			*exitCode = PATCH_EXIT_IN_USE;
		}
		else if (error == ERROR_ACCESS_DENIED && isProcessElevated())
		{
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"Windows refused to let\n%ls\nbe changed, even with administrator rights (access denied).\n\n"
				L"Check the file's permissions, and that no security program (such as Controlled folder access) protects it.",
				targetPath);
			*exitCode = PATCH_EXIT_ACCESS;
		}
		else if (error == ERROR_ACCESS_DENIED)
		{
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"Windows refused to let\n%ls\nbe changed (access denied).\n\nRun this program as administrator.", targetPath);
			*exitCode = PATCH_EXIT_ACCESS;
		}
		else if (error == ERROR_FILE_NOT_FOUND)
		{
			// Before 1.14 the patch goes in D2gfx.dll, which a broken install can lack.
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"%ls was not found.", targetPath);
			*exitCode = PATCH_EXIT_NO_GAME;
		}
		else
		{
			wchar_t text[256];
			errorText(error, text, _countof(text));
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"Can't open %ls: %ls.", targetPath, text);
			*exitCode = PATCH_EXIT_ERROR;
		}
		return NULL;
	}
	int fd = _open_osfhandle((intptr_t)h, _O_RDWR | _O_BINARY);
	if (fd == -1)
	{
		CloseHandle(h);
		*exitCode = PATCH_EXIT_ERROR;
		return NULL;
	}
	FILE* file = _fdopen(fd, "r+b");
	if (!file)
	{
		_close(fd);
		*exitCode = PATCH_EXIT_ERROR;
	}
	return file;
}

int Align(int v)
{
	return v % 16 ? v + 16 - v % 16 : v;
}

////////////////////////////// EXPORTED FUNCTIONS //////////////////////////////

int Patch(FILE *targetFile, bool write)
{
	bool error = false;
	int ident = 0;
	BYTE buffer[100];
	BYTE zeros[100];
	memset(zeros, 0, sizeof(zeros));

	fseek(targetFile, loadCallerAddr, SEEK_SET);
	fread(buffer, sizeof(loadCallerNew), 1, targetFile);
	if (memcmp(buffer, loadCallerOld, sizeof(loadCallerNew)) != 0) error = true;
	if (memcmp(buffer, loadCallerNew, sizeof(loadCallerNew)) == 0) ident++;

	fseek(targetFile, freeCallerAddr, SEEK_SET);
	fread(buffer, sizeof(freeCallerNew), 1, targetFile);
	if (memcmp(buffer, freeCallerOld, sizeof(freeCallerNew)) != 0) error = true;
	if (memcmp(buffer, freeCallerNew, sizeof(freeCallerNew)) == 0) ident++;

	fseek(targetFile, loadDllAddr, SEEK_SET);
	fread(buffer, sizeof(loadDll), 1, targetFile);
	if (memcmp(buffer, zeros, sizeof(loadDll)) != 0) error = true;
	if (memcmp(buffer, loadDll, sizeof(loadDll)) == 0) ident++;

	fseek(targetFile, freeDllAddr, SEEK_SET);
	fread(buffer, sizeof(freeDll), 1, targetFile);
	if (memcmp(buffer, zeros, sizeof(freeDll)) != 0) error = true;
	if (memcmp(buffer, freeDll, sizeof(freeDll)) == 0) ident++;

	fseek(targetFile, initNameAddr, SEEK_SET);
	fread(buffer, initNameLen, 1, targetFile);
	if (memcmp(buffer, zeros, initNameLen) != 0) error = true;
	if (memcmp(buffer, initName, initNameLen) == 0) ident++;

	fseek(targetFile, releaseNameAddr, SEEK_SET);
	fread(buffer, releaseNameLen, 1, targetFile);
	if (memcmp(buffer, zeros, releaseNameLen) != 0) error = true;
	if (memcmp(buffer, releaseName, releaseNameLen) == 0) ident++;

	fseek(targetFile, libraryNameAddr, SEEK_SET);
	fread(buffer, libraryNameLen, 1, targetFile);
	if (memcmp(buffer, zeros, libraryNameLen) != 0) error = true;
	if (memcmp(buffer, libraryName, libraryNameLen) == 0) ident++;

	if (error)
		return ident == 7 ? 1 : 2;
	if (!write)
		return 0;

	fseek(targetFile, loadCallerAddr, SEEK_SET);
	fwrite(loadCallerNew, 6, 1, targetFile);

	fseek(targetFile, freeCallerAddr, SEEK_SET);
	fwrite(freeCallerNew, 6, 1, targetFile);

	fseek(targetFile, loadDllAddr, SEEK_SET);
	fwrite(loadDll, sizeof(loadDll), 1, targetFile);

	fseek(targetFile, freeDllAddr, SEEK_SET);
	fwrite(freeDll, sizeof(freeDll), 1, targetFile);

	fseek(targetFile, initNameAddr, SEEK_SET);
	fwrite(initName, initNameLen, 1, targetFile);

	fseek(targetFile, releaseNameAddr, SEEK_SET);
	fwrite(releaseName, releaseNameLen, 1, targetFile);

	fseek(targetFile, libraryNameAddr, SEEK_SET);
	fwrite(libraryName, libraryNameLen, 1, targetFile);

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

int Unpatch(FILE *targetFile, bool write)
{
	bool error = false;
	int ident = 0;
	BYTE buffer[100];
	BYTE zeros[100];
	memset(zeros, 0, sizeof(zeros));

	fseek(targetFile, loadCallerAddr, SEEK_SET);
	fread(buffer, 6, 1, targetFile);
	if (memcmp(buffer, loadCallerNew, 6) != 0) error = true;
	if (memcmp(buffer, loadCallerOld, 6) == 0) ident++;

	fseek(targetFile, freeCallerAddr, SEEK_SET);
	fread(buffer, 6, 1, targetFile);
	if (memcmp(buffer, freeCallerNew, 6) != 0) error = true;
	if (memcmp(buffer, freeCallerOld, 6) == 0) ident++;

	fseek(targetFile, loadDllAddr, SEEK_SET);
	fread(buffer, sizeof(loadDll), 1, targetFile);
	if (memcmp(buffer, loadDll, sizeof(loadDll)) != 0) error = true;
	if (memcmp(buffer, zeros, sizeof(loadDll)) == 0) ident++;

	fseek(targetFile, freeDllAddr, SEEK_SET);
	fread(buffer, sizeof(freeDll), 1, targetFile);
	if (memcmp(buffer, freeDll, sizeof(freeDll)) != 0) error = true;
	if (memcmp(buffer, zeros, sizeof(freeDll)) == 0) ident++;

	fseek(targetFile, initNameAddr, SEEK_SET);
	fread(buffer, initNameLen, 1, targetFile);
	if (memcmp(buffer, initName, initNameLen) != 0) error = true;
	if (memcmp(buffer, zeros, initNameLen) == 0) ident++;

	fseek(targetFile, releaseNameAddr, SEEK_SET);
	fread(buffer, releaseNameLen, 1, targetFile);
	if (memcmp(buffer, releaseName, releaseNameLen) != 0) error = true;
	if (memcmp(buffer, zeros, releaseNameLen) == 0) ident++;

	fseek(targetFile, libraryNameAddr, SEEK_SET);
	fread(buffer, libraryNameLen, 1, targetFile);
	if (memcmp(buffer, libraryName, libraryNameLen) != 0) error = true;
	if (memcmp(buffer, zeros, libraryNameLen) == 0) ident++;

	if (error)
		return ident == 7 ? 1 : 2;
	if (!write)
		return 0;

	fseek(targetFile, loadCallerAddr, SEEK_SET);
	fwrite(loadCallerOld, 6, 1, targetFile);

	fseek(targetFile, freeCallerAddr, SEEK_SET);
	fwrite(freeCallerOld, 6, 1, targetFile);

	fseek(targetFile, loadDllAddr, SEEK_SET);
	fwrite(zeros, sizeof(loadDll), 1, targetFile);

	fseek(targetFile, freeDllAddr, SEEK_SET);
	fwrite(zeros, sizeof(freeDll), 1, targetFile);

	fseek(targetFile, initNameAddr, SEEK_SET);
	fwrite(zeros, initNameLen, 1, targetFile);

	fseek(targetFile, releaseNameAddr, SEEK_SET);
	fwrite(zeros, releaseNameLen, 1, targetFile);

	fseek(targetFile, libraryNameAddr, SEEK_SET);
	fwrite(zeros, libraryNameLen, 1, targetFile);

	return 0;
}

int main()
{
	restrictDllSearch();

	PatchOptions o;
	if (!parseOptions(&o))
		return PATCH_EXIT_ERROR;
#ifdef RESTORE
	o.unpatch = true;
#endif
	bool unpatch = o.unpatch;
	LPCWSTR boxName = unpatch ? L"Uninstall PlugY4Kolbot" : L"Install PlugY4Kolbot";

	if (!enterGameFolder(o.dir))
	{
		// UAC's administrator token does not see drive letters mapped in the user's own session.
		if (o.elevated)
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"Running as administrator, this program can't open the folder\n%ls\n\n"
				L"A network drive mapped to a letter isn't visible to programs run as administrator. "
				L"Run it from the folder's network path (\\\\server\\share\\...) or from a local disk.", o.dir);
		else
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"The folder %ls can't be used.", o.dir ? o.dir : L"of this program");
		return o.dir ? PATCH_EXIT_NO_GAME : PATCH_EXIT_ERROR;
	}
	wchar_t folder[MAX_PATH];
	DWORD folderLen = GetCurrentDirectoryW(MAX_PATH, folder);
	if (!folderLen || folderLen >= MAX_PATH)
	{
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION, L"The path of the Diablo II folder is too long.");
		return PATCH_EXIT_ERROR;
	}

	if (!fileExists(L".\\Game.exe"))
	{
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
			L"Game.exe was not found in\n%ls\n\n"
			L"Run this program in your Diablo II folder, next to Game.exe.", folder);
		return PATCH_EXIT_NO_GAME;
	}
	int version = GetD2Version(".\\Game.exe");
	if (version < V107 || version > V114d)
	{
		msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
			L"Current version of LoD (%hs) isn't compatible with PlugY.\n\n"
			L"Please, install a patch between 1.09 and 1.13d or 1.14d.",
			GetVersionString(version));
		return PATCH_EXIT_NO_GAME;
	}
	LPCWSTR targetFilename = version > V113d ? L"Game.exe" : L"D2gfx.dll";
	LPCWSTR relativePath = version > V113d ? L".\\Game.exe" : L".\\D2gfx.dll";
	wchar_t targetPath[MAX_PATH + 16];
	_snwprintf_s(targetPath, _countof(targetPath), _TRUNCATE, L"%ls%ls%ls",
		folder, folder[folderLen - 1] == L'\\' ? L"" : L"\\", targetFilename);

	// Read first, without write access: a file already in the wanted state, or holding unexpected
	// bytes, is reported as such even when it is locked, read-only or protected. Sharing everything
	// lets the read through while Diablo II runs.
	FILE *targetFile = _wfsopen(relativePath, L"rb", _SH_DENYNO);
	if (!targetFile)
	{
		// openTarget says why (D2gfx.dll missing, access denied...) and gives the exit code.
		int openOutcome = PATCH_EXIT_ERROR;
		targetFile = openTarget(&o, boxName, relativePath, targetPath, folder, false, &openOutcome);
		if (!targetFile)
			return openOutcome;
	}

	// Get size
	loadCallerLen = sizeof(loadCallerNew);
	freeCallerLen = sizeof(freeCallerNew);
	loadDllLen = sizeof(loadDll);
	freeDllLen = sizeof(freeDll);
	libraryNameLen = strlen(libraryName) + 1;
	initNameLen = strlen(initName) + 1;
	releaseNameLen = strlen(releaseName) + 1;

	// Get Addr
	DWORD offsetPESignature;
	fseek(targetFile, 0x3C, SEEK_SET);
	fread(&offsetPESignature, sizeof(offsetPESignature), 1, targetFile);
	DWORD sizeOfCode;
	fseek(targetFile, offsetPESignature + 0x1C, SEEK_SET);
	fread(&sizeOfCode, sizeof(sizeOfCode), 1, targetFile);
	DWORD baseOfCode;
	fseek(targetFile, offsetPESignature + 0x2C, SEEK_SET);
	fread(&baseOfCode, sizeof(baseOfCode), 1, targetFile);
	DWORD imageBase;
	fseek(targetFile, offsetPESignature + 0x34, SEEK_SET);
	fread(&imageBase, sizeof(imageBase), 1, targetFile);
	DWORD sizeOfData;
	fseek(targetFile, offsetPESignature + 0x150, SEEK_SET);
	fread(&sizeOfData, sizeof(sizeOfData), 1, targetFile);
	DWORD baseOfData;
	fseek(targetFile, offsetPESignature + 0x154, SEEK_SET);
	fread(&baseOfData, sizeof(baseOfData), 1, targetFile);

	// Set Addr
	DWORD loadLibraryAddr = imageBase;
	DWORD freeLibraryAddr = imageBase;
	DWORD getProcAddressAddr = imageBase;
	DWORD handleAddr = imageBase + baseOfData + sizeOfData;

	loadCallerAddr = 0;
	freeCallerAddr = 0;
	loadDllAddr = Align(baseOfCode + sizeOfCode - 0x100);
	freeDllAddr = Align(loadDllAddr + sizeof(loadDll));
	libraryNameAddr = Align(freeDllAddr + sizeof(freeDll));
	initNameAddr = Align(libraryNameAddr + libraryNameLen);
	releaseNameAddr = Align(initNameAddr + initNameLen);

	switch (version)
	{
	case V107:
		loadCallerAddr += 0x3882;
		freeCallerAddr += 0x3A6C;
		loadLibraryAddr += 0xC038;
		freeLibraryAddr += 0xC040;
		getProcAddressAddr += 0xC034;
		break;
	case V108:
	case V109:
	case V109b:
	case V109d:
		loadCallerAddr += 0x389B;
		freeCallerAddr += 0x3A8C;
		loadLibraryAddr += 0xC03C;
		freeLibraryAddr += 0xC044;
		getProcAddressAddr += 0xC038;
		break;
	case V110:
		loadCallerAddr += 0x3870;
		freeCallerAddr += 0x3A6D;
		loadLibraryAddr += 0xC040;
		freeLibraryAddr += 0xC048;
		getProcAddressAddr += 0xC03C;
		break;
	case V111:
		loadCallerAddr += 0x8B23;
		freeCallerAddr += 0x8ACA;
		loadLibraryAddr += 0xD11C;
		freeLibraryAddr += 0xD12C;
		getProcAddressAddr += 0xD120;
		break;
	case V111b:
		loadCallerAddr += 0xB423;
		freeCallerAddr += 0xB3CA;
		loadLibraryAddr += 0xD11C;
		freeLibraryAddr += 0xD12C;
		getProcAddressAddr += 0xD120;
		break;
	case V112:
		loadCallerAddr += 0x8F63;
		freeCallerAddr += 0x8F0A;
		loadLibraryAddr += 0xD11C;
		freeLibraryAddr += 0xD12C;
		getProcAddressAddr += 0xD120;
		break;
	case V113c:
		loadCallerAddr += 0xB423;
		freeCallerAddr += 0xB3CA;
		loadLibraryAddr += 0xD11C;
		freeLibraryAddr += 0xD12C;
		getProcAddressAddr += 0xD120;
		break;
	case V113d:
		loadCallerAddr += 0xAA03;
		freeCallerAddr += 0xA9AA;
		loadLibraryAddr += 0xD11C;
		freeLibraryAddr += 0xD124;
		getProcAddressAddr += 0xD120;
		break;
	case V114a:
		loadCallerAddr += 0x1BCB;// Load advapi.dll
		freeCallerAddr += 0xF375;// Free dbghelp.dll
		loadLibraryAddr += 0x2CD118;
		freeLibraryAddr += 0x2CD120;
		getProcAddressAddr += 0x2CD11C;
		break;
	case V114b:
	case V114c:
		loadCallerAddr += 0x1BCB;// Load advapi.dll
		freeCallerAddr += 0x6F75;// Free dbghelp.dll
		loadLibraryAddr += 0x2CD11C;
		freeLibraryAddr += 0x2CD124;
		getProcAddressAddr += 0x2CD120;
		break;
	case V114d:
		loadCallerAddr += 0x621C;// Load advapi.dll
		freeCallerAddr += 0xB514;// Free dbghelp.dll
		loadLibraryAddr += 0x2CC144;
		freeLibraryAddr += 0x2CC14C;
		getProcAddressAddr += 0x2CC148;
		break;
	default:
		fclose(targetFile);
		return PATCH_EXIT_NO_GAME;
	}

	*(DWORD*)&loadCallerNew[2] = loadDllAddr - (loadCallerAddr + sizeof(loadCallerNew));
	*(DWORD*)&loadCallerOld[2] = loadLibraryAddr;
	*(DWORD*)&freeCallerNew[2] = freeDllAddr - (freeCallerAddr + sizeof(freeCallerNew));
	*(DWORD*)&freeCallerOld[2] = freeLibraryAddr;
	*(DWORD*)&loadDll[6] = loadLibraryAddr;
	*(DWORD*)&loadDll[12] = imageBase + libraryNameAddr;
	*(DWORD*)&loadDll[18] = loadLibraryAddr;
	*(DWORD*)&loadDll[23] = handleAddr;
	*(DWORD*)&loadDll[33] = imageBase + initNameAddr;
	*(DWORD*)&loadDll[40] = getProcAddressAddr;
	*(DWORD*)&loadDll[63] = getProcAddressAddr;
	*(DWORD*)&freeDll[6] = freeLibraryAddr;
	*(DWORD*)&freeDll[12] = handleAddr;
	*(DWORD*)&freeDll[22] = imageBase + releaseNameAddr;
	*(DWORD*)&freeDll[29] = getProcAddressAddr;
	*(DWORD*)&freeDll[48] = getProcAddressAddr;
	*(DWORD*)&freeDll[60] = freeLibraryAddr;

	// Patch / Unpatch: 0 written (to be written, when only checking), 1 already in that state,
	// 2 neither state (nothing written).
	int res = unpatch ? Unpatch(targetFile, false) : Patch(targetFile, false);
	fclose(targetFile);
	if (res == 0)
	{
		if (!confirmChange(&o, boxName, targetPath))
			return PATCH_EXIT_CANCELLED;

		int openOutcome = PATCH_EXIT_ERROR;
		targetFile = openTarget(&o, boxName, relativePath, targetPath, folder, true, &openOutcome);
		if (!targetFile)
			return openOutcome;
		// Checked again under the write handle: the file may have changed since it was read.
		res = unpatch ? Unpatch(targetFile, true) : Patch(targetFile, true);

		// Only a write can fail here; the box must not say "was modified" when it did.
		bool writeFailed = res == 0 && (fflush(targetFile) != 0 || ferror(targetFile));
		if (fclose(targetFile) != 0 && res == 0)
			writeFailed = true;
		if (writeFailed)
		{
			msgBox(boxName, MB_OK | MB_ICONEXCLAMATION,
				L"Writing %ls failed, so it may be only partly changed.\n"
				L"Restore it from your backup.", targetPath);
			return PATCH_EXIT_ERROR;
		}
	}

	if (res == 0)
		msgBox(boxName, MB_OK | MB_ICONASTERISK,
			L"%ls was modified.\n"
			L"PlugY4Kolbot %hs.",
			targetPath, unpatch ? "is removed" : "is installed");
	else if (res == 1)
		msgBox(boxName, MB_OK | MB_ICONASTERISK,
			L"PlugY4Kolbot already %hs.",
			unpatch ? "uninstalled" : "installed");
	else
		msgBox(boxName, MB_OK | MB_ICONASTERISK,
			L"File contents in %ls aren't what PlugY4Kolbot expects.\n"
			L"Unable to %hs PlugY4Kolbot.",
			targetPath, unpatch ? "uninstall" : "install");

	return res == 0 ? PATCH_EXIT_DONE : res == 1 ? PATCH_EXIT_ALREADY : PATCH_EXIT_UNEXPECTED;
}

///////////////////////// END OF FILE ///////////////////////
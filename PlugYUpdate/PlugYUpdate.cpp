/*=================================================================
	PlugY4Kolbot updater (PlugY4KolbotUpdater.exe).

	Brings the PlugY4Kolbot install it sits in up to the newest GitHub
	release. Zip layout (PlugY.dll beside the updater): verifies the
	release zip, extracts it with Windows' tar.exe and replaces the
	files one by one, keeping the user's PlugY.ini and backing up
	everything it replaces. Installer layout (an install the NSIS
	installer made, found by its folder in the installer's records):
	verifies the release installer and runs it silently for that
	install. printUsage has the options.

	Every path is handled as UTF-16: release files have non-ANSI names.
	Only the work folder (%TEMP%, or %ProgramData%\PlugY4Kolbot for an
	elevated run) reaches the ANSI Commons API and tar.exe, so it is kept
	ANSI-representable.
=================================================================*/

#include "UpdaterLogic.h"
#include "../Commons/UpdateCheck.h"
#include "../Commons/UpdateDownload.h"
#include <shellapi.h>
#include <shlobj.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")

/* The installer's records, under HKEY_LOCAL_MACHINE\SOFTWARE in the 32-bit view (or the test-only
   --registry-base). REGKEY names only the install that ran last; INSTALLS lists every install by its
   folder, with its Diablo II folder. */
#define INSTALLER_REGKEY L"PlugY, The Survival Kit"
#define INSTALLER_INSTALLS_KEY L"PlugY4Kolbot\\Installs"
#define REGISTRY_BASE_MAX 256
#define RELEASES_URL "https://github.com/" PLUGY_UPDATE_REPO "/releases"
#define BACKUP_FOLDER L"PlugY4Kolbot-backup"
#define BACKUPS_KEPT 3
#define MAX_BACKUPS_LISTED 256
#define MAX_CREATED_DIRS 32
#define NEW_SUFFIX UPDATER_NEW_SUFFIX
#define OLD_SUFFIX L".old"
// Release assets are under 1 MB today; the caps only stop a runaway download.
#define PACKAGE_MAX_BYTES (64 * 1024 * 1024)
#define SUMS_MAX_BYTES (64 * 1024)
#define LISTING_MAX_BYTES (1024 * 1024)
#define MAX_STAGED_FILES 256
#define NOTES_LINES 15
#define TAR_TIMEOUT_MS (5 * 60 * 1000)
// What the updater fetches gets installed, possibly as administrator: a certificate whose revocation cannot be checked is refused.
#define UPDATER_HTTP_FLAGS UPDATE_HTTP_CHECK_REVOCATION

typedef struct
{
	bool help;
	bool check;
	bool yes;
	bool prerelease;
	bool registryOff;
	bool elevated;
	wchar_t registryBase[REGISTRY_BASE_MAX];	// test-only: the installer's records under HKCU\<this> instead
	wchar_t workBase[MAX_PATH];	// test-only: the elevated work folder's code path, rooted here instead of %ProgramData%
	wchar_t target[MAX_PATH];
	wchar_t fromZip[MAX_PATH];
	wchar_t fromInstaller[MAX_PATH];
	wchar_t sums[MAX_PATH];
	char version[UPDATE_TAG_MAX];
	char expectVersion[UPDATE_TAG_MAX];	// internal (--expect-version): set by the copy that asked for elevation
} Options;

typedef struct
{
	bool installerLayout;
	wchar_t dir[MAX_PATH];		// zip layout: everything; installer layout: the install folder (PlugY.exe, PlugY.ini)
	wchar_t dllDir[MAX_PATH];	// the folder of PlugY.dll; the same as dir for the zip layout
	wchar_t dll[MAX_PATH];
	wchar_t exe[MAX_PATH];
	wchar_t ini[MAX_PATH];
	wchar_t installArgs[2 * MAX_PATH + 32];	// installer layout: the installer's command line for this install
} Target;

typedef struct
{
	wchar_t rel[MAX_PATH];
	bool existed;	// it replaced a file, which the backup holds
	bool isSelf;	// it replaced the running updater
	bool done;
} PlanEntry;

enum eVersionRead
{
	VERSION_READ_OK = 0,		// a ProductVersion string was read (semver or not)
	VERSION_READ_NONE,			// the file has no ProductVersion
	VERSION_READ_MISSING,
	VERSION_READ_IN_USE,		// opened without read sharing by another process
	VERSION_READ_ERROR
};

static UpdateRelease release;	// ~16 KB
static PlanEntry plan[MAX_STAGED_FILES];
static DWORD nbPlan = 0;
static wchar_t selfPath[MAX_PATH];
static wchar_t workBase[MAX_PATH];	// %TEMP%, or the target for an elevated run
static wchar_t workDir[MAX_PATH];	// "" until created; always ANSI-representable
static wchar_t workDirLong[MAX_PATH];	// the same folder by its long name
static char workDirA[MAX_PATH];
static HANDLE heldPackage = INVALID_HANDLE_VALUE;	// the verified package, open without write sharing
static HANDLE heldWorkDir = INVALID_HANDLE_VALUE;	// the work folder, open without delete sharing
static wchar_t createdDirs[MAX_CREATED_DIRS][MAX_PATH];	// folders the apply step created in the target
static DWORD nbCreatedDirs = 0;
static bool relaunched = false;

// --from-zip or --from-installer: the package is a local file, "" for a download.
static const wchar_t* localPackage(const Options* o)
{
	return o->fromZip[0] ? o->fromZip : o->fromInstaller;
}


/*============================= Output ============================*/

static void emit(HANDLE h, const wchar_t* text)
{
	DWORD len = (DWORD)wcslen(text);
	DWORD written;
	DWORD mode;
	if (GetConsoleMode(h, &mode))
	{
		WriteConsoleW(h, text, len, &written, NULL);
		return;
	}
	// Redirected: encode with the console's output code page, which a reading shell decodes with.
	UINT cp = GetConsoleOutputCP();
	char buffer[16384];
	int n = WideCharToMultiByte(cp ? cp : CP_UTF8, 0, text, (int)len, buffer, sizeof(buffer), NULL, NULL);
	if (n > 0)
		WriteFile(h, buffer, (DWORD)n, &written, NULL);
}

static void vprint(HANDLE h, const wchar_t* prefix, const wchar_t* format, va_list args)
{
	wchar_t text[4096];
	int n = _snwprintf(text, 4096, L"%ls", prefix);
	if (n < 0)
		n = 0;
	_vsnwprintf(text + n, 4096 - n, format, args);
	text[4095] = 0;
	emit(h, text);
}

static void say(const wchar_t* format, ...)
{
	va_list args;
	va_start(args, format);
	vprint(GetStdHandle(STD_OUTPUT_HANDLE), L"", format, args);
	va_end(args);
}

static void fail(const wchar_t* format, ...)
{
	va_list args;
	va_start(args, format);
	vprint(GetStdHandle(STD_ERROR_HANDLE), L"ERROR: ", format, args);
	va_end(args);
}

// FormatMessage text of a Win32 error, in a static buffer.
static const wchar_t* errorText(DWORD error)
{
	static wchar_t text[512];
	DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, text, 512, NULL);
	while (n && (text[n - 1] == L'\r' || text[n - 1] == L'\n' || text[n - 1] == L' ' || text[n - 1] == L'.'))
		n--;
	text[n] = 0;
	if (!n)
		_snwprintf(text, 512, L"Windows error %lu", error);
	return text;
}

static void printUsage()
{
	say(L"PlugY4Kolbot updater %hs\n"
		L"\n"
		L"Updates the PlugY4Kolbot install this program is in to the newest release on\n"
		L"GitHub, keeping your PlugY.ini. For an install the installer made, it downloads\n"
		L"the new installer and runs it for that install, without its pages. Otherwise it\n"
		L"replaces the files in its own folder (the one that holds PlugY.dll), backing up\n"
		L"each one under PlugY4Kolbot-backup\\ first.\n"
		L"\n"
		L"Usage: PlugY4KolbotUpdater.exe [--check] [--yes] [--prerelease]\n"
		L"\n"
		L"  --check        Only report whether an update is available.\n"
		L"  --yes          Install without asking first.\n"
		L"  --prerelease   Also offer release candidates and betas. The same as\n"
		L"                 UpdateChannel=prerelease in the [GENERAL] section of PlugY.ini.\n"
		L"  --help         Show this text.\n"
		L"\n"
		L"Test options:\n"
		L"  --target <dir>   Update <dir> (a folder that holds PlugY.dll) instead.\n"
		L"  --registry-off   Ignore the installer's registry entries.\n"
		L"  --registry-base <key>  Read the installer's registry entries under\n"
		L"                   HKEY_CURRENT_USER\\<key> instead of HKEY_LOCAL_MACHINE\\SOFTWARE.\n"
		L"  --from-zip <zip> --sums <SHA256SUMS.txt> --version <V>\n"
		L"                   Install a local release zip instead of downloading one.\n"
		L"  --from-installer <exe> --sums <SHA256SUMS.txt> --version <V>\n"
		L"                   Run a local release installer instead (installer installs).\n"
		L"  --work-base <dir>  Make the work folder as an elevated run does, but in\n"
		L"                   <dir>\\PlugY4Kolbot instead of %%ProgramData%%\\PlugY4Kolbot.\n"
		L"\n"
		L"Exit codes: 0 updated or up to date, 1 error, 2 update available (--check),\n"
		L"            3 files in use, 4 cancelled.\n", PLUGY_VERSION);
}


/*============================= Paths =============================*/

static bool pathJoin(wchar_t* out, const wchar_t* dir, const wchar_t* name)
{
	size_t len = wcslen(dir);
	bool separator = len && dir[len - 1] != L'\\' && dir[len - 1] != L'/';
	int n = _snwprintf(out, MAX_PATH, separator ? L"%ls\\%ls" : L"%ls%ls", dir, name);
	if (n < 0 || n >= MAX_PATH)
	{
		out[0] = 0;
		return false;
	}
	return true;
}

static bool pathAppend(wchar_t* out, const wchar_t* path, const wchar_t* suffix)
{
	int n = _snwprintf(out, MAX_PATH, L"%ls%ls", path, suffix);
	if (n < 0 || n >= MAX_PATH)
	{
		out[0] = 0;
		return false;
	}
	return true;
}

// Absolute, long-name form without a trailing separator, so paths compare reliably.
static bool canonicalPath(const wchar_t* in, wchar_t* out)
{
	wchar_t full[MAX_PATH];
	DWORD n = GetFullPathNameW(in, MAX_PATH, full, NULL);
	if (!n || n >= MAX_PATH)
		return false;
	n = GetLongPathNameW(full, out, MAX_PATH);
	if (!n || n >= MAX_PATH)
		wcscpy(out, full);
	size_t len = wcslen(out);
	while (len > 3 && (out[len - 1] == L'\\' || out[len - 1] == L'/'))
		out[--len] = 0;
	return true;
}

static void parentDir(const wchar_t* path, wchar_t* out)
{
	wcscpy(out, path);
	wchar_t* slash = wcsrchr(out, L'\\');
	if (slash)
		*slash = 0;
}

static bool fileExists(const wchar_t* path)
{
	DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dirExists(const wchar_t* path)
{
	DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void clearReadOnlyOnHandle(HANDLE h)
{
	FILE_BASIC_INFO basic;
	if (GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) && (basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
	{
		basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
		if (!basic.FileAttributes)
			basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
		SetFileInformationByHandle(h, FileBasicInfo, &basic, sizeof(basic));
	}
}

// Clears the read-only attribute of the entry itself: a symlink's target is never changed.
static void clearReadOnly(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	clearReadOnlyOnHandle(h);
	CloseHandle(h);
}

static bool isReparsePoint(const wchar_t* path)
{
	DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* Opens every existing folder of rel below base as the entry itself
   (FILE_FLAG_OPEN_REPARSE_POINT). False when one is a junction or symlink,
   or cannot be inspected: an elevated run must never write through a link
   planted inside the install. Folders that do not exist yet pass; the
   apply step creates them itself. */
static bool noLinksBelow(const wchar_t* base, const wchar_t* rel)
{
	wchar_t path[MAX_PATH];
	if (!pathJoin(path, base, rel))
		return false;
	size_t baseLen = wcslen(base);
	for (wchar_t* p = path + baseLen + 1; *p; p++)
	{
		if (*p != L'\\')
			continue;
		*p = 0;
		HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
		DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
		bool link;
		if (h != INVALID_HANDLE_VALUE)
		{
			FILE_ATTRIBUTE_TAG_INFO tag;
			link = !GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag))
				|| (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
			CloseHandle(h);
		}
		else
			link = error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
		*p = L'\\';
		if (link)
			return false;
		if (h == INVALID_HANDLE_VALUE)
			break;
	}
	return true;
}

/* Creates the folders of rel (a relative file path) below base. With record,
   the folders it made are remembered so a rollback can remove them again. */
static bool createParents(const wchar_t* base, const wchar_t* rel, bool record)
{
	wchar_t path[MAX_PATH];
	if (!pathJoin(path, base, rel))
		return false;
	size_t baseLen = wcslen(base);
	for (wchar_t* p = path + baseLen + 1; *p; p++)
	{
		if (*p != L'\\')
			continue;
		*p = 0;
		bool made = CreateDirectoryW(path, NULL) != 0;
		bool ok = made || GetLastError() == ERROR_ALREADY_EXISTS;
		if (made && record && nbCreatedDirs < MAX_CREATED_DIRS)
			wcscpy(createdDirs[nbCreatedDirs++], path);
		*p = L'\\';
		if (!ok)
			return false;
	}
	return true;
}

/* Deletes one file, empty folder or link through a handle opened on the entry
   itself (FILE_FLAG_OPEN_REPARSE_POINT), so a link's target is never touched,
   not even to clear its read-only attribute. */
static bool deleteEntryNoFollow(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	clearReadOnlyOnHandle(h);
	FILE_DISPOSITION_INFO disposition = { TRUE };
	bool ok = SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
	CloseHandle(h);
	return ok;
}

// Deletes dir and its content. Links (junctions, symlinks) are removed, never followed.
static bool deleteTree(const wchar_t* dir)
{
	wchar_t pattern[MAX_PATH];
	if (!pathJoin(pattern, dir, L"*"))
		return false;
	bool ok = true;
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern, &fd);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
				continue;
			wchar_t child[MAX_PATH];
			if (!pathJoin(child, dir, fd.cFileName))
			{
				ok = false;
				continue;
			}
			// Attributes are read again: a folder swapped for a link since the listing is not entered.
			bool folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				&& !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !isReparsePoint(child);
			ok = (folder ? deleteTree(child) : deleteEntryNoFollow(child)) && ok;
		} while (FindNextFileW(hFind, &fd));
		FindClose(hFind);
	}
	return deleteEntryNoFollow(dir) && ok;
}

// deleteTree, refused unless dir lies strictly below base.
static bool deleteTreeBelow(const wchar_t* base, const wchar_t* dir)
{
	if (!UpdaterIsPathInside(base, dir) || UpdaterSamePath(base, dir))
		return false;
	DWORD attrs = GetFileAttributesW(dir);
	if (attrs == INVALID_FILE_ATTRIBUTES)
		return true;
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
		return deleteEntryNoFollow(dir);
	return deleteTree(dir);
}

/* FlushFileBuffers on a closed file, so its data is on disk before it is moved
   into place. Full sharing: an antivirus scan holding the new file must not fail the update. */
static bool flushFile(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	bool ok = FlushFileBuffers(h) != 0;
	DWORD error = GetLastError();
	CloseHandle(h);
	SetLastError(error);
	return ok;
}

static char* readSmallFile(const wchar_t* path, DWORD maxBytes, DWORD* len)
{
	*len = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;
	LARGE_INTEGER size;
	char* data = NULL;
	if (GetFileSizeEx(h, &size) && size.QuadPart <= maxBytes)
	{
		data = (char*)malloc((size_t)size.QuadPart + 1);
		DWORD got = 0;
		if (data && ReadFile(h, data, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart)
		{
			data[got] = 0;
			*len = got;
		}
		else
		{
			free(data);
			data = NULL;
		}
	}
	CloseHandle(h);
	return data;
}


/*============================ Versions ===========================*/

// The ProductVersion string of a file, as ASCII (anything else becomes '?').
static int readProductVersion(const wchar_t* path, char* out, DWORD outSize)
{
	out[0] = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();
		if (error == ERROR_SHARING_VIOLATION)
			return VERSION_READ_IN_USE;
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
			return VERSION_READ_MISSING;
		return VERSION_READ_ERROR;
	}
	CloseHandle(h);

	DWORD unused = 0;
	DWORD size = GetFileVersionInfoSizeW(path, &unused);
	if (!size)
		return VERSION_READ_NONE;
	BYTE* data = (BYTE*)malloc(size);
	if (!data)
		return VERSION_READ_ERROR;
	int result = VERSION_READ_NONE;
	if (GetFileVersionInfoW(path, 0, size, data))
	{
		// The declared translations first, then the blocks PlugY binaries have used.
		DWORD candidates[16];
		DWORD nb = 0;
		WORD* translation = NULL;
		UINT translationLen = 0;
		if (VerQueryValueW(data, L"\\VarFileInfo\\Translation", (LPVOID*)&translation, &translationLen) && translation)
			for (UINT i = 0; i + 1 < translationLen / sizeof(WORD) && nb < 13; i += 2)
				candidates[nb++] = ((DWORD)translation[i] << 16) | translation[i + 1];
		candidates[nb++] = 0x040904B0;
		candidates[nb++] = 0x040C04B0;
		candidates[nb++] = 0x040904E4;
		for (DWORD i = 0; i < nb && result != VERSION_READ_OK; i++)
		{
			wchar_t query[64];
			_snwprintf(query, 64, L"\\StringFileInfo\\%08lx\\ProductVersion", candidates[i]);
			query[63] = 0;
			wchar_t* value = NULL;
			UINT valueLen = 0;
			if (!VerQueryValueW(data, query, (LPVOID*)&value, &valueLen) || !value || !valueLen)
				continue;
			DWORD o = 0;
			for (UINT k = 0; k < valueLen && value[k] && o + 1 < outSize; k++)
				out[o++] = value[k] < 0x80 ? (char)value[k] : '?';
			while (o && out[o - 1] == ' ')
				o--;
			out[o] = 0;
			char* s = out;
			while (*s == ' ')
				s++;
			memmove(out, s, strlen(s) + 1);
			if (out[0])
				result = VERSION_READ_OK;
		}
	}
	free(data);
	return result;
}


/*============================= Target ============================*/

static bool readRegString(HKEY key, const wchar_t* name, wchar_t* out, DWORD maxChars)
{
	DWORD type = 0;
	DWORD bytes = (maxChars - 1) * sizeof(wchar_t);
	if (RegQueryValueExW(key, name, NULL, &type, (BYTE*)out, &bytes) != ERROR_SUCCESS || type != REG_SZ)
		return false;
	out[bytes / sizeof(wchar_t)] = 0;
	return out[0] != 0;
}

// sub is INSTALLER_REGKEY or INSTALLER_INSTALLS_KEY.
static bool openInstallerKey(const Options* o, const wchar_t* sub, HKEY* key)
{
	wchar_t path[REGISTRY_BASE_MAX + 64];
	int n = o->registryBase[0] ? _snwprintf(path, REGISTRY_BASE_MAX + 64, L"%ls\\%ls", o->registryBase, sub)
		: _snwprintf(path, REGISTRY_BASE_MAX + 64, L"SOFTWARE\\%ls", sub);
	if (n < 0 || n >= REGISTRY_BASE_MAX + 64)
		return false;
	if (o->registryBase[0])
		return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, key) == ERROR_SUCCESS;
	return RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, key) == ERROR_SUCCESS;
}

/* The install that ran last (InstallPath) and its Diablo II folder (PlugYDllPath), as stored (raw*)
   and in canonical form. */
static bool readInstallerRegistry(const Options* o, wchar_t* rawInstall, wchar_t* rawD2, wchar_t* installPath, wchar_t* dllPath)
{
	HKEY key;
	if (!openInstallerKey(o, INSTALLER_REGKEY, &key))
		return false;
	bool ok = readRegString(key, L"InstallPath", rawInstall, MAX_PATH) && canonicalPath(rawInstall, installPath)
		&& readRegString(key, L"PlugYDllPath", rawD2, MAX_PATH) && canonicalPath(rawD2, dllPath);
	RegCloseKey(key);
	return ok;
}

/* The Installs entry of the install in selfDir whose Diablo II folder holds PlugY.dll: both folders
   as stored (raw*), and the Diablo II folder in canonical form. */
static bool findListedInstall(const Options* o, const wchar_t* selfDir, wchar_t* rawInstall, wchar_t* rawD2, wchar_t* d2Dir)
{
	HKEY key;
	if (!openInstallerKey(o, INSTALLER_INSTALLS_KEY, &key))
		return false;
	bool found = false;
	for (DWORD i = 0; !found; i++)
	{
		DWORD nameChars = MAX_PATH;
		DWORD type = 0;
		DWORD bytes = (MAX_PATH - 1) * sizeof(wchar_t);
		LONG res = RegEnumValueW(key, i, rawInstall, &nameChars, NULL, &type, (BYTE*)rawD2, &bytes);
		// ERROR_MORE_DATA: a name or folder too long to be a usable path.
		if (res == ERROR_MORE_DATA || (res == ERROR_SUCCESS && type != REG_SZ))
			continue;
		if (res != ERROR_SUCCESS)
			break;
		rawD2[bytes / sizeof(wchar_t)] = 0;
		wchar_t installDir[MAX_PATH];
		wchar_t dll[MAX_PATH];
		found = canonicalPath(rawInstall, installDir) && UpdaterSamePath(installDir, selfDir)
			&& canonicalPath(rawD2, d2Dir) && pathJoin(dll, d2Dir, L"PlugY.dll") && fileExists(dll);
	}
	RegCloseKey(key);
	return found;
}

static bool fillTargetFiles(Target* t)
{
	return pathJoin(t->dll, t->dllDir, L"PlugY.dll") && pathJoin(t->exe, t->dir, L"PlugY.exe")
		&& pathJoin(t->ini, t->dir, L"PlugY.ini");
}

static bool detectTarget(const Options* o, Target* t)
{
	memset(t, 0, sizeof(*t));
	wchar_t selfDir[MAX_PATH];
	parentDir(selfPath, selfDir);
	if (o->target[0])
	{
		wcscpy(t->dir, o->target);
		wcscpy(t->dllDir, o->target);
		if (!fillTargetFiles(t) || !fileExists(t->dll))
		{
			fail(L"--target %ls holds no PlugY.dll.\n", o->target);
			return false;
		}
		return true;
	}
	wcscpy(t->dir, selfDir);
	wcscpy(t->dllDir, selfDir);
	if (!fillTargetFiles(t))
	{
		fail(L"The path %ls is too long.\n", selfDir);
		return false;
	}

	wchar_t rawInstall[MAX_PATH];
	wchar_t rawD2[MAX_PATH];
	wchar_t d2Dir[MAX_PATH];
	bool listed = !o->registryOff && findListedInstall(o, selfDir, rawInstall, rawD2, d2Dir);
	bool beside = fileExists(t->dll);
	bool registered = false;
	if (!listed && !beside && !o->registryOff)
	{
		wchar_t installPath[MAX_PATH];
		wchar_t dll[MAX_PATH];
		registered = readInstallerRegistry(o, rawInstall, rawD2, installPath, d2Dir) && UpdaterSamePath(installPath, selfDir)
			&& pathJoin(dll, d2Dir, L"PlugY.dll") && fileExists(dll);
	}
	int layout = UpdaterChooseLayout(listed, beside, registered);
	if (layout == UPDATER_LAYOUT_ZIP)
		return true;
	if (layout == UPDATER_LAYOUT_INSTALLER)
	{
		t->installerLayout = true;
		wcscpy(t->dllDir, d2Dir);
		if (!fillTargetFiles(t))
		{
			fail(L"The path %ls is too long.\n", d2Dir);
			return false;
		}
		if (!UpdaterBuildInstallerArgs(rawD2, rawInstall, t->installArgs, sizeof(t->installArgs) / sizeof(wchar_t)))
		{
			fail(L"The installer's record of this install (%ls, with Diablo II in %ls) cannot be passed back to\n"
				L"the installer. Run the new installer yourself.\n", rawInstall, rawD2);
			return false;
		}
		return true;
	}
	fail(L"PlugY.dll was not found next to the updater, in %ls.\n"
		L"Put PlugY4KolbotUpdater.exe in the folder that holds PlugY.dll (your Diablo II\n"
		L"folder) and run it again.\n", selfDir);
	return false;
}


/*=========================== Elevation ===========================*/

// Creates and deletes a file in dir; 0 when that worked, else the error.
static DWORD probeWrite(const wchar_t* dir)
{
	wchar_t name[64];
	_snwprintf(name, 64, L"PlugY4Kolbot-write-test-%lu.tmp", GetCurrentProcessId());
	name[63] = 0;
	wchar_t path[MAX_PATH];
	if (!pathJoin(path, dir, name))
		return ERROR_FILENAME_EXCED_RANGE;
	HANDLE h = CreateFileW(path, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return GetLastError();
	CloseHandle(h);
	return 0;
}

/* Runs this exe again as administrator with the same (absolute) options plus
   --elevated, and waits for it. confirmedVersion (online runs) becomes
   --expect-version, so the copy installs only what the user said yes to. */
static int relaunchElevated(const Options* o, const char* confirmedVersion)
{
	wchar_t params[4096];
	UpdaterRelaunchArgs args;
	memset(&args, 0, sizeof(args));
	args.yes = o->yes;
	args.prerelease = o->prerelease;
	args.registryOff = o->registryOff;
	args.registryBase = o->registryBase;
	args.workBase = o->workBase;
	args.target = o->target;
	args.fromZip = o->fromZip;
	args.fromInstaller = o->fromInstaller;
	args.sums = o->sums;
	args.version = o->version;
	args.expectVersion = localPackage(o)[0] ? NULL : confirmedVersion;
	if (!UpdaterBuildRelaunchArgs(&args, params, 4096))
	{
		fail(L"The command line is too long to restart the updater as administrator.\n");
		return UPDATER_EXIT_ERROR;
	}
	say(L"Windows needs administrator permission to change these files. Restarting the\n"
		L"updater as administrator; it continues in a new window.\n");
	SHELLEXECUTEINFOW sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.lpVerb = L"runas";
	sei.lpFile = selfPath;
	sei.lpParameters = params;
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei))
	{
		DWORD error = GetLastError();
		int outcome = UpdaterRelaunchFailedOutcome(error);
		if (outcome == UPDATER_UAC_CANCELLED)
			fail(L"Administrator permission was not given, so nothing was changed.\n");
		else
			fail(L"Could not restart the updater as administrator: %ls.\n", errorText(error));
		return UpdaterExitCode(outcome);
	}
	if (!sei.hProcess)
	{
		fail(L"Could not follow the updater running as administrator.\n");
		return UPDATER_EXIT_ERROR;
	}
	relaunched = true;
	WaitForSingleObject(sei.hProcess, INFINITE);
	DWORD code = UPDATER_EXIT_ERROR;
	GetExitCodeProcess(sei.hProcess, &code);
	CloseHandle(sei.hProcess);
	say(L"The updater running as administrator finished (exit code %lu).\n", code);
	return UpdaterChildExitCode(code);
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

/* Serializes updaters of one install across sessions; other installs are not
   blocked. Access denied on the Global name means an elevated updater made
   it, so that counts as running; Local is only the fallback when Global
   objects cannot be made at all. NULL: another updater runs. */
static HANDLE acquireUpdaterMutex(const Target* t)
{
	wchar_t name[128];
	HANDLE mutex = NULL;
	DWORD error = ERROR_INVALID_NAME;
	if (UpdaterMutexName(t->dir, true, name, 128))
	{
		mutex = CreateMutexW(NULL, TRUE, name);
		error = mutex ? 0 : GetLastError();
	}
	if (!mutex && error != ERROR_ACCESS_DENIED && UpdaterMutexName(t->dir, false, name, 128))
		mutex = CreateMutexW(NULL, TRUE, name);
	if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(mutex);
		mutex = NULL;
	}
	return mutex;
}

static void releaseUpdaterMutex(HANDLE* mutex)
{
	if (*mutex)
	{
		ReleaseMutex(*mutex);
		CloseHandle(*mutex);
		*mutex = NULL;
	}
}


/*=========================== Work folder =========================*/

// ANSI form of path, or of its 8.3 name when the ANSI code page cannot hold it.
static bool ansiPath(const wchar_t* path, char* out, wchar_t* usable)
{
	BOOL lossy = FALSE;
	int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, out, MAX_PATH, NULL, &lossy);
	if (n > 0 && !lossy)
	{
		wcscpy(usable, path);
		return true;
	}
	wchar_t shortPath[MAX_PATH];
	DWORD len = GetShortPathNameW(path, shortPath, MAX_PATH);
	if (!len || len >= MAX_PATH)
		return false;
	lossy = FALSE;
	n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, shortPath, -1, out, MAX_PATH, NULL, &lossy);
	if (n <= 0 || lossy)
		return false;
	wcscpy(usable, shortPath);
	return true;
}

/* Opens dir as the entry itself: true only for a plain folder (not a link)
   owned by Administrators or SYSTEM. A folder a standard user made first,
   even under %ProgramData%, could still be theirs to change. */
static bool isAdminOwnedFolder(const wchar_t* dir)
{
	HANDLE h = CreateFileW(dir, READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	FILE_ATTRIBUTE_TAG_INFO tag;
	PSID owner = NULL;
	PSECURITY_DESCRIPTOR sd = NULL;
	bool ok = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag))
		&& (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		&& GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, &sd) == ERROR_SUCCESS
		&& UpdaterIsAdminOwner(owner);
	if (sd)
		LocalFree(sd);
	CloseHandle(h);
	return ok;
}

/* The base of an elevated run's work folder: %ProgramData%\PlugY4Kolbot (or
   <testBase>\PlugY4Kolbot), created with the admin-only DACL at
   CreateDirectory time. The user's %TEMP% or a user-writable install would
   let the verified files be swapped between their check and their use. */
static bool prepareAdminWorkBase(const wchar_t* testBase, const SECURITY_ATTRIBUTES* sa)
{
	wchar_t base[MAX_PATH];
	if (testBase)
		wcscpy(base, testBase);
	else if (SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, base) != S_OK)
	{
		fail(L"The %%ProgramData%% folder could not be found.\n");
		return false;
	}
	if (!pathJoin(workBase, base, L"PlugY4Kolbot"))
	{
		fail(L"The path %ls is too long for a work folder.\n", base);
		return false;
	}
	if (!CreateDirectoryW(workBase, (LPSECURITY_ATTRIBUTES)sa) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		fail(L"Could not create %ls: %ls.\n", workBase, errorText(GetLastError()));
		return false;
	}
	// Checked whether it was made just now or long ago: a pre-made one is trusted only if an administrator owns it.
	if (!isAdminOwnedFolder(workBase))
	{
		fail(L"%ls is a link or is not owned by Administrators, so it cannot hold the update safely.\n"
			L"Delete that folder and run the updater again.\n", workBase);
		return false;
	}
	return true;
}

/* Creates the work folder, refusing one that exists, and holds it open (no
   delete sharing) until cleanWorkDir: elevated, <admin base>\update-<V>-<hex>;
   otherwise %TEMP%\PlugY4Kolbot-update-<V>-<hex>. */
static bool createWorkDir(const char* version, const wchar_t* testBase, bool secured)
{
	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	if (secured && !UpdaterAdminOnlySecurity(&sa))
	{
		fail(L"Could not build the work folder's permissions: %ls.\n", errorText(GetLastError()));
		return false;
	}
	bool ok = true;
	wchar_t raw[MAX_PATH];
	if (secured)
		ok = prepareAdminWorkBase(testBase, &sa);
	else
	{
		DWORD n = GetTempPathW(MAX_PATH, raw);
		if (!n || n >= MAX_PATH || !canonicalPath(raw, workBase))
		{
			fail(L"The temp folder (%%TEMP%%) is not usable.\n");
			ok = false;
		}
	}
	BYTE random[UPDATER_WORK_RANDOM_HEX / 2];
	if (ok && BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
	{
		fail(L"Windows could not supply random numbers for the work folder name.\n");
		ok = false;
	}
	wchar_t name[MAX_PATH];
	wchar_t dir[MAX_PATH];
	if (ok)
	{
		int n = _snwprintf(name, MAX_PATH, L"%ls%hs-", secured ? UPDATER_ELEVATED_WORK_PREFIX : UPDATER_WORK_PREFIX, version);
		for (int i = 0; n > 0 && i < (int)sizeof(random); i++)
			n += _snwprintf(name + n, MAX_PATH - n, L"%02x", random[i]);
		name[MAX_PATH - 1] = 0;
		if (!pathJoin(dir, workBase, name))
		{
			fail(L"The path %ls is too long for a work folder.\n", workBase);
			ok = false;
		}
	}
	if (ok && !CreateDirectoryW(dir, secured ? &sa : NULL))
	{
		fail(L"Could not create the work folder %ls: %ls.\n", dir, errorText(GetLastError()));
		ok = false;
	}
	if (sa.lpSecurityDescriptor)
		LocalFree(sa.lpSecurityDescriptor);
	if (!ok)
		return false;
	// workDirLong is what cleanWorkDir deletes; workDir may become the 8.3 form below.
	wcscpy(workDirLong, dir);
	heldWorkDir = CreateFileW(dir, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	FILE_ATTRIBUTE_TAG_INFO tag;
	if (heldWorkDir == INVALID_HANDLE_VALUE || !GetFileInformationByHandleEx(heldWorkDir, FileAttributeTagInfo, &tag, sizeof(tag))
		|| (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
	{
		fail(L"The work folder %ls could not be locked or is a link; stopping.\n", dir);
		return false;
	}
	if (!ansiPath(dir, workDirA, workDir))
	{
		fail(L"The folder %ls has characters that tar.exe cannot handle.\n%ls\n", workBase, secured
			? L"Make sure %ProgramData% has a plain name." : L"Point the TEMP environment variable to a folder with a plain name and run again.");
		return false;
	}
	return true;
}

static void cleanWorkDir()
{
	if (heldPackage != INVALID_HANDLE_VALUE)
	{
		CloseHandle(heldPackage);
		heldPackage = INVALID_HANDLE_VALUE;
	}
	if (heldWorkDir != INVALID_HANDLE_VALUE)
	{
		CloseHandle(heldWorkDir);
		heldWorkDir = INVALID_HANDLE_VALUE;
	}
	if (workDirLong[0] && !deleteTreeBelow(workBase, workDirLong))
		say(L"Note: could not delete the work folder %ls.\n", workDirLong);
	workDir[0] = 0;
	workDirLong[0] = 0;
}

// Deletes the *.PlugY4Kolbot-new copies an interrupted run left in the install.
static void removeLeftovers(const Target* t)
{
	const wchar_t* subs[] = { L"", L"PlugY" };
	for (int s = 0; s < 2; s++)
	{
		wchar_t dir[MAX_PATH];
		wchar_t pattern[MAX_PATH];
		if (!(subs[s][0] ? pathJoin(dir, t->dir, subs[s]) : (wcscpy(dir, t->dir) != NULL)) || !pathJoin(pattern, dir, L"*"))
			continue;
		// A PlugY folder that is a link is never listed: its files are outside the install.
		if (subs[s][0] && !noLinksBelow(t->dir, L"PlugY\\x"))
			continue;
		WIN32_FIND_DATAW fd;
		HANDLE hFind = FindFirstFileW(pattern, &fd);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			wchar_t path[MAX_PATH];
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !pathJoin(path, dir, fd.cFileName))
				continue;
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && UpdaterIsLeftoverTemp(fd.cFileName))
				deleteEntryNoFollow(path);
		} while (FindNextFileW(hFind, &fd));
		FindClose(hFind);
	}
}

static bool workFile(const char* name, wchar_t* path, char* pathA)
{
	int n = _snprintf(pathA, MAX_PATH, "%s\\%s", workDirA, name);
	if (n < 0 || n >= MAX_PATH)
		return false;
	return MultiByteToWideChar(CP_ACP, 0, pathA, -1, path, MAX_PATH) > 0;
}


/*======================== Download + verify =======================*/

static void describeResult(int result, const UpdateHttpInfo* info, wchar_t* out, DWORD maxChars)
{
	switch (result)
	{
	case UPDATE_NETWORK_ERROR:
		_snwprintf(out, maxChars, L"GitHub could not be reached (%ls). Check the internet connection", errorText(info->winError));
		break;
	case UPDATE_HTTP_ERROR:
		if (info->httpStatus == 404)
			_snwprintf(out, maxChars, L"GitHub answered HTTP 404 (not found)");
		else if (info->httpStatus == 403 || info->httpStatus == 429)
			_snwprintf(out, maxChars, L"GitHub refused the request (HTTP %lu), most likely its hourly limit; try again later", info->httpStatus);
		else
			_snwprintf(out, maxChars, L"GitHub answered HTTP %lu", info->httpStatus);
		break;
	case UPDATE_TOO_LARGE:
		_snwprintf(out, maxChars, L"the download was larger than expected");
		break;
	case UPDATE_BAD_RESPONSE:
		_snwprintf(out, maxChars, L"GitHub's answer could not be read");
		break;
	case UPDATE_FILE_ERROR:
		_snwprintf(out, maxChars, L"the file could not be written (%ls)", errorText(info->winError));
		break;
	default:
		_snwprintf(out, maxChars, L"%hs", UpdateResultName(result));
		break;
	}
	out[maxChars - 1] = 0;
}

static const UpdateAsset* findAsset(const char* name)
{
	for (DWORD i = 0; i < release.nbAssets; i++)
		if (!strcmp(release.assets[i].name, name))
			return &release.assets[i];
	return NULL;
}

static bool download(const UpdateAsset* asset, const char* pathA, DWORD maxBytes)
{
	say(L"Downloading %hs...\n", asset->name);
	UpdateHttpInfo info;
	int result = UpdateDownloadToFileEx(asset->url, pathA, maxBytes, UPDATER_HTTP_FLAGS, &info);
	if (result == UPDATE_OK)
		return true;
	wchar_t text[512];
	describeResult(result, &info, text, 512);
	fail(L"Could not download %hs: %ls.\n", asset->name, text);
	return false;
}

/* Puts the package and SHA256SUMS.txt into the work folder (downloaded, or
   copied for --from-zip / --from-installer), then requires the package's
   SHA-256 to match SHA256SUMS.txt and the digest GitHub reported, when it
   reported one. */
static bool fetchAndVerify(const Options* o, const char* packageName, wchar_t* packagePath)
{
	char packageA[MAX_PATH];
	char sumsA[MAX_PATH];
	wchar_t sumsPath[MAX_PATH];
	if (!workFile(packageName, packagePath, packageA) || !workFile("SHA256SUMS.txt", sumsPath, sumsA))
	{
		fail(L"The work folder path is too long.\n");
		return false;
	}
	const char* expectedDigest = "";
	if (localPackage(o)[0])
	{
		if (!CopyFileW(localPackage(o), packagePath, FALSE) || !CopyFileW(o->sums, sumsPath, FALSE))
		{
			fail(L"Could not copy %ls and --sums into the work folder: %ls.\n", localPackage(o), errorText(GetLastError()));
			return false;
		}
	}
	else
	{
		const UpdateAsset* package = findAsset(packageName);
		const UpdateAsset* sums = findAsset("SHA256SUMS.txt");
		if (!package || !sums)
		{
			fail(L"Release %hs has no %hs asset%ls. Update by hand from %hs\n", release.tag,
				package ? "SHA256SUMS.txt" : packageName,
				release.assetsOmitted ? L" (or GitHub listed too many assets)" : L"",
				release.pageUrl[0] ? release.pageUrl : RELEASES_URL);
			return false;
		}
		if (!download(sums, sumsA, SUMS_MAX_BYTES) || !download(package, packageA, PACKAGE_MAX_BYTES))
			return false;
		expectedDigest = package->sha256;
	}

	DWORD sumsLen = 0;
	char* sumsText = readSmallFile(sumsPath, SUMS_MAX_BYTES, &sumsLen);
	char listed[UPDATE_SHA256_HEX];
	bool found = sumsText && UpdateFindSha256Sum(sumsText, sumsLen, packageName, listed);
	free(sumsText);
	if (!found)
	{
		fail(L"SHA256SUMS.txt has no entry for %hs, so it cannot be verified. Nothing was changed.\n", packageName);
		return false;
	}
	/* Held open from before the hash until cleanWorkDir: no write or delete
	   sharing, so the file tar.exe or the installer gets is the one hashed. The
	   entry itself is opened, and must not be a link. */
	heldPackage = CreateFileW(packagePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	FILE_ATTRIBUTE_TAG_INFO tag;
	if (heldPackage == INVALID_HANDLE_VALUE || !GetFileInformationByHandleEx(heldPackage, FileAttributeTagInfo, &tag, sizeof(tag))
		|| (tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
	{
		fail(L"Could not lock %hs for verification. Nothing was changed.\n", packageName);
		return false;
	}
	char actual[UPDATE_SHA256_HEX];
	if (!UpdateSha256File(packageA, actual))
	{
		fail(L"Could not compute the SHA-256 of %hs. Nothing was changed.\n", packageName);
		return false;
	}
	if (strcmp(actual, listed) || (expectedDigest[0] && strcmp(actual, expectedDigest)))
	{
		fail(L"%hs is damaged or was altered: its SHA-256 is %hs, but %hs %hs.\n"
			L"Nothing was changed.\n", packageName, actual,
			strcmp(actual, listed) ? "SHA256SUMS.txt lists" : "GitHub reports",
			strcmp(actual, listed) ? listed : expectedDigest);
		return false;
	}
	say(L"Verified %hs (SHA-256 %hs).\n", packageName, actual);
	return true;
}


/*============================ Extraction ==========================*/

// %SystemRoot%\System32\tar.exe (Windows 10 1803 and later). Sysnative covers a 32-bit view without it.
static bool findTar(wchar_t* tar)
{
	wchar_t dir[MAX_PATH];
	UINT n = GetSystemDirectoryW(dir, MAX_PATH);
	if (n && n < MAX_PATH && pathJoin(tar, dir, L"tar.exe") && fileExists(tar))
		return true;
	n = GetWindowsDirectoryW(dir, MAX_PATH);
	return n && n < MAX_PATH && pathJoin(tar, dir, L"Sysnative\\tar.exe") && fileExists(tar);
}

/* Runs tar.exe (an absolute path, no shell) with args, its output going to
   outputPath. Returns its exit code, or -1 when it could not run or timed out. */
static int runTar(const wchar_t* tar, const wchar_t* const* args, int nbArgs, const wchar_t* outputPath)
{
	wchar_t cmd[2048] = L"";
	bool ok = UpdaterAppendArg(cmd, 2048, tar);
	for (int i = 0; ok && i < nbArgs; i++)
		ok = UpdaterAppendArg(cmd, 2048, args[i]);
	if (!ok)
		return -1;
	SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
	HANDLE output = CreateFileW(outputPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (output == INVALID_HANDLE_VALUE)
		return -1;
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = output;
	si.hStdError = output;
	PROCESS_INFORMATION pi;
	BOOL started = CreateProcessW(tar, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, workDir, &si, &pi);
	CloseHandle(output);
	if (!started)
		return -1;
	CloseHandle(pi.hThread);
	int code = -1;
	if (WaitForSingleObject(pi.hProcess, TAR_TIMEOUT_MS) == WAIT_OBJECT_0)
	{
		DWORD exitCode;
		if (GetExitCodeProcess(pi.hProcess, &exitCode))
			code = (int)exitCode;
	}
	else
		TerminateProcess(pi.hProcess, 1);
	CloseHandle(pi.hProcess);
	return code;
}

static void printTarOutput(const wchar_t* path)
{
	DWORD len;
	char* text = readSmallFile(path, 16384, &len);
	if (text && len)
	{
		wchar_t wide[16385];
		int n = MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 16385);
		if (n > 0)
			say(L"%ls\n", wide);
	}
	free(text);
}

/* Every name tar lists must stay inside the extraction folder. tar prints
   names the code page cannot hold with '?', which UpdaterIsSafeRelativePath
   accepts: only the structure matters here. */
static bool checkListing(const wchar_t* listPath)
{
	DWORD len;
	char* text = readSmallFile(listPath, LISTING_MAX_BYTES, &len);
	if (!text)
	{
		fail(L"Could not read the archive listing.\n");
		return false;
	}
	bool ok = true;
	DWORD entries = 0;
	char* line = text;
	while (ok && *line)
	{
		char* end = strchr(line, '\n');
		if (end)
			*end = 0;
		size_t n = strlen(line);
		if (n && line[n - 1] == '\r')
			line[--n] = 0;
		if (n)
		{
			wchar_t wide[MAX_PATH];
			if (!MultiByteToWideChar(CP_ACP, 0, line, -1, wide, MAX_PATH) || !UpdaterIsSafeRelativePath(wide))
			{
				fail(L"The archive holds an unsafe path (%hs). Nothing was changed.\n", line);
				ok = false;
			}
			entries++;
		}
		if (!end)
			break;
		line = end + 1;
	}
	free(text);
	if (ok && !entries)
	{
		fail(L"The archive is empty. Nothing was changed.\n");
		ok = false;
	}
	return ok;
}

// Lists the staged files (relative paths) into plan[]; links and unsafe names are refused.
static bool collectStaged(const wchar_t* staging, const wchar_t* rel)
{
	wchar_t dir[MAX_PATH];
	wchar_t pattern[MAX_PATH];
	bool fits = rel[0] ? pathJoin(dir, staging, rel) : (wcscpy(dir, staging) != NULL);
	if (!fits || !pathJoin(pattern, dir, L"*"))
	{
		fail(L"A staged path is too long.\n");
		return false;
	}
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return true;
	bool ok = true;
	do
	{
		if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
			continue;
		wchar_t childRel[MAX_PATH];
		bool childFits = rel[0] ? pathJoin(childRel, rel, fd.cFileName) : (wcscpy(childRel, fd.cFileName) != NULL);
		if (!childFits)
		{
			fail(L"A staged path is too long.\n");
			ok = false;
		}
		else if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !UpdaterIsSafeRelativePath(childRel))
		{
			fail(L"The archive holds a link or an unsafe name (%ls). Nothing was changed.\n", childRel);
			ok = false;
		}
		else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			ok = collectStaged(staging, childRel);
		else if (nbPlan >= MAX_STAGED_FILES)
		{
			fail(L"The archive holds more than %d files.\n", MAX_STAGED_FILES);
			ok = false;
		}
		else
		{
			memset(&plan[nbPlan], 0, sizeof(plan[nbPlan]));
			wcscpy(plan[nbPlan].rel, childRel);
			nbPlan++;
		}
	} while (ok && FindNextFileW(hFind, &fd));
	FindClose(hFind);
	return ok;
}

static bool extractAndValidate(const wchar_t* zipPath, const char* version, const char* pageUrl, const Target* t, wchar_t* staging)
{
	wchar_t tar[MAX_PATH];
	if (!findTar(tar))
	{
		fail(L"This Windows has no tar.exe (it comes with Windows 10 version 1803 and later),\n"
			L"so the update cannot be unpacked automatically. To update by hand: download\n"
			L"PlugY4Kolbot_v%hs.zip from %hs, close every Diablo II game,\n"
			L"and extract it into %ls, keeping your PlugY.ini.\n", version, pageUrl, t->dir);
		return false;
	}
	wchar_t listPath[MAX_PATH];
	wchar_t logPath[MAX_PATH];
	if (!pathJoin(staging, workDir, L"staging") || !pathJoin(listPath, workDir, L"listing.txt")
		|| !pathJoin(logPath, workDir, L"tar.log"))
	{
		fail(L"The work folder path is too long.\n");
		return false;
	}
	const wchar_t* listArgs[] = { L"-tf", zipPath };
	if (runTar(tar, listArgs, 2, listPath) != 0)
	{
		fail(L"tar.exe could not read the archive:\n");
		printTarOutput(listPath);
		return false;
	}
	if (!checkListing(listPath))
		return false;
	if (!CreateDirectoryW(staging, NULL))
	{
		fail(L"Could not create %ls: %ls.\n", staging, errorText(GetLastError()));
		return false;
	}
	say(L"Extracting...\n");
	const wchar_t* extractArgs[] = { L"-xf", zipPath, L"-C", staging };
	if (runTar(tar, extractArgs, 4, logPath) != 0)
	{
		fail(L"tar.exe could not extract the archive:\n");
		printTarOutput(logPath);
		return false;
	}

	nbPlan = 0;
	if (!collectStaged(staging, L""))
		return false;
	wchar_t stagedDll[MAX_PATH];
	wchar_t stagedData[MAX_PATH];
	if (!pathJoin(stagedDll, staging, L"PlugY.dll") || !pathJoin(stagedData, staging, L"PlugY")
		|| !fileExists(stagedDll) || !dirExists(stagedData))
	{
		fail(L"The archive is not a PlugY4Kolbot release (PlugY.dll or the PlugY folder is missing).\n");
		return false;
	}
	char stagedVersion[UPDATE_TAG_MAX];
	if (readProductVersion(stagedDll, stagedVersion, UPDATE_TAG_MAX) != VERSION_READ_OK || strcmp(stagedVersion, version))
	{
		fail(L"The archive's PlugY.dll is version \"%hs\", not %hs.\n", stagedVersion, version);
		return false;
	}
	return true;
}


/*============================== Apply =============================*/

// Copies src next to dest, then moves it over dest. The running updater cannot be
// overwritten but can be renamed, so it is moved to <dest>.old first.
static bool installFile(const wchar_t* src, const wchar_t* dest, bool isSelf, DWORD* error)
{
	wchar_t temp[MAX_PATH];
	wchar_t old[MAX_PATH];
	if (!pathAppend(temp, dest, NEW_SUFFIX) || !pathAppend(old, dest, OLD_SUFFIX))
	{
		*error = ERROR_FILENAME_EXCED_RANGE;
		return false;
	}
	if (!CopyFileW(src, temp, FALSE) || !flushFile(temp))
	{
		*error = GetLastError();
		DeleteFileW(temp);
		return false;
	}
	clearReadOnly(dest);
	if (isSelf)
	{
		if (!MoveFileExW(dest, old, MOVEFILE_REPLACE_EXISTING))
		{
			*error = GetLastError();
			DeleteFileW(temp);
			return false;
		}
		if (!MoveFileExW(temp, dest, MOVEFILE_WRITE_THROUGH))
		{
			*error = GetLastError();
			MoveFileExW(old, dest, 0);
			DeleteFileW(temp);
			return false;
		}
		return true;
	}
	if (!MoveFileExW(temp, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		*error = GetLastError();
		DeleteFileW(temp);
		return false;
	}
	return true;
}

/* Undoes every done entry: replaced files come back from the backup, new ones
   are deleted, then the folders the apply step created are removed if empty. */
static DWORD rollback(const Target* t, const wchar_t* backupDir)
{
	DWORD failures = 0;
	for (DWORD i = nbPlan; i-- > 0;)
	{
		PlanEntry* e = &plan[i];
		if (!e->done)
			continue;
		wchar_t dest[MAX_PATH];
		wchar_t saved[MAX_PATH];
		wchar_t old[MAX_PATH];
		DWORD error = 0;
		bool ok = pathJoin(dest, t->dir, e->rel) && noLinksBelow(t->dir, e->rel);
		// The running updater moves back from .old; its backup copy is the fallback.
		bool restored = ok && e->isSelf && pathAppend(old, dest, OLD_SUFFIX) && MoveFileExW(old, dest, MOVEFILE_REPLACE_EXISTING);
		if (ok && !restored)
			ok = e->existed ? pathJoin(saved, backupDir, e->rel) && installFile(saved, dest, false, &error)
				: DeleteFileW(dest) != 0;
		if (ok)
			e->done = false;
		else
		{
			failures++;
			fail(L"Could not restore %ls.\n", e->rel);
		}
	}
	// RemoveDirectoryW only removes an empty folder, and these are never links: this run made them.
	while (nbCreatedDirs)
		RemoveDirectoryW(createdDirs[--nbCreatedDirs]);
	return failures;
}

/* Rolls back and reports. When everything came back the install is as it was,
   so this run's own backup goes too, with the backup root if this run made it. */
static void rollbackAndReport(const Target* t, const wchar_t* backupDir, bool backupRootCreated)
{
	DWORD failures = rollback(t, backupDir);
	if (failures)
	{
		fail(L"%lu file(s) could not be restored; copy them back from %ls.\n", failures, backupDir);
		return;
	}
	say(L"Every file was restored; the install is as it was.\n");
	wchar_t root[MAX_PATH];
	if (backupDir[0] && pathJoin(root, t->dir, BACKUP_FOLDER) && deleteTreeBelow(root, backupDir) && backupRootCreated)
		RemoveDirectoryW(root);
}

/* Keeps the BACKUPS_KEPT newest backup folders by creation time, never this
   run's own (ownBackupDir); only plain folders named like ours are deleted. */
static void pruneBackups(const Target* t, const wchar_t* ownBackupDir)
{
	wchar_t root[MAX_PATH];
	wchar_t pattern[MAX_PATH];
	if (!pathJoin(root, t->dir, BACKUP_FOLDER) || !pathJoin(pattern, root, L"*")
		|| !noLinksBelow(t->dir, BACKUP_FOLDER L"\\x"))
		return;
	const wchar_t* ownName = wcsrchr(ownBackupDir, L'\\');
	ownName = ownName ? ownName + 1 : ownBackupDir;
	static wchar_t names[MAX_BACKUPS_LISTED][MAX_PATH];
	static const wchar_t* pointers[MAX_BACKUPS_LISTED];
	static ULONGLONG createdAt[MAX_BACKUPS_LISTED];
	static bool doomed[MAX_BACKUPS_LISTED];
	DWORD count = 0;
	DWORD own = MAX_BACKUPS_LISTED;
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			&& UpdaterParseBackupName(fd.cFileName, NULL))
		{
			wcscpy(names[count], fd.cFileName);
			pointers[count] = names[count];
			createdAt[count] = ((ULONGLONG)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
			if (ownName[0] && !wcscmp(fd.cFileName, ownName))
				own = count;
			count++;
		}
	} while (count < MAX_BACKUPS_LISTED && FindNextFileW(hFind, &fd));
	FindClose(hFind);
	DWORD removed = 0;
	if (UpdaterSelectOldBackups(pointers, createdAt, count, BACKUPS_KEPT, own < count ? own : count, doomed))
		for (DWORD i = 0; i < count; i++)
		{
			wchar_t path[MAX_PATH];
			if (doomed[i] && pathJoin(path, root, names[i]) && deleteTreeBelow(root, path))
				removed++;
		}
	if (removed)
		say(L"  Older backups removed: %lu (the %d newest are kept)\n", removed, BACKUPS_KEPT);
}

static bool createBackupDir(const Target* t, const char* installed, wchar_t* backupDir, bool* rootCreated)
{
	wchar_t root[MAX_PATH];
	wchar_t name[MAX_PATH];
	SYSTEMTIME now;
	GetLocalTime(&now);
	*rootCreated = false;
	if (!pathJoin(root, t->dir, BACKUP_FOLDER) || !UpdaterBackupFolderName(installed, &now, name, MAX_PATH - 8))
		return false;
	*rootCreated = CreateDirectoryW(root, NULL) != 0;
	if (!*rootCreated && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	if (!noLinksBelow(t->dir, BACKUP_FOLDER L"\\x"))
	{
		SetLastError(ERROR_REPARSE_POINT_ENCOUNTERED);
		return false;
	}
	size_t len = wcslen(name);
	for (int i = 1; i < 100; i++)
	{
		if (i > 1)
			_snwprintf(name + len, 8, L"-%d", i);
		if (!pathJoin(backupDir, root, name))
			return false;
		if (CreateDirectoryW(backupDir, NULL))
			return true;
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
	}
	return false;
}

static int applyStaged(const Target* t, const wchar_t* staging, const char* installed, const char* version)
{
	bool targetHasIni = fileExists(t->ini);
	DWORD n = 0;
	for (DWORD i = 0; i < nbPlan; i++)
		if (UpdaterPlanFile(plan[i].rel, targetHasIni) == UPDATER_PLAN_INSTALL)
			plan[n++] = plan[i];
	nbPlan = n;
	for (DWORD i = 0; i + 1 < nbPlan; i++)
		if (UpdaterApplyLast(plan[i].rel))
		{
			PlanEntry last = plan[i];
			memmove(&plan[i], &plan[i + 1], (nbPlan - i - 1) * sizeof(PlanEntry));
			plan[nbPlan - 1] = last;
			break;
		}
	nbCreatedDirs = 0;

	DWORD toBackUp = 0;
	bool selfReplaced = false;
	for (DWORD i = 0; i < nbPlan; i++)
	{
		wchar_t dest[MAX_PATH];
		if (!pathJoin(dest, t->dir, plan[i].rel))
		{
			fail(L"The path of %ls in %ls is too long. Nothing was changed.\n", plan[i].rel, t->dir);
			return UPDATER_EXIT_ERROR;
		}
		if (!noLinksBelow(t->dir, plan[i].rel))
		{
			fail(L"A folder for %ls in %ls is a junction or symbolic link; the updater does not write\n"
				L"through links. Nothing was changed.\n", plan[i].rel, t->dir);
			return UPDATER_EXIT_ERROR;
		}
		plan[i].existed = fileExists(dest);
		plan[i].isSelf = UpdaterSamePath(dest, selfPath);
		toBackUp += plan[i].existed;
		selfReplaced = selfReplaced || plan[i].isSelf;
	}

	wchar_t backupDir[MAX_PATH] = L"";
	bool backupRootCreated = false;
	if (toBackUp)
	{
		if (!createBackupDir(t, installed, backupDir, &backupRootCreated))
		{
			fail(L"Could not create a backup folder in %ls\\" BACKUP_FOLDER L": %ls. Nothing was changed.\n",
				t->dir, errorText(GetLastError()));
			return UPDATER_EXIT_ERROR;
		}
		for (DWORD i = 0; i < nbPlan; i++)
		{
			if (!plan[i].existed)
				continue;
			wchar_t src[MAX_PATH];
			wchar_t saved[MAX_PATH];
			if (!pathJoin(src, t->dir, plan[i].rel) || !pathJoin(saved, backupDir, plan[i].rel)
				|| !createParents(backupDir, plan[i].rel, false) || !CopyFileW(src, saved, TRUE))
			{
				DWORD error = GetLastError();
				wchar_t root[MAX_PATH];
				if (pathJoin(root, t->dir, BACKUP_FOLDER) && deleteTreeBelow(root, backupDir) && backupRootCreated)
					RemoveDirectoryW(root);
				fail(L"Could not back up %ls: %ls. Nothing was changed.\n", plan[i].rel, errorText(error));
				return UPDATER_EXIT_ERROR;
			}
		}
	}

	say(L"Installing %lu files into %ls...\n", nbPlan, t->dir);
	for (DWORD i = 0; i < nbPlan; i++)
	{
		wchar_t src[MAX_PATH];
		wchar_t dest[MAX_PATH];
		DWORD error = 0;
		bool ok = pathJoin(src, staging, plan[i].rel) && pathJoin(dest, t->dir, plan[i].rel);
		if (ok && !createParents(t->dir, plan[i].rel, true))
		{
			error = GetLastError();
			ok = false;
		}
		// Checked again right before the write: a folder swapped for a link since the first check aborts.
		if (ok && !noLinksBelow(t->dir, plan[i].rel))
		{
			error = ERROR_CANT_ACCESS_FILE;
			ok = false;
		}
		if (ok)
			ok = installFile(src, dest, plan[i].isSelf, &error);
		if (!ok)
		{
			fail(L"Could not replace %ls: %ls.\n", plan[i].rel, errorText(error ? error : ERROR_FILENAME_EXCED_RANGE));
			rollbackAndReport(t, backupDir, backupRootCreated);
			return UPDATER_EXIT_ERROR;
		}
		plan[i].done = true;
	}

	char now[UPDATE_TAG_MAX];
	if (readProductVersion(t->dll, now, UPDATE_TAG_MAX) != VERSION_READ_OK || strcmp(now, version))
	{
		fail(L"After the update PlugY.dll reports version \"%hs\", not %hs.\n", now, version);
		rollbackAndReport(t, backupDir, backupRootCreated);
		return UPDATER_EXIT_ERROR;
	}

	say(L"\nPlugY4Kolbot %hs is installed in %ls.\n", version, t->dir);
	say(L"  Updated files: %lu\n", nbPlan);
	if (backupDir[0])
		say(L"  Backup of the replaced files: %ls\n", backupDir);
	if (targetHasIni)
		say(L"  Your PlugY.ini was kept; new options take their defaults from PlugY\\PlugYDefault.ini.\n");
	else
		say(L"  There was no PlugY.ini, so the release's default PlugY.ini was installed.\n");
	if (selfReplaced)
		say(L"  The updater updated itself; the old copy (.old) is deleted the next time it runs.\n");
	pruneBackups(t, backupDir);
	return UPDATER_EXIT_OK;
}


/*============================ Installer ===========================*/

// Returns an eUpdaterOutcome. The installer runs without its pages, for t's install only.
static int runInstaller(const Target* t, const wchar_t* installer, const char* version)
{
	say(L"Running the PlugY4Kolbot %hs installer for %ls; this window waits for it.\n", version, t->dir);
	SHELLEXECUTEINFOW sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.lpVerb = L"open";	// the installer asks for administrator rights itself (requireAdministrator)
	sei.lpFile = installer;
	sei.lpParameters = t->installArgs;
	sei.lpDirectory = workDir;
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei))
	{
		DWORD error = GetLastError();
		int outcome = UpdaterRelaunchFailedOutcome(error);
		if (outcome == UPDATER_UAC_CANCELLED)
			fail(L"The installer was not given administrator permission, so nothing was changed.\n");
		else
			fail(L"Could not start the installer: %ls.\n", errorText(error));
		return outcome;
	}
	if (!sei.hProcess)
	{
		fail(L"Could not follow the installer.\n");
		return UPDATER_FAILED;
	}
	WaitForSingleObject(sei.hProcess, INFINITE);
	DWORD code = 2;
	GetExitCodeProcess(sei.hProcess, &code);
	CloseHandle(sei.hProcess);
	int outcome = UpdaterInstallerOutcome(code);
	if (outcome == UPDATER_INSTALLER_CANCELLED)
		fail(L"The installer was cancelled.\n");
	else if (code == 2)
		fail(L"The installer stopped (exit code 2). It does when the Diablo II folder, %ls,\n"
			L"no longer holds Game.exe.\n", t->dllDir);
	else if (outcome != UPDATER_UPDATED)
		fail(L"The installer failed (exit code %lu).\n", code);
	return outcome;
}

// Both of t's own files must report the new version, or the installer did not update this install.
static int finishInstaller(const Target* t, const char* version)
{
	const wchar_t* files[] = { t->dll, t->exe };
	for (int i = 0; i < 2; i++)
	{
		char now[UPDATE_TAG_MAX] = "";
		if (readProductVersion(files[i], now, UPDATE_TAG_MAX) != VERSION_READ_OK || strcmp(now, version))
		{
			fail(L"The installer finished, but %ls does not report version %hs (found \"%hs\").\n"
				L"Run the new installer yourself, and choose this install's folders.\n", files[i], version, now);
			return UPDATER_EXIT_ERROR;
		}
	}
	say(L"\nPlugY4Kolbot %hs is installed (PlugY.dll in %ls, PlugY.exe in %ls).\n", version, t->dllDir, t->dir);
	return UPDATER_EXIT_OK;
}

static volatile LONG selfAside = 0;	// 1 while the running updater is renamed to selfOldPath
static wchar_t selfOldPath[MAX_PATH];

// Closing the window or Ctrl+C while the installer runs must not leave the install without its updater.
static BOOL WINAPI restoreSelfOnCtrl(DWORD ctrlType)
{
	if (UpdaterCtrlShouldRestore(ctrlType, selfAside != 0) && InterlockedExchange(&selfAside, 0))
		MoveFileExW(selfOldPath, selfPath, MOVEFILE_REPLACE_EXISTING);
	return FALSE;
}

// NSIS cannot overwrite the running updater in the install folder, so it waits as .old while the installer runs.
static int updateWithInstaller(const Target* t, const wchar_t* installer, const char* version)
{
	if (!pathAppend(selfOldPath, selfPath, OLD_SUFFIX))
	{
		fail(L"The updater's path is too long. Nothing was changed.\n");
		return UPDATER_EXIT_ERROR;
	}
	SetConsoleCtrlHandler(restoreSelfOnCtrl, TRUE);
	if (!MoveFileExW(selfPath, selfOldPath, MOVEFILE_REPLACE_EXISTING))
	{
		SetConsoleCtrlHandler(restoreSelfOnCtrl, FALSE);
		fail(L"Could not move the running updater aside for the installer: %ls. Nothing was changed.\n",
			errorText(GetLastError()));
		return UPDATER_EXIT_ERROR;
	}
	InterlockedExchange(&selfAside, 1);
	int outcome = runInstaller(t, installer, version);
	// Whoever clears the flag first (this thread or the Ctrl handler) decides about the old copy.
	if (InterlockedExchange(&selfAside, 0)
		&& UpdaterSelfAfterInstaller(outcome, fileExists(selfPath)) == UPDATER_SELF_RESTORE_OLD
		&& !MoveFileExW(selfOldPath, selfPath, MOVEFILE_REPLACE_EXISTING))
		fail(L"Could not move the updater back from %ls; rename it to %ls.\n", selfOldPath, selfPath);
	SetConsoleCtrlHandler(restoreSelfOnCtrl, FALSE);
	if (outcome != UPDATER_UPDATED)
		return UpdaterExitCode(outcome);
	return finishInstaller(t, version);
}


/*============================== Main ==============================*/

static bool askYesNo(const char* version)
{
	say(L"Install PlugY4Kolbot %hs? [y/N] ", version);
	char answer[32];
	bool read = fgets(answer, sizeof(answer), stdin) != NULL;
	// A console echoes the Enter that ends the answer; piped input does not.
	DWORD mode;
	if (!read || !GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode))
		say(L"\n");
	if (!read)
		return false;
	char* s = answer;
	while (*s == ' ' || *s == '\t')
		s++;
	return (s[0] == 'y' || s[0] == 'Y') && (s[1] == 0 || s[1] == '\r' || s[1] == '\n'
		|| ((s[1] == 'e' || s[1] == 'E') && (s[2] == 's' || s[2] == 'S') && (s[3] == 0 || s[3] == '\r' || s[3] == '\n')));
}

static bool isInUse(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		CloseHandle(h);
		return false;
	}
	DWORD error = GetLastError();
	return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
}

static int reportInUse(const Target* t)
{
	fail(L"Close every Diablo II game and PlugY.exe that use %ls, then run the updater again.\n", t->dir);
	return UpdaterExitCode(UPDATER_FILES_IN_USE);
}

static void printNotes()
{
	static char head[UPDATE_NOTES_MAX];
	bool more = UpdaterNotesHead(release.notes, NOTES_LINES, head, sizeof(head)) || release.notesTruncated;
	if (!head[0])
		return;
	say(L"\nRelease notes:\n");
	char* line = head;
	while (*line)
	{
		char* end = strchr(line, '\n');
		if (end)
			*end = 0;
		static wchar_t wide[UPDATE_NOTES_MAX];
		if (!MultiByteToWideChar(CP_UTF8, 0, line, -1, wide, UPDATE_NOTES_MAX))
			wide[0] = 0;
		say(L"  %ls\n", wide);
		if (!end)
			break;
		line = end + 1;
	}
	if (more)
		say(L"  ...\n");
}

static bool readChannelPrerelease(const Target* t)
{
	wchar_t value[64];
	GetPrivateProfileStringW(L"GENERAL", L"UpdateChannel", L"", value, 64, t->ini);
	return CompareStringOrdinal(value, -1, L"prerelease", -1, TRUE) == CSTR_EQUAL;
}

// The update of one detected target; mutex is held (NULL for --check) and released before a relaunch.
static int updateTarget(const Options* o, const Target t, HANDLE* mutex)
{
	char installed[UPDATE_TAG_MAX];
	int read = readProductVersion(t.dll, installed, UPDATE_TAG_MAX);
	if (read == VERSION_READ_IN_USE)
		return reportInUse(&t);
	if (read == VERSION_READ_MISSING || read == VERSION_READ_ERROR)
	{
		fail(L"Could not read %ls.\n", t.dll);
		return UPDATER_EXIT_ERROR;
	}
	UpdateVersion dllVersion = { 0 };
	bool dllSemver = read == VERSION_READ_OK && UpdateParseVersion(installed, &dllVersion);
	if (read != VERSION_READ_OK)
		strcpy(installed, "unknown");
	char dllInstalled[UPDATE_TAG_MAX];
	strcpy(dllInstalled, installed);
	char exeVersion[UPDATE_TAG_MAX];
	int exeRead = readProductVersion(t.exe, exeVersion, UPDATE_TAG_MAX);
	if (exeRead == VERSION_READ_IN_USE)
		return reportInUse(&t);
	if (exeRead != VERSION_READ_OK)
		strcpy(exeVersion, "unknown");
	UpdateVersion exeParsed = { 0 };
	bool exeSemver = exeRead == VERSION_READ_OK && UpdateParseVersion(exeVersion, &exeParsed);
	int source = UpdaterInstalledVersion(dllSemver, exeRead != VERSION_READ_MISSING, exeSemver,
		dllSemver && exeSemver ? UpdateCompareVersions(&exeParsed, &dllVersion) : 0);
	bool fromExe = source == UPDATER_INSTALLED_EXE || source == UPDATER_INSTALLED_EXE_UNKNOWN;
	bool installedSemver = source == UPDATER_INSTALLED_DLL || source == UPDATER_INSTALLED_EXE;
	UpdateVersion installedVersion = fromExe ? exeParsed : dllVersion;
	if (fromExe)
		strcpy(installed, exeVersion);

	bool prerelease = o->prerelease || readChannelPrerelease(&t);
	char version[UPDATE_TAG_MAX];
	char pageUrl[UPDATE_URL_MAX];
	UpdateVersion latest;
	if (localPackage(o)[0])
	{
		strcpy(version, o->version);
		strcpy(pageUrl, RELEASES_URL);
		if (!UpdateParseVersion(version, &latest))
		{
			fail(L"--version %hs is not a version like 14.4.0 or 14.4.0-rc.1.\n", version);
			return UPDATER_EXIT_ERROR;
		}
	}
	else
	{
		say(L"Checking GitHub for the newest PlugY4Kolbot release (%hs channel)...\n", prerelease ? "prerelease" : "stable");
		UpdateHttpInfo info;
		int result = UpdateFetchLatestEx(PLUGY_UPDATE_REPO, prerelease, UPDATER_HTTP_FLAGS, &release, &info);
		if (result == UPDATE_NO_RELEASE)
		{
			say(L"No PlugY4Kolbot release is published on the %hs channel yet.\n", prerelease ? "prerelease" : "stable");
			return UpdaterExitCode(UPDATER_UP_TO_DATE);
		}
		if (result != UPDATE_OK)
		{
			wchar_t text[512];
			describeResult(result, &info, text, 512);
			fail(L"Could not check %hs for updates: %ls.\nReleases: %hs\n", PLUGY_UPDATE_REPO, text, RELEASES_URL);
			return UPDATER_EXIT_ERROR;
		}
		strcpy(version, release.tag[0] == 'v' ? release.tag + 1 : release.tag);
		strcpy(pageUrl, release.pageUrl[0] ? release.pageUrl : RELEASES_URL);
		latest = release.version;
		if (!UpdaterExpectedVersionMatches(o->expectVersion, version))
		{
			fail(L"The newest release is now PlugY4Kolbot %hs, not the %hs you confirmed (it changed\n"
				L"while Windows asked for permission). Nothing was changed; run the updater again.\n", version, o->expectVersion);
			return UPDATER_EXIT_ERROR;
		}
	}

	int offer = UpdaterOffer(installedSemver, installedSemver ? UpdateCompareVersions(&latest, &installedVersion) : 0,
		dllSemver, dllSemver ? UpdateCompareVersions(&latest, &dllVersion) : 0);
	if (offer == UPDATER_OFFER_UP_TO_DATE)
	{
		say(L"PlugY4Kolbot %hs is up to date.\n", installed);
		return UpdaterExitCode(UPDATER_UP_TO_DATE);
	}
	if (offer == UPDATER_OFFER_SHARED_DLL_NEWER)
	{
		fail(L"PlugY.dll in %ls is PlugY4Kolbot %hs, newer than %hs: another install in that folder\n"
			L"updated it. Updating this install would put the older PlugY.dll back, so nothing was changed.\n"
			L"Give this install the other one's UpdateChannel (in the [GENERAL] section of its PlugY.ini),\n"
			L"or wait for a newer release.\n", t.dllDir, dllInstalled, version);
		return UPDATER_EXIT_ERROR;
	}
	if (o->check)
	{
		say(L"PlugY4Kolbot %hs is available (installed: %hs%hs).\nRelease page: %hs\n", version,
			installedSemver ? "" : "upstream PlugY ", installed, pageUrl);
		return UpdaterExitCode(UPDATER_UPDATE_AVAILABLE);
	}

	if (installedSemver)
		say(L"Installed: PlugY4Kolbot %hs in %ls\n", installed, t.dir);
	else
		say(L"Installed: PlugY %hs in %ls\nThis switches it from upstream PlugY to PlugY4Kolbot.\n", installed, t.dir);
	say(L"New:       PlugY4Kolbot %hs\n", version);
	if (localPackage(o)[0])
		say(L"From:      %ls\n", localPackage(o));
	else
	{
		printNotes();
		say(L"Release page: %hs\n", pageUrl);
	}
	// --elevated: the copy that asked for administrator rights already asked this.
	if (!o->yes && !o->elevated && !askYesNo(version))
	{
		say(L"Nothing was changed.\n");
		return UpdaterExitCode(UPDATER_DECLINED);
	}

	DWORD probe = probeWrite(t.dir);
	if (!probe && t.installerLayout)
		probe = probeWrite(t.dllDir);
	int elevation = UpdaterElevationAction(probe, o->elevated);
	if (elevation == UPDATER_ELEVATION_RELAUNCH)
	{
		releaseUpdaterMutex(mutex);
		return relaunchElevated(o, version);
	}
	if (elevation == UPDATER_ELEVATION_FAIL)
	{
		fail(L"Cannot write to %ls%ls: %ls.\n", t.dir, t.installerLayout ? L" or the Diablo II folder" : L"", errorText(probe));
		return UPDATER_EXIT_ERROR;
	}

	if (isInUse(t.dll) || isInUse(t.exe))
		return reportInUse(&t);

	if (!createWorkDir(version, o->workBase[0] ? o->workBase : NULL, o->workBase[0] || isProcessElevated()))
		return UPDATER_EXIT_ERROR;
	char packageName[UPDATE_NAME_MAX];
	_snprintf(packageName, UPDATE_NAME_MAX, t.installerLayout ? "PlugY4Kolbot_v%s_Installer.exe" : "PlugY4Kolbot_v%s.zip", version);
	packageName[UPDATE_NAME_MAX - 1] = 0;
	wchar_t packagePath[MAX_PATH];
	if (!fetchAndVerify(o, packageName, packagePath))
		return UPDATER_EXIT_ERROR;

	if (t.installerLayout)
		return updateWithInstaller(&t, packagePath, version);
	wchar_t staging[MAX_PATH];
	if (!extractAndValidate(packagePath, version, pageUrl, &t, staging))
		return UPDATER_EXIT_ERROR;
	return applyStaged(&t, staging, installed, version);
}

static int update(const Options* o)
{
	Target t;
	if (!detectTarget(o, &t))
		return UPDATER_EXIT_ERROR;
	if (o->fromZip[0] && t.installerLayout)
	{
		fail(L"--from-zip needs a zip-style install; this one is updated by the installer (--from-installer).\n");
		return UPDATER_EXIT_ERROR;
	}
	if (o->fromInstaller[0] && !t.installerLayout)
	{
		fail(L"--from-installer needs an install the installer made; this one is updated from the zip (--from-zip).\n");
		return UPDATER_EXIT_ERROR;
	}
	HANDLE mutex = NULL;
	if (!o->check)
	{
		// Held for the whole run: two updaters on one install would interleave their replacements.
		mutex = acquireUpdaterMutex(&t);
		if (!mutex)
		{
			fail(L"Another PlugY4Kolbot updater is running for %ls; let it finish first.\n", t.dir);
			return UPDATER_EXIT_ERROR;
		}
		removeLeftovers(&t);
	}
	int code = updateTarget(o, t, &mutex);
	cleanWorkDir();
	releaseUpdaterMutex(&mutex);
	return code;
}

static bool parseArgs(int argc, wchar_t** argv, Options* o)
{
	memset(o, 0, sizeof(*o));
	for (int i = 1; i < argc; i++)
	{
		const wchar_t* a = argv[i];
		bool hasValue = i + 1 < argc;
		if (!wcscmp(a, L"--help") || !wcscmp(a, L"-h") || !wcscmp(a, L"/?"))
			o->help = true;
		else if (!wcscmp(a, L"--check"))
			o->check = true;
		else if (!wcscmp(a, L"--yes") || !wcscmp(a, L"-y"))
			o->yes = true;
		else if (!wcscmp(a, L"--prerelease"))
			o->prerelease = true;
		else if (!wcscmp(a, L"--registry-off"))
			o->registryOff = true;
		else if (!wcscmp(a, L"--elevated"))
			o->elevated = true;
		else if (hasValue && (!wcscmp(a, L"--target") || !wcscmp(a, L"--from-zip") || !wcscmp(a, L"--from-installer")
			|| !wcscmp(a, L"--sums") || !wcscmp(a, L"--work-base")))
		{
			wchar_t* field = !wcscmp(a, L"--target") ? o->target : !wcscmp(a, L"--from-zip") ? o->fromZip
				: !wcscmp(a, L"--from-installer") ? o->fromInstaller : !wcscmp(a, L"--sums") ? o->sums : o->workBase;
			if (!canonicalPath(argv[++i], field))
			{
				fail(L"%ls: the path %ls is not usable.\n", a, argv[i]);
				return false;
			}
		}
		else if (hasValue && !wcscmp(a, L"--registry-base"))
		{
			const wchar_t* key = argv[++i];
			if (!key[0] || wcslen(key) >= REGISTRY_BASE_MAX)
			{
				fail(L"--registry-base: the key %ls is not usable.\n", key);
				return false;
			}
			wcscpy(o->registryBase, key);
		}
		else if (hasValue && (!wcscmp(a, L"--version") || !wcscmp(a, L"--expect-version")))
		{
			char* field = !wcscmp(a, L"--version") ? o->version : o->expectVersion;
			BOOL lossy = FALSE;
			UpdateVersion parsed;
			if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, argv[++i], -1, field, UPDATE_TAG_MAX, NULL, &lossy) <= 0 || lossy
				|| (field == o->expectVersion && !UpdateParseVersion(field, &parsed)))
			{
				fail(L"%ls %ls is not a version like 14.4.0.\n", a, argv[i]);
				return false;
			}
		}
		else
		{
			fail(L"Unknown or incomplete option %ls. Run PlugY4KolbotUpdater.exe --help for the options.\n", a);
			return false;
		}
	}
	bool local = o->fromZip[0] || o->fromInstaller[0];
	if ((o->fromZip[0] && o->fromInstaller[0]) || local != (o->sums[0] != 0) || local != (o->version[0] != 0))
	{
		fail(L"--from-zip or --from-installer, --sums and --version go together.\n");
		return false;
	}
	return true;
}

int wmain(int argc, wchar_t** argv)
{
	/* Before anything can load a DLL: the delay-loaded winhttp, bcrypt and
	   version.dll (none is a KnownDLL) then come from System32 only, never
	   from a planted copy beside the exe, which an elevated run would load
	   as administrator. The link also sets /DEPENDENTLOADFLAG for imports. */
	// Resolved at run time: Windows 7 without KB2533623 lacks it, and the exe must still start there.
	typedef BOOL (WINAPI* SetDefaultDllDirectoriesFn)(DWORD);
	SetDefaultDllDirectoriesFn setDefaultDllDirectories =
		(SetDefaultDllDirectoriesFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
	if (!setDefaultDllDirectories || !setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
		SetDllDirectoryW(L"");	// at least drops the current folder from the search

	// Started from Explorer: the console closes with the process, so it waits for Enter.
	DWORD consoleProcesses[2];
	bool ownConsole = GetConsoleProcessList(consoleProcesses, 2) == 1;

	int code = UPDATER_EXIT_ERROR;
	Options o;
	memset(&o, 0, sizeof(o));
	DWORD n = GetModuleFileNameW(NULL, selfPath, MAX_PATH);
	if (!n || n >= MAX_PATH || !canonicalPath(selfPath, selfPath))
		fail(L"The updater's own path is too long.\n");
	else if (parseArgs(argc, argv, &o))
	{
		if (o.help)
		{
			printUsage();
			code = UPDATER_EXIT_OK;
		}
		else
		{
			// Left by the previous self-update, once that updater has exited.
			wchar_t old[MAX_PATH];
			if (pathAppend(old, selfPath, OLD_SUFFIX))
				DeleteFileW(old);
			code = update(&o);
			cleanWorkDir();
		}
	}

	// The elevated copy runs in a console of its own; with --yes it closes like its parent would.
	DWORD mode;
	if (ownConsole && !relaunched && !(o.elevated && o.yes) && GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode))
	{
		say(L"\nPress Enter to close.");
		char line[16];
		fgets(line, sizeof(line), stdin);
	}
	return code;
}

///////////////////////// END OF FILE ///////////////////////

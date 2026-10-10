/*=================================================================
	PlugY4Kolbot updater: the decisions that need no I/O.
	See UpdaterLogic.h.
=================================================================*/

#include "UpdaterLogic.h"
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>


int UpdaterExitCode(int outcome)
{
	switch (outcome)
	{
	case UPDATER_UPDATED:
	case UPDATER_UP_TO_DATE:
		return UPDATER_EXIT_OK;
	case UPDATER_UPDATE_AVAILABLE:
		return UPDATER_EXIT_UPDATE_AVAILABLE;
	case UPDATER_FILES_IN_USE:
		return UPDATER_EXIT_IN_USE;
	case UPDATER_DECLINED:
	case UPDATER_UAC_CANCELLED:
	case UPDATER_INSTALLER_CANCELLED:
		return UPDATER_EXIT_CANCELLED;
	default:
		return UPDATER_EXIT_ERROR;
	}
}

int UpdaterInstallerOutcome(DWORD installerExitCode)
{
	if (installerExitCode == 0)
		return UPDATER_UPDATED;
	if (installerExitCode == 1)
		return UPDATER_INSTALLER_CANCELLED;
	return UPDATER_FAILED;
}

int UpdaterChildExitCode(DWORD childExitCode)
{
	return childExitCode <= UPDATER_EXIT_CANCELLED ? (int)childExitCode : UPDATER_EXIT_ERROR;
}

int UpdaterSelfAfterInstaller(int installerOutcome, bool newSelfExists)
{
	return installerOutcome == UPDATER_UPDATED && newSelfExists ? UPDATER_SELF_KEEP_NEW : UPDATER_SELF_RESTORE_OLD;
}

bool UpdaterCtrlShouldRestore(DWORD ctrlType, bool selfAside)
{
	return selfAside && (ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT);
}

int UpdaterElevationAction(DWORD probeError, bool elevated)
{
	if (!probeError)
		return UPDATER_ELEVATION_NONE;
	// One relaunch at most: an elevated copy that is still denied would otherwise loop.
	if (probeError == ERROR_ACCESS_DENIED && !elevated)
		return UPDATER_ELEVATION_RELAUNCH;
	return UPDATER_ELEVATION_FAIL;
}

int UpdaterRelaunchFailedOutcome(DWORD error)
{
	return error == ERROR_CANCELLED ? UPDATER_UAC_CANCELLED : UPDATER_FAILED;
}


/*-------------------------------- Layout -------------------------------*/
int UpdaterChooseLayout(bool listed, bool dllBeside, bool registered)
{
	if (listed)
		return UPDATER_LAYOUT_INSTALLER;
	if (dllBeside)
		return UPDATER_LAYOUT_ZIP;
	return registered ? UPDATER_LAYOUT_INSTALLER : UPDATER_LAYOUT_NONE;
}

static bool isInstallerArgUsable(const wchar_t* s)
{
	if (!s || !s[0])
		return false;
	for (; *s; s++)
		if (*s == L'"' || *s < 0x20)
			return false;
	return true;
}

bool UpdaterBuildInstallerArgs(const wchar_t* d2Dir, const wchar_t* installDir, wchar_t* out, DWORD maxChars)
{
	if (!out || !maxChars)
		return false;
	out[0] = 0;
	if (!isInstallerArgUsable(d2Dir) || !isInstallerArgUsable(installDir) || wcsstr(d2Dir, L" /"))
		return false;
	int n = _snwprintf(out, maxChars, L"/S /D2=\"%ls\" /D=%ls", d2Dir, installDir);
	if (n < 0 || (DWORD)n >= maxChars)
	{
		out[0] = 0;
		return false;
	}
	return true;
}


/*--------------------------- Installed version -------------------------*/
int UpdaterInstalledVersion(bool dllSemver, bool exePresent, bool exeSemver, int exeVsDll)
{
	if (!dllSemver)
		return UPDATER_INSTALLED_DLL_UNKNOWN;
	if (!exePresent)
		return UPDATER_INSTALLED_DLL;
	if (!exeSemver)
		return UPDATER_INSTALLED_EXE_UNKNOWN;
	return exeVsDll < 0 ? UPDATER_INSTALLED_EXE : UPDATER_INSTALLED_DLL;
}

int UpdaterOffer(bool installedSemver, int latestVsInstalled, bool dllSemver, int latestVsDll)
{
	if (installedSemver && latestVsInstalled <= 0)
		return UPDATER_OFFER_UP_TO_DATE;
	if (dllSemver && latestVsDll < 0)
		return UPDATER_OFFER_SHARED_DLL_NEWER;
	return UPDATER_OFFER_UPDATE;
}


/*-------------------------------- Paths --------------------------------*/
static bool isSeparator(wchar_t ch)
{
	return ch == L'\\' || ch == L'/';
}

bool UpdaterIsSafeRelativePath(const wchar_t* rel)
{
	if (!rel || !rel[0] || isSeparator(rel[0]))
		return false;
	const wchar_t* p = rel;
	while (*p)
	{
		const wchar_t* start = p;
		bool onlyDots = true;
		while (*p && !isSeparator(*p))
		{
			if (*p == L':' || *p < 0x20)
				return false;
			if (*p != L'.' && *p != L' ')
				onlyDots = false;
			p++;
		}
		if (p == start || onlyDots)
			return false;
		if (*p)
			p++;
	}
	return true;
}

static DWORD trimmedLength(const wchar_t* s)
{
	DWORD n = (DWORD)wcslen(s);
	while (n && isSeparator(s[n - 1]))
		n--;
	return n;
}

static bool sameChar(wchar_t a, wchar_t b)
{
	if (a == L'/')
		a = L'\\';
	if (b == L'/')
		b = L'\\';
	return a == b || CompareStringOrdinal(&a, 1, &b, 1, TRUE) == CSTR_EQUAL;
}

bool UpdaterSamePath(const wchar_t* a, const wchar_t* b)
{
	if (!a || !b)
		return false;
	DWORD la = trimmedLength(a);
	if (!la || la != trimmedLength(b))
		return false;
	for (DWORD i = 0; i < la; i++)
		if (!sameChar(a[i], b[i]))
			return false;
	return true;
}

bool UpdaterIsPathInside(const wchar_t* base, const wchar_t* path)
{
	if (!base || !path)
		return false;
	DWORD lb = trimmedLength(base);
	if (!lb || wcslen(path) < lb)
		return false;
	for (DWORD i = 0; i < lb; i++)
		if (!sameChar(base[i], path[i]))
			return false;
	return !path[lb] || isSeparator(path[lb]);
}

bool UpdaterBackupFolderName(const char* installed, const SYSTEMTIME* t, wchar_t* out, DWORD maxChars)
{
	if (!out || !maxChars || !t)
		return false;
	out[0] = 0;
	char name[33];
	DWORD n = 0;
	for (const char* s = installed ? installed : ""; *s && n < 32; s++)
	{
		char ch = *s;
		bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
			|| ch == '.' || ch == '-' || ch == '_';
		name[n++] = keep ? ch : '_';
	}
	name[n] = 0;
	if (!n)
		strcpy(name, "unknown");
	int written = _snwprintf(out, maxChars, L"%hs-%04u%02u%02u-%02u%02u%02u", name,
		t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute, t->wSecond);
	if (written < 0 || (DWORD)written >= maxChars)
	{
		out[0] = 0;
		return false;
	}
	return true;
}


static bool isDigits(const wchar_t* s, DWORD n)
{
	for (DWORD i = 0; i < n; i++)
		if (s[i] < L'0' || s[i] > L'9')
			return false;
	return true;
}

static bool isBackupNameChar(wchar_t ch)
{
	return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')
		|| ch == L'.' || ch == L'-' || ch == L'_';
}

bool UpdaterParseBackupName(const wchar_t* name, ULONGLONG* key)
{
	if (!name)
		return false;
	DWORD len = (DWORD)wcslen(name);
	DWORD n = 1;
	// "-N" ends the name only when what precedes it ends in a full stamp.
	for (DWORD digits = 1; digits <= 2; digits++)
	{
		if (len < 18 + digits || name[len - digits - 1] != L'-' || !isDigits(name + len - digits, digits))
			continue;
		DWORD stampEnd = len - digits - 1;
		if (name[stampEnd - 16] != L'-' || name[stampEnd - 7] != L'-' || !isDigits(name + stampEnd - 15, 8)
			|| !isDigits(name + stampEnd - 6, 6))
			continue;
		DWORD value = (DWORD)_wtoi(name + len - digits);
		if (value < 2 || (digits == 2 && name[len - 2] == L'0'))
			continue;
		n = value;
		len = stampEnd;
		break;
	}
	if (len < 17 || name[len - 16] != L'-' || name[len - 7] != L'-' || !isDigits(name + len - 15, 8) || !isDigits(name + len - 6, 6))
		return false;
	DWORD prefixLen = len - 16;
	if (prefixLen > 32)
		return false;
	for (DWORD i = 0; i < prefixLen; i++)
		if (!isBackupNameChar(name[i]))
			return false;
	ULONGLONG stamp = 0;
	for (DWORD i = len - 15; i < len; i++)
		if (name[i] != L'-')
			stamp = stamp * 10 + (name[i] - L'0');
	if (key)
		*key = stamp * 100 + n;
	return true;
}

DWORD UpdaterSelectOldBackups(const wchar_t* const* names, const ULONGLONG* createdAt, DWORD count, DWORD keep,
	DWORD protect, bool* remove)
{
	DWORD marked = 0;
	for (DWORD i = 0; i < count; i++)
	{
		remove[i] = false;
		if (i == protect || !UpdaterParseBackupName(names[i], NULL))
			continue;
		DWORD newer = 0;
		for (DWORD j = 0; j < count; j++)
		{
			// Equal times are ranked by position so exactly keep folders survive.
			if (j != i && UpdaterParseBackupName(names[j], NULL)
				&& (createdAt[j] > createdAt[i] || (createdAt[j] == createdAt[i] && j < i)))
				newer++;
		}
		if (newer >= keep)
		{
			remove[i] = true;
			marked++;
		}
	}
	return marked;
}

static bool endsWithNoCase(const wchar_t* s, const wchar_t* suffix)
{
	size_t ls = wcslen(s);
	size_t lx = wcslen(suffix);
	return ls > lx && CompareStringOrdinal(s + ls - lx, (int)lx, suffix, (int)lx, TRUE) == CSTR_EQUAL;
}

bool UpdaterIsLeftoverTemp(const wchar_t* name)
{
	return name && endsWithNoCase(name, UPDATER_NEW_SUFFIX);
}

bool UpdaterAdminOnlySecurity(SECURITY_ATTRIBUTES* sa)
{
	if (!sa)
		return false;
	memset(sa, 0, sizeof(*sa));
	sa->nLength = sizeof(*sa);
	return ConvertStringSecurityDescriptorToSecurityDescriptorW(UPDATER_ADMIN_ONLY_SDDL, SDDL_REVISION_1,
		&sa->lpSecurityDescriptor, NULL) != 0;
}

bool UpdaterIsAdminOwner(PSID owner)
{
	return owner && IsValidSid(owner)
		&& (IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid));
}

bool UpdaterMutexName(const wchar_t* target, bool global, wchar_t* out, DWORD maxChars)
{
	if (!target || !out || !maxChars)
		return false;
	out[0] = 0;
	DWORD len = trimmedLength(target);
	if (!len || len >= 1024)
		return false;
	wchar_t normal[1024];
	for (DWORD i = 0; i < len; i++)
		normal[i] = target[i] == L'/' ? L'\\' : target[i];
	wchar_t upper[1024];
	if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, normal, (int)len, upper, 1024, NULL, NULL, 0) != (int)len)
		return false;
	ULONGLONG hash = 14695981039346656037ULL;
	for (DWORD i = 0; i < len; i++)
	{
		hash = (hash ^ (upper[i] & 0xFF)) * 1099511628211ULL;
		hash = (hash ^ (upper[i] >> 8)) * 1099511628211ULL;
	}
	int n = _snwprintf(out, maxChars, L"%ls\\PlugY4KolbotUpdater-%016llx", global ? L"Global" : L"Local", hash);
	if (n < 0 || (DWORD)n >= maxChars)
	{
		out[0] = 0;
		return false;
	}
	return true;
}


/*------------------------------ File plan ------------------------------*/
bool UpdaterIsUserIni(const wchar_t* rel)
{
	return rel && CompareStringOrdinal(rel, -1, L"PlugY.ini", -1, TRUE) == CSTR_EQUAL;
}

bool UpdaterApplyLast(const wchar_t* rel)
{
	return rel && CompareStringOrdinal(rel, -1, L"PlugY.dll", -1, TRUE) == CSTR_EQUAL;
}

int UpdaterPlanFile(const wchar_t* rel, bool targetHasIni)
{
	return UpdaterIsUserIni(rel) && targetHasIni ? UPDATER_PLAN_KEEP : UPDATER_PLAN_INSTALL;
}


/*----------------------------- Command line ----------------------------*/
// The quoting rules of CommandLineToArgvW and the Microsoft CRT: backslashes are
// literal except in front of a quote, where each one needs doubling.
bool UpdaterAppendArg(wchar_t* cmd, DWORD maxChars, const wchar_t* arg)
{
	if (!cmd || !arg || !maxChars)
		return false;
	DWORD start = (DWORD)wcslen(cmd);
	DWORD pos = start;
#define PUT(ch) do { if (pos + 1 >= maxChars) { cmd[start] = 0; return false; } cmd[pos++] = (ch); } while (0)
	if (pos)
		PUT(L' ');
	if (arg[0] && !wcspbrk(arg, L" \t\n\v\""))
	{
		for (const wchar_t* s = arg; *s; s++)
			PUT(*s);
	}
	else
	{
		PUT(L'"');
		for (const wchar_t* s = arg; ; s++)
		{
			DWORD backslashes = 0;
			while (*s == L'\\')
			{
				s++;
				backslashes++;
			}
			if (!*s)
			{
				for (DWORD i = 0; i < backslashes * 2; i++)
					PUT(L'\\');
				break;
			}
			if (*s == L'"')
			{
				for (DWORD i = 0; i < backslashes * 2 + 1; i++)
					PUT(L'\\');
			}
			else
			{
				for (DWORD i = 0; i < backslashes; i++)
					PUT(L'\\');
			}
			PUT(*s);
		}
		PUT(L'"');
	}
#undef PUT
	cmd[pos] = 0;
	return true;
}


static bool appendNamed(wchar_t* cmd, DWORD maxChars, const wchar_t* name, const wchar_t* value)
{
	return UpdaterAppendArg(cmd, maxChars, name) && UpdaterAppendArg(cmd, maxChars, value);
}

static bool appendNamedAscii(wchar_t* cmd, DWORD maxChars, const wchar_t* name, const char* value)
{
	wchar_t wide[128];
	return MultiByteToWideChar(CP_ACP, 0, value, -1, wide, 128) > 0 && appendNamed(cmd, maxChars, name, wide);
}

bool UpdaterBuildRelaunchArgs(const UpdaterRelaunchArgs* a, wchar_t* out, DWORD maxChars)
{
	if (!a || !out || !maxChars)
		return false;
	out[0] = 0;
	bool ok = (!a->yes || UpdaterAppendArg(out, maxChars, L"--yes"))
		&& (!a->prerelease || UpdaterAppendArg(out, maxChars, L"--prerelease"))
		&& (!a->registryOff || UpdaterAppendArg(out, maxChars, L"--registry-off"))
		&& (!a->registryBase || !a->registryBase[0] || appendNamed(out, maxChars, L"--registry-base", a->registryBase))
		&& (!a->workBase || !a->workBase[0] || appendNamed(out, maxChars, L"--work-base", a->workBase))
		&& (!a->target || !a->target[0] || appendNamed(out, maxChars, L"--target", a->target))
		&& (!a->fromZip || !a->fromZip[0] || appendNamed(out, maxChars, L"--from-zip", a->fromZip))
		&& (!a->fromInstaller || !a->fromInstaller[0] || appendNamed(out, maxChars, L"--from-installer", a->fromInstaller))
		&& (!a->sums || !a->sums[0] || appendNamed(out, maxChars, L"--sums", a->sums))
		&& (!a->version || !a->version[0] || appendNamedAscii(out, maxChars, L"--version", a->version))
		&& (!a->expectVersion || !a->expectVersion[0] || appendNamedAscii(out, maxChars, L"--expect-version", a->expectVersion))
		&& UpdaterAppendArg(out, maxChars, L"--elevated");
	if (!ok)
		out[0] = 0;
	return ok;
}

bool UpdaterExpectedVersionMatches(const char* expectVersion, const char* selectedVersion)
{
	if (!expectVersion || !expectVersion[0])
		return true;
	return selectedVersion && !strcmp(expectVersion, selectedVersion);
}


/*------------------------------ Release notes ---------------------------*/
static DWORD utf8SequenceLength(unsigned char lead)
{
	if (lead >= 0xF0)
		return 4;
	if (lead >= 0xE0)
		return 3;
	if (lead >= 0xC0)
		return 2;
	return 1;
}

bool UpdaterNotesHead(const char* notes, DWORD maxLines, char* out, DWORD outSize)
{
	if (!out || !outSize)
		return notes && notes[0];
	out[0] = 0;
	if (!notes)
		return false;
	DWORD o = 0;
	DWORD lines = 0;
	const char* p = notes;
	while (*p && lines < maxLines)
	{
		while (*p && *p != '\n')
		{
			if (*p == '\r' && (p[1] == '\n' || !p[1]))
			{
				p++;
				continue;
			}
			DWORD len = utf8SequenceLength((unsigned char)*p);
			for (DWORD i = 1; i < len; i++)
				if (!p[i])
					len = i;
			if (o + len + 1 > outSize)
			{
				out[o] = 0;
				return true;
			}
			memcpy(out + o, p, len);
			o += len;
			p += len;
		}
		if (*p == '\n')
			p++;
		if (o + 2 > outSize)
		{
			out[o] = 0;
			return true;
		}
		out[o++] = '\n';
		lines++;
	}
	out[o] = 0;
	// Only blank lines left over are not worth a "more" marker.
	for (; *p; p++)
		if (*p != '\r' && *p != '\n' && *p != ' ' && *p != '\t')
			return true;
	return false;
}

///////////////////////// END OF FILE ///////////////////////

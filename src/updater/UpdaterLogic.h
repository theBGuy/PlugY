/*=================================================================
	PlugY4Kolbot updater: the decisions that need no I/O.

	Kept apart from PlugYUpdate.cpp so tests/UpdaterTests.cpp can
	check them without touching the network, the registry or any
	install. Paths are wide: release files have non-ANSI names (the
	zh-TW readme), which the ANSI code page cannot hold.
=================================================================*/
#pragma once

#include <windows.h>

// The process exit codes; docs and scripts depend on these values.
enum eUpdaterExit
{
	UPDATER_EXIT_OK = 0,				// updated, or already up to date
	UPDATER_EXIT_ERROR = 1,
	UPDATER_EXIT_UPDATE_AVAILABLE = 2,	// --check only
	UPDATER_EXIT_IN_USE = 3,
	UPDATER_EXIT_CANCELLED = 4			// the user said no: prompt, UAC or installer
};

enum eUpdaterOutcome
{
	UPDATER_UPDATED = 0,
	UPDATER_UP_TO_DATE,
	UPDATER_UPDATE_AVAILABLE,
	UPDATER_FAILED,
	UPDATER_FILES_IN_USE,
	UPDATER_DECLINED,			// answered no at the [y/N] prompt
	UPDATER_UAC_CANCELLED,
	UPDATER_INSTALLER_CANCELLED
};

int UpdaterExitCode(int outcome);

// An NSIS installer's exit code: 0 done, 1 cancelled by the user, 2 aborted by the script.
int UpdaterInstallerOutcome(DWORD installerExitCode);

// The elevated copy's exit code as this process's own: anything outside 0-4 is an error.
int UpdaterChildExitCode(DWORD childExitCode);

/* Installer layout: the running updater is moved to .old so NSIS can overwrite
   it. Afterwards the new copy stays only when the installer succeeded and put
   one in place; otherwise the old one is moved back. */
enum eUpdaterSelfAfterInstaller
{
	UPDATER_SELF_KEEP_NEW = 0,
	UPDATER_SELF_RESTORE_OLD
};
int UpdaterSelfAfterInstaller(int installerOutcome, bool newSelfExists);
// A console close, Ctrl+C or Ctrl+Break while the updater is renamed aside: move it back before exiting.
bool UpdaterCtrlShouldRestore(DWORD ctrlType, bool selfAside);


/*------------------------------ Elevation ------------------------------*/
enum eUpdaterElevation
{
	UPDATER_ELEVATION_NONE = 0,	// the folder is writable
	UPDATER_ELEVATION_RELAUNCH,	// relaunch as administrator with --elevated
	UPDATER_ELEVATION_FAIL		// another error, or still denied after the one relaunch
};

// probeError is the GetLastError of creating the write-test file, 0 when it worked.
int UpdaterElevationAction(DWORD probeError, bool elevated);
// The outcome of a relaunch that ShellExecuteEx could not start (error is its GetLastError).
int UpdaterRelaunchFailedOutcome(DWORD error);


/*-------------------------------- Layout -------------------------------*/
enum eUpdaterLayout
{
	UPDATER_LAYOUT_NONE = 0,	// no install found
	UPDATER_LAYOUT_ZIP,			// the updater's folder holds PlugY.dll: files are replaced one by one
	UPDATER_LAYOUT_INSTALLER	// an install the installer made: the new installer runs for it
};

/* listed: the installer's Installs list (install folder -> Diablo II folder) names the updater's
   folder, and that Diablo II folder holds PlugY.dll. registered: the installer's single older record
   (InstallPath, PlugYDllPath) does, which names only the install that ran last. An install the
   installer made goes through the installer even with PlugY.dll beside the updater, which also
   brings its uninstaller and Add/Remove Programs entry up to date. */
int UpdaterChooseLayout(bool listed, bool dllBeside, bool registered);

/* The installer's command line for a silent update of one install:
   /S /D2="<d2Dir>" /D=<installDir>. NSIS wants /D= last and unquoted, and reads no option after it.
   Both strings are the installer's registry values as stored, since they key its other records. False
   (out "") when a string is empty, holds a quote or a control character, d2Dir holds " /" (NSIS would
   read an option inside it), or out is too small. */
bool UpdaterBuildInstallerArgs(const wchar_t* d2Dir, const wchar_t* installDir, wchar_t* out, DWORD maxChars);


/*--------------------------- Installed version -------------------------*/
/* Which file's version is the install's. Two installs of one Diablo II folder share its PlugY.dll,
   and updating either replaces it under the other, whose own PlugY.exe still tells its version: the
   older of the two decides. exeVsDll is UpdateCompareVersions(exe, dll), read only when both are
   semver. A version that is not semver (upstream PlugY's, or none) is never up to date. */
enum eUpdaterInstalled
{
	UPDATER_INSTALLED_DLL = 0,		// PlugY.dll's: no PlugY.exe, or one as new or newer
	UPDATER_INSTALLED_EXE,			// PlugY.exe's, older than PlugY.dll's
	UPDATER_INSTALLED_DLL_UNKNOWN,	// PlugY.dll's is not semver
	UPDATER_INSTALLED_EXE_UNKNOWN	// PlugY.exe's is not semver, or could not be read
};
int UpdaterInstalledVersion(bool dllSemver, bool exePresent, bool exeSemver, int exeVsDll);

/* What to do once the newest release is known. latestVsInstalled and latestVsDll are
   UpdateCompareVersions(latest, installed) and (latest, PlugY.dll), read only when that version is
   semver. A release older than the shared PlugY.dll (another install of the folder, on another update
   channel) is refused: installing it would put the older DLL back under that install. */
enum eUpdaterOffer
{
	UPDATER_OFFER_UP_TO_DATE = 0,
	UPDATER_OFFER_SHARED_DLL_NEWER,
	UPDATER_OFFER_UPDATE
};
int UpdaterOffer(bool installedSemver, int latestVsInstalled, bool dllSemver, int latestVsDll);


/*-------------------------------- Paths --------------------------------*/
/* True when rel (separators '\' or '/') names something inside its base: not
   empty, not rooted (\x, /x, \\server), no ':' (drive letters and alternate
   streams), and no component made only of dots and spaces, which Windows
   reads as "." or ".." once it strips the trailing ones. A trailing
   separator (tar lists folders as "PlugY/") is allowed. */
bool UpdaterIsSafeRelativePath(const wchar_t* rel);

// Ordinal, case-insensitive like NTFS; '/' equals '\' and trailing separators are ignored.
bool UpdaterSamePath(const wchar_t* a, const wchar_t* b);
// True when path is base itself or lies below it, compared like UpdaterSamePath.
bool UpdaterIsPathInside(const wchar_t* base, const wchar_t* path);

/* The backup folder name "<installed>-<yyyyMMdd-HHmmss>". installed is the
   installed ProductVersion as read (any text): characters other than
   letters, digits, '.', '-' and '_' become '_', it is cut at 32
   characters, and an empty one becomes "unknown". False when out is too small. */
bool UpdaterBackupFolderName(const char* installed, const SYSTEMTIME* localTime, wchar_t* out, DWORD maxChars);

/* Parses a name made by UpdaterBackupFolderName, plus the "-N" (2-99) that
   tells apart folders made in the same second. key orders them by time.
   False for any other name. */
bool UpdaterParseBackupName(const wchar_t* name, ULONGLONG* key);
/* Marks in remove[] the backup folders beyond the keep newest by createdAt
   (folder creation times; the name's stamp is local time, which can jump).
   Names that are not backup folders are never marked, nor is index protect
   (this run's own backup; pass count for none). Returns how many were marked. */
DWORD UpdaterSelectOldBackups(const wchar_t* const* names, const ULONGLONG* createdAt, DWORD count, DWORD keep,
	DWORD protect, bool* remove);

#define UPDATER_NEW_SUFFIX L".PlugY4Kolbot-new"	// a replacement being copied next to its file
#define UPDATER_WORK_PREFIX L"PlugY4Kolbot-update-"	// %TEMP%\<prefix><V>-<hex>
#define UPDATER_ELEVATED_WORK_PREFIX L"update-"		// %ProgramData%\PlugY4Kolbot\update-<V>-<hex>
#define UPDATER_WORK_RANDOM_HEX 16
// A file name ending in UPDATER_NEW_SUFFIX (case-insensitive) after a non-empty stem.
bool UpdaterIsLeftoverTemp(const wchar_t* name);

/* The security of the elevated work folder and its PlugY4Kolbot parent:
   owned by Administrators (which only an elevated token may set), and a
   protected DACL (nothing inherited from %ProgramData%, where users may
   create folders) giving full control to SYSTEM and Administrators only,
   inherited by everything inside. */
#define UPDATER_ADMIN_ONLY_SDDL L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
// Fills sa from UPDATER_ADMIN_ONLY_SDDL; free sa->lpSecurityDescriptor with LocalFree.
bool UpdaterAdminOnlySecurity(SECURITY_ATTRIBUTES* sa);
// An owner a standard user cannot be: Administrators or SYSTEM.
bool UpdaterIsAdminOwner(PSID owner);

/* "Global\PlugY4KolbotUpdater-<16 hex>" (or Local\), the hex being a 64-bit
   FNV-1a hash of target upper-cased, with '/' as '\' and no trailing
   separator: one updater per install, across sessions. */
bool UpdaterMutexName(const wchar_t* target, bool global, wchar_t* out, DWORD maxChars);


/*------------------------------ File plan ------------------------------*/
enum eUpdaterPlan
{
	UPDATER_PLAN_INSTALL = 0,	// copy over the target (backing up the file it replaces)
	UPDATER_PLAN_KEEP			// the user's PlugY.ini: left untouched
};

/* What to do with one staged file (rel is relative to the staging folder).
   Every file is installed except the root PlugY.ini, which is copied only
   when the target has none: it holds the user's settings, and PlugY takes
   options missing from it from PlugY\PlugYDefault.ini. */
int UpdaterPlanFile(const wchar_t* rel, bool targetHasIni);
bool UpdaterIsUserIni(const wchar_t* rel);
/* The root PlugY.dll, which is replaced after every other file: the installed
   version is read from it, so an interrupted update still reads as the old
   version and the next run finishes the job. */
bool UpdaterApplyLast(const wchar_t* rel);


/*----------------------------- Command line ----------------------------*/
/* Appends arg to cmd (a NUL-terminated command line of maxChars), preceded by a
   space unless cmd is empty, quoted so CommandLineToArgvW and the CRT give back
   exactly arg. False (cmd unchanged) when it does not fit. */
bool UpdaterAppendArg(wchar_t* cmd, DWORD maxChars, const wchar_t* arg);

// What the elevated copy is started with. NULL or "" leaves a value option out.
typedef struct
{
	bool yes;
	bool prerelease;
	bool registryOff;
	const wchar_t* registryBase;
	const wchar_t* workBase;
	const wchar_t* target;
	const wchar_t* fromZip;
	const wchar_t* fromInstaller;
	const wchar_t* sums;
	const char* version;		// --version (offline), ASCII
	const char* expectVersion;	// the release the user confirmed (online), ASCII
} UpdaterRelaunchArgs;

/* The elevated copy's parameters: the given options (paths already absolute),
   then "--elevated". False (out "") when they do not fit. */
bool UpdaterBuildRelaunchArgs(const UpdaterRelaunchArgs* args, wchar_t* out, DWORD maxChars);

/* The elevated copy installs only the release the user confirmed: with
   --expect-version, the release its own query selected must be exactly that
   version; one published during the UAC prompt is refused. */
bool UpdaterExpectedVersionMatches(const char* expectVersion, const char* selectedVersion);


/*------------------------------ Release notes ---------------------------*/
/* Copies the first maxLines lines of notes (UTF-8, LF or CRLF) to out without
   the line ends' CRs, cut at a character boundary when out is too small.
   Returns true when lines (or bytes) were left out. */
bool UpdaterNotesHead(const char* notes, DWORD maxLines, char* out, DWORD outSize);

///////////////////////// END OF FILE ///////////////////////

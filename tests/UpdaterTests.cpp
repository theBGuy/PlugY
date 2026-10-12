/*=================================================================
	Unit tests for src/updater/UpdaterLogic (the updater's decisions
	that need no I/O) and UpdaterIniMerge. Built and run by
	scripts/build.ps1 -Test; exits 1 on any failure. Touches no registry
	key or network, and no file outside a %TEMP% folder of its own.
=================================================================*/

#include "../src/updater/UpdaterLogic.h"
#include "../src/updater/UpdaterIniMerge.h"
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int nbPassed = 0;
static int nbFailed = 0;

static void check(bool ok, const char* text, int line)
{
	if (ok)
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %s\n", line, text);
}

#define CHECK(cond) check((cond) ? true : false, #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkInt((int)(actual), (int)(expected), #actual, __LINE__)
#define CHECK_WSTR(actual, expected) checkWStr((actual), (expected), #actual, __LINE__)
#define CHECK_STR(actual, expected) checkStr((actual), (expected), #actual, __LINE__)

static void checkInt(int actual, int expected, const char* text, int line)
{
	if (actual == expected)
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %s is %d, expected %d\n", line, text, actual, expected);
}

static void checkWStr(const wchar_t* actual, const wchar_t* expected, const char* text, int line)
{
	if (!wcscmp(actual, expected))
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %s is \"%ls\", expected \"%ls\"\n", line, text, actual, expected);
}

static void checkStr(const char* actual, const char* expected, const char* text, int line)
{
	if (!strcmp(actual, expected))
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %s is \"%s\", expected \"%s\"\n", line, text, actual, expected);
}


/*=========================== Exit codes ==========================*/

static void testExitCodes()
{
	printf("exit codes\n");
	CHECK_EQ(UpdaterExitCode(UPDATER_UPDATED), 0);
	CHECK_EQ(UpdaterExitCode(UPDATER_UP_TO_DATE), 0);
	CHECK_EQ(UpdaterExitCode(UPDATER_FAILED), 1);
	CHECK_EQ(UpdaterExitCode(UPDATER_UPDATE_AVAILABLE), 2);
	CHECK_EQ(UpdaterExitCode(UPDATER_FILES_IN_USE), 3);
	CHECK_EQ(UpdaterExitCode(UPDATER_DECLINED), 4);
	CHECK_EQ(UpdaterExitCode(UPDATER_UAC_CANCELLED), 4);
	CHECK_EQ(UpdaterExitCode(UPDATER_INSTALLER_CANCELLED), 4);
	CHECK_EQ(UpdaterExitCode(-1), 1);
	CHECK_EQ(UpdaterExitCode(1000), 1);

	CHECK_EQ(UpdaterInstallerOutcome(0), UPDATER_UPDATED);
	CHECK_EQ(UpdaterInstallerOutcome(1), UPDATER_INSTALLER_CANCELLED);
	CHECK_EQ(UpdaterInstallerOutcome(2), UPDATER_FAILED);
	CHECK_EQ(UpdaterInstallerOutcome(0xC0000005), UPDATER_FAILED);
	CHECK_EQ(UpdaterExitCode(UpdaterInstallerOutcome(1)), 4);
}


/*=========================== Elevation ===========================*/

static void testElevation()
{
	printf("elevation\n");
	CHECK_EQ(UpdaterElevationAction(0, false), UPDATER_ELEVATION_NONE);
	CHECK_EQ(UpdaterElevationAction(0, true), UPDATER_ELEVATION_NONE);
	CHECK_EQ(UpdaterElevationAction(ERROR_ACCESS_DENIED, false), UPDATER_ELEVATION_RELAUNCH);
	// --elevated never relaunches again.
	CHECK_EQ(UpdaterElevationAction(ERROR_ACCESS_DENIED, true), UPDATER_ELEVATION_FAIL);
	CHECK_EQ(UpdaterElevationAction(ERROR_PATH_NOT_FOUND, false), UPDATER_ELEVATION_FAIL);
	CHECK_EQ(UpdaterElevationAction(ERROR_WRITE_PROTECT, false), UPDATER_ELEVATION_FAIL);

	CHECK_EQ(UpdaterRelaunchFailedOutcome(ERROR_CANCELLED), UPDATER_UAC_CANCELLED);
	CHECK_EQ(UpdaterExitCode(UpdaterRelaunchFailedOutcome(ERROR_CANCELLED)), 4);
	CHECK_EQ(UpdaterRelaunchFailedOutcome(ERROR_FILE_NOT_FOUND), UPDATER_FAILED);
	CHECK_EQ(UpdaterExitCode(UpdaterRelaunchFailedOutcome(ERROR_ACCESS_DENIED)), 1);
}


/*============================= Paths =============================*/

static void testSafeRelativePath()
{
	printf("path escape check\n");
	CHECK(UpdaterIsSafeRelativePath(L"PlugY.dll"));
	CHECK(UpdaterIsSafeRelativePath(L"PlugY/"));
	CHECK(UpdaterIsSafeRelativePath(L"PlugY/PlugYDefault.ini"));
	CHECK(UpdaterIsSafeRelativePath(L"PlugY\\statsinterface.txt"));
	CHECK(UpdaterIsSafeRelativePath(L"PlugY_The_Survival_Kit_-_\x8B80\x6211\x6A94\x6848.txt"));
	CHECK(UpdaterIsSafeRelativePath(L"PlugY_The_Survival_Kit_-_????.txt"));	// tar's listing of a non-ANSI name
	CHECK(UpdaterIsSafeRelativePath(L".hidden"));
	CHECK(UpdaterIsSafeRelativePath(L"a..b/c"));
	CHECK(UpdaterIsSafeRelativePath(L"..a"));

	CHECK(!UpdaterIsSafeRelativePath(NULL));
	CHECK(!UpdaterIsSafeRelativePath(L""));
	CHECK(!UpdaterIsSafeRelativePath(L".."));
	CHECK(!UpdaterIsSafeRelativePath(L"../PlugY.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"..\\PlugY.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"PlugY/../../x"));
	CHECK(!UpdaterIsSafeRelativePath(L"PlugY/.."));
	CHECK(!UpdaterIsSafeRelativePath(L"PlugY/../"));
	CHECK(!UpdaterIsSafeRelativePath(L"./PlugY.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"..."));
	CHECK(!UpdaterIsSafeRelativePath(L".. /x"));	// Windows strips the trailing space: ".."
	CHECK(!UpdaterIsSafeRelativePath(L"a/. ./b"));
	CHECK(!UpdaterIsSafeRelativePath(L"/etc/passwd"));
	CHECK(!UpdaterIsSafeRelativePath(L"\\Windows\\x.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"\\\\server\\share\\x"));
	CHECK(!UpdaterIsSafeRelativePath(L"C:\\Windows\\x.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"C:x.dll"));
	CHECK(!UpdaterIsSafeRelativePath(L"PlugY.dll:stream"));
	CHECK(!UpdaterIsSafeRelativePath(L"a//b"));
	CHECK(!UpdaterIsSafeRelativePath(L"a\\\\b"));
	CHECK(!UpdaterIsSafeRelativePath(L"a\nb"));
}

static void testSamePath()
{
	printf("path comparison\n");
	CHECK(UpdaterSamePath(L"C:\\Games\\Diablo II", L"c:\\games\\diablo ii"));
	CHECK(UpdaterSamePath(L"C:\\Games\\Diablo II\\", L"C:\\Games\\Diablo II"));
	CHECK(UpdaterSamePath(L"C:/Games/Diablo II", L"C:\\Games\\Diablo II\\"));
	CHECK(UpdaterSamePath(L"C:\\D\\\x00C9t\x00E9", L"C:\\d\\\x00E9T\x00C9"));
	CHECK(!UpdaterSamePath(L"C:\\Games\\Diablo II", L"C:\\Games\\Diablo II - Plugy"));
	CHECK(!UpdaterSamePath(L"C:\\Games\\Diablo", L"C:\\Games\\Diablo II"));
	CHECK(!UpdaterSamePath(L"", L""));
	CHECK(!UpdaterSamePath(NULL, L"C:\\"));

	CHECK(UpdaterIsPathInside(L"C:\\Temp", L"C:\\Temp\\PlugY4Kolbot-update-14.4.0"));
	CHECK(UpdaterIsPathInside(L"C:\\Temp\\", L"c:\\temp\\x\\y"));
	CHECK(UpdaterIsPathInside(L"C:\\Temp", L"C:\\Temp"));
	CHECK(UpdaterIsPathInside(L"C:\\", L"C:\\Temp"));
	CHECK(!UpdaterIsPathInside(L"C:\\Temp", L"C:\\Temp2\\x"));
	CHECK(!UpdaterIsPathInside(L"C:\\Temp\\a", L"C:\\Temp"));
	CHECK(!UpdaterIsPathInside(L"C:\\Temp", L"D:\\Temp\\x"));
	CHECK(!UpdaterIsPathInside(L"", L"C:\\x"));
}

static void testBackupName()
{
	printf("backup folder name\n");
	SYSTEMTIME t;
	memset(&t, 0, sizeof(t));
	t.wYear = 2026;
	t.wMonth = 10;
	t.wDay = 9;
	t.wHour = 7;
	t.wMinute = 5;
	t.wSecond = 3;
	wchar_t name[MAX_PATH];
	CHECK(UpdaterBackupFolderName("14.3.0", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"14.3.0-20261009-070503");
	CHECK(UpdaterBackupFolderName("14.4.0-rc.1", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"14.4.0-rc.1-20261009-070503");
	CHECK(UpdaterBackupFolderName("14.03", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"14.03-20261009-070503");
	// Anything that could form a path or a reserved character is neutralised.
	CHECK(UpdaterBackupFolderName("..\\x/y:z *?", &t, name, MAX_PATH));
	CHECK_WSTR(name, L".._x_y_z___-20261009-070503");
	CHECK(UpdaterBackupFolderName("", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"unknown-20261009-070503");
	CHECK(UpdaterBackupFolderName(NULL, &t, name, MAX_PATH));
	CHECK_WSTR(name, L"unknown-20261009-070503");
	CHECK(UpdaterBackupFolderName("0123456789012345678901234567890123456789", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"01234567890123456789012345678901-20261009-070503");
	t.wYear = 2027;
	t.wMonth = 12;
	t.wDay = 31;
	t.wHour = 23;
	t.wMinute = 59;
	t.wSecond = 59;
	CHECK(UpdaterBackupFolderName("14.3.0", &t, name, MAX_PATH));
	CHECK_WSTR(name, L"14.3.0-20271231-235959");
	CHECK(UpdaterIsSafeRelativePath(name));
	// Too small: refused, never cut short.
	CHECK(!UpdaterBackupFolderName("14.3.0", &t, name, 22));
	CHECK_WSTR(name, L"");
	CHECK(UpdaterBackupFolderName("14.3.0", &t, name, 23));
}


/*============================ File plan ===========================*/

static void testFilePlan()
{
	printf("file plan\n");
	// The user's PlugY.ini is kept when there is one, installed when there is none.
	CHECK_EQ(UpdaterPlanFile(L"PlugY.ini", true), UPDATER_PLAN_KEEP);
	CHECK_EQ(UpdaterPlanFile(L"PlugY.ini", false), UPDATER_PLAN_INSTALL);
	CHECK_EQ(UpdaterPlanFile(L"plugy.INI", true), UPDATER_PLAN_KEEP);
	// Everything else is replaced, the data folder's INIs included.
	const wchar_t* replaced[] = {
		L"PlugY.dll", L"PlugY.exe", L"PlugY4KolbotUpdater.exe", L"CHANGELOG.md", L"LICENSE.txt",
		L"PlugY_The_Survival_Kit_-_Readme.txt", L"PlugY\\PlugYDefault.ini", L"PlugY\\PlugYFixed.ini",
		L"PlugY\\LocalizedStrings.ini", L"PlugY\\PlugY.ini", L"PlugY\\EmptyPage.dc6", L"PlugY.ini.txt",
		L"xPlugY.ini"
	};
	for (int i = 0; i < (int)(sizeof(replaced) / sizeof(replaced[0])); i++)
	{
		CHECK_EQ(UpdaterPlanFile(replaced[i], true), UPDATER_PLAN_INSTALL);
		CHECK_EQ(UpdaterPlanFile(replaced[i], false), UPDATER_PLAN_INSTALL);
	}
	CHECK(UpdaterIsUserIni(L"PlugY.ini"));
	CHECK(!UpdaterIsUserIni(L"PlugY\\PlugY.ini"));
	CHECK(!UpdaterIsUserIni(NULL));
}


/*========================== Command line ==========================*/

static void testAppendArg()
{
	printf("command line quoting\n");
	wchar_t cmd[256] = L"";
	CHECK(UpdaterAppendArg(cmd, 256, L"--yes"));
	CHECK_WSTR(cmd, L"--yes");
	CHECK(UpdaterAppendArg(cmd, 256, L"--target"));
	CHECK(UpdaterAppendArg(cmd, 256, L"C:\\Program Files (x86)\\Diablo II"));
	CHECK_WSTR(cmd, L"--yes --target \"C:\\Program Files (x86)\\Diablo II\"");

	wcscpy(cmd, L"");
	CHECK(UpdaterAppendArg(cmd, 256, L"C:\\dir with space\\"));
	CHECK_WSTR(cmd, L"\"C:\\dir with space\\\\\"");
	wcscpy(cmd, L"");
	CHECK(UpdaterAppendArg(cmd, 256, L""));
	CHECK_WSTR(cmd, L"\"\"");
	wcscpy(cmd, L"");
	CHECK(UpdaterAppendArg(cmd, 256, L"a\"b"));
	CHECK_WSTR(cmd, L"\"a\\\"b\"");
	wcscpy(cmd, L"");
	CHECK(UpdaterAppendArg(cmd, 256, L"a\\\\\"b c"));
	CHECK_WSTR(cmd, L"\"a\\\\\\\\\\\"b c\"");
	wcscpy(cmd, L"");
	CHECK(UpdaterAppendArg(cmd, 256, L"C:\\no\\space\\"));
	CHECK_WSTR(cmd, L"C:\\no\\space\\");

	// The quoted form survives CommandLineToArgvW's parse unchanged.
	const wchar_t* args[] = { L"--target", L"C:\\Program Files (x86)\\Diablo II\\", L"a \"quoted\" arg", L"", L"x\\\\" };
	wchar_t line[512] = L"prog.exe";
	for (int i = 0; i < 5; i++)
		CHECK(UpdaterAppendArg(line, 512, args[i]));
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(line, &argc);
	CHECK_EQ(argc, 6);
	if (argv && argc == 6)
		for (int i = 0; i < 5; i++)
			CHECK_WSTR(argv[i + 1], args[i]);
	LocalFree(argv);

	// No room: refused and cmd left as it was.
	wchar_t tight[12] = L"--yes";
	CHECK(!UpdaterAppendArg(tight, 12, L"--prerelease"));
	CHECK_WSTR(tight, L"--yes");
	CHECK(!UpdaterAppendArg(tight, 12, L"--abcd"));	// 12 characters + NUL
	CHECK(UpdaterAppendArg(tight, 12, L"--abc"));
	CHECK_WSTR(tight, L"--yes --abc");
}


static void testLayout()
{
	printf("layout, installer command line\n");
	// An install the installer listed goes through the installer, PlugY.dll beside the updater or not.
	CHECK_EQ(UpdaterChooseLayout(true, true, false), UPDATER_LAYOUT_INSTALLER);
	CHECK_EQ(UpdaterChooseLayout(true, false, false), UPDATER_LAYOUT_INSTALLER);
	CHECK_EQ(UpdaterChooseLayout(true, true, true), UPDATER_LAYOUT_INSTALLER);
	CHECK_EQ(UpdaterChooseLayout(false, true, false), UPDATER_LAYOUT_ZIP);
	CHECK_EQ(UpdaterChooseLayout(false, true, true), UPDATER_LAYOUT_ZIP);
	CHECK_EQ(UpdaterChooseLayout(false, false, true), UPDATER_LAYOUT_INSTALLER);
	CHECK_EQ(UpdaterChooseLayout(false, false, false), UPDATER_LAYOUT_NONE);

	wchar_t out[600];
	CHECK(UpdaterBuildInstallerArgs(L"C:\\Program Files (x86)\\Diablo II", L"C:\\Program Files (x86)\\Diablo II\\Mod PlugY", out, 600));
	CHECK_WSTR(out, L"/S /D2=\"C:\\Program Files (x86)\\Diablo II\" /D=C:\\Program Files (x86)\\Diablo II\\Mod PlugY");
	// As stored, a drive root included: NSIS does not read backslashes as escapes.
	CHECK(UpdaterBuildInstallerArgs(L"D:\\", L"D:\\", out, 600));
	CHECK_WSTR(out, L"/S /D2=\"D:\\\" /D=D:\\");
	CHECK(UpdaterBuildInstallerArgs(L"C:/Games/Diablo II", L"c:\\games\\diablo ii", out, 600));
	CHECK_WSTR(out, L"/S /D2=\"C:/Games/Diablo II\" /D=c:\\games\\diablo ii");

	wcscpy(out, L"x");
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\a\"b", L"C:\\x", out, 600));
	CHECK_WSTR(out, L"");
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\x", L"C:\\a\"b", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\a\nb", L"C:\\x", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\x", L"C:\\a\tb", out, 600));
	// NSIS would take " /D=..." inside the Diablo II folder as its own option.
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\a /D=C:\\b", L"C:\\x", out, 600));
	CHECK(UpdaterBuildInstallerArgs(L"C:\\x", L"C:\\a /D=b", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(L"", L"C:\\x", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\x", L"", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(NULL, L"C:\\x", out, 600));
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\x", NULL, out, 600));
	// Exactly fitting, then one short: refused, out empty.
	CHECK(UpdaterBuildInstallerArgs(L"C:\\d", L"C:\\i", out, 22));
	CHECK_WSTR(out, L"/S /D2=\"C:\\d\" /D=C:\\i");
	CHECK(!UpdaterBuildInstallerArgs(L"C:\\d", L"C:\\i", out, 21));
	CHECK_WSTR(out, L"");
}

static void testInstalledVersion()
{
	printf("installed version, update offer\n");
	// Which file decides: the older of PlugY.dll and the install's own PlugY.exe.
	CHECK_EQ(UpdaterInstalledVersion(true, true, true, -1), UPDATER_INSTALLED_EXE);
	CHECK_EQ(UpdaterInstalledVersion(true, true, true, 0), UPDATER_INSTALLED_DLL);
	CHECK_EQ(UpdaterInstalledVersion(true, true, true, 1), UPDATER_INSTALLED_DLL);
	CHECK_EQ(UpdaterInstalledVersion(true, false, false, 0), UPDATER_INSTALLED_DLL);
	// A PlugY.exe without a semver version (upstream's, or unreadable) gets the update.
	CHECK_EQ(UpdaterInstalledVersion(true, true, false, 0), UPDATER_INSTALLED_EXE_UNKNOWN);
	CHECK_EQ(UpdaterInstalledVersion(false, true, true, -1), UPDATER_INSTALLED_DLL_UNKNOWN);
	CHECK_EQ(UpdaterInstalledVersion(false, false, false, 0), UPDATER_INSTALLED_DLL_UNKNOWN);

	// Up to date when the release is not newer than the installed version, a newer shared DLL or not.
	CHECK_EQ(UpdaterOffer(true, 0, true, -1), UPDATER_OFFER_UP_TO_DATE);
	CHECK_EQ(UpdaterOffer(true, -1, true, -1), UPDATER_OFFER_UP_TO_DATE);
	// Older PlugY.exe (14.4.0), shared PlugY.dll 14.6.0-rc.1: stable 14.5.0 is refused, 14.6.0-rc.1 is offered.
	CHECK_EQ(UpdaterOffer(true, 1, true, -1), UPDATER_OFFER_SHARED_DLL_NEWER);
	CHECK_EQ(UpdaterOffer(true, 1, true, 0), UPDATER_OFFER_UPDATE);
	CHECK_EQ(UpdaterOffer(true, 1, true, 1), UPDATER_OFFER_UPDATE);
	// Not semver: always offered, unless the shared PlugY.dll is newer than the release.
	CHECK_EQ(UpdaterOffer(false, 0, true, 0), UPDATER_OFFER_UPDATE);
	CHECK_EQ(UpdaterOffer(false, 0, true, -1), UPDATER_OFFER_SHARED_DLL_NEWER);
	CHECK_EQ(UpdaterOffer(false, 0, false, 0), UPDATER_OFFER_UPDATE);
}


/*========================== Release notes =========================*/

static void testNotesHead()
{
	printf("release notes head\n");
	char out[256];
	CHECK(!UpdaterNotesHead("one\r\ntwo\r\n", 15, out, sizeof(out)));
	CHECK_STR(out, "one\ntwo\n");
	CHECK(UpdaterNotesHead("1\n2\n3\n4", 2, out, sizeof(out)));
	CHECK_STR(out, "1\n2\n");
	CHECK(!UpdaterNotesHead("1\n2\n\n  \r\n", 2, out, sizeof(out)));
	CHECK_STR(out, "1\n2\n");
	CHECK(!UpdaterNotesHead("", 15, out, sizeof(out)));
	CHECK_STR(out, "");
	CHECK(!UpdaterNotesHead(NULL, 15, out, sizeof(out)));
	CHECK_STR(out, "");
	// A lone CR inside a line is kept; only line-end CRs go.
	CHECK(!UpdaterNotesHead("a\rb", 15, out, sizeof(out)));
	CHECK_STR(out, "a\rb\n");
	// Cut at a character boundary: the 3-byte character does not fit after "ab".
	char tight[5];
	CHECK(UpdaterNotesHead("ab\xE8\xAE\x80", 15, tight, sizeof(tight)));
	CHECK_STR(tight, "ab");
	CHECK(!UpdaterNotesHead("\xE8\xAE\x80", 15, tight, sizeof(tight)));
	CHECK_STR(tight, "\xE8\xAE\x80\n");
}


/*========================== Robustness ============================*/

static void testChildAndInstaller()
{
	printf("elevated child exit code, installer self handling\n");
	for (DWORD c = 0; c <= 4; c++)
		CHECK_EQ(UpdaterChildExitCode(c), c);
	CHECK_EQ(UpdaterChildExitCode(5), 1);
	CHECK_EQ(UpdaterChildExitCode(0xC0000005), 1);
	CHECK_EQ(UpdaterChildExitCode(0xFFFFFFFF), 1);

	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_UPDATED, true), UPDATER_SELF_KEEP_NEW);
	// An installer without the updater must not leave the install without one.
	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_UPDATED, false), UPDATER_SELF_RESTORE_OLD);
	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_INSTALLER_CANCELLED, false), UPDATER_SELF_RESTORE_OLD);
	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_INSTALLER_CANCELLED, true), UPDATER_SELF_RESTORE_OLD);
	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_FAILED, true), UPDATER_SELF_RESTORE_OLD);
	CHECK_EQ(UpdaterSelfAfterInstaller(UPDATER_UAC_CANCELLED, false), UPDATER_SELF_RESTORE_OLD);
}

static void testApplyOrder()
{
	printf("apply order\n");
	CHECK(UpdaterApplyLast(L"PlugY.dll"));
	CHECK(UpdaterApplyLast(L"plugy.DLL"));
	CHECK(!UpdaterApplyLast(L"PlugY.exe"));
	CHECK(!UpdaterApplyLast(L"PlugY\\PlugY.dll"));
	CHECK(!UpdaterApplyLast(L"PlugY4KolbotUpdater.exe"));
	CHECK(!UpdaterApplyLast(NULL));
}

static void testLeftoverNames()
{
	printf("leftover names\n");
	CHECK(UpdaterIsLeftoverTemp(L"PlugY.dll.PlugY4Kolbot-new"));
	CHECK(UpdaterIsLeftoverTemp(L"x.plugy4kolbot-NEW"));
	CHECK(!UpdaterIsLeftoverTemp(L".PlugY4Kolbot-new"));
	CHECK(!UpdaterIsLeftoverTemp(L"PlugY.dll.PlugY4Kolbot-new.txt"));
	CHECK(!UpdaterIsLeftoverTemp(L"PlugY.dll"));
	CHECK(!UpdaterIsLeftoverTemp(L"PlugY4Kolbot-new"));
	CHECK(!UpdaterIsLeftoverTemp(NULL));
}

static void testBackupSelection()
{
	printf("backup names and pruning\n");
	ULONGLONG k1 = 0, k2 = 0, k3 = 0;
	CHECK(UpdaterParseBackupName(L"14.3.0-20261009-070503", &k1));
	CHECK(UpdaterParseBackupName(L"14.3.0-20261009-070503-2", &k2));
	CHECK(UpdaterParseBackupName(L"14.4.0-rc.1-20261010-000000", &k3));
	CHECK(k1 < k2 && k2 < k3);
	CHECK(UpdaterParseBackupName(L"unknown-20261009-070503-99", NULL));
	CHECK(UpdaterParseBackupName(L"x-20261009-070503", NULL));
	CHECK(!UpdaterParseBackupName(L"-20261009-070503", NULL));
	CHECK(!UpdaterParseBackupName(L"20261009-070503", NULL));
	CHECK(!UpdaterParseBackupName(L"14.3.0-20261009-07050", NULL));
	CHECK(!UpdaterParseBackupName(L"14.3.0-20261009-070503-1", NULL));
	CHECK(!UpdaterParseBackupName(L"14.3.0-20261009-070503-02", NULL));
	CHECK(!UpdaterParseBackupName(L"14.3.0-20261009-070503-100", NULL));
	CHECK(!UpdaterParseBackupName(L"14 3 0-20261009-070503", NULL));
	CHECK(!UpdaterParseBackupName(L"my saves", NULL));
	CHECK(!UpdaterParseBackupName(L"012345678901234567890123456789012-20261009-070503", NULL));
	CHECK(!UpdaterParseBackupName(NULL, NULL));

	// Creation times decide, not the names' stamps (local time can jump): [6] has the newest name but is the oldest.
	const wchar_t* names[] = {
		L"14.1.0-20260101-000000",
		L"keep me",
		L"14.3.0-20261009-070503",
		L"14.2.0-20260601-120000",
		L"14.3.0-20261009-070503-2",
		L"14.0.0-20250101-000000",
		L"14.9.9-20991231-235959"
	};
	const ULONGLONG created[] = { 300, 999, 600, 400, 700, 200, 100 };
	bool remove[7];
	CHECK_EQ(UpdaterSelectOldBackups(names, created, 7, 3, 7, remove), 3);
	CHECK(remove[0]);
	CHECK(!remove[1]);
	CHECK(!remove[2]);
	CHECK(!remove[3]);
	CHECK(!remove[4]);
	CHECK(remove[5]);
	CHECK(remove[6]);
	CHECK_EQ(UpdaterSelectOldBackups(names, created, 7, 10, 7, remove), 0);
	CHECK_EQ(UpdaterSelectOldBackups(names, created, 2, 3, 2, remove), 0);
	// This run's own backup is never pruned, even when its time looks oldest.
	CHECK_EQ(UpdaterSelectOldBackups(names, created, 7, 3, 6, remove), 2);
	CHECK(!remove[6]);
	CHECK(remove[0] && remove[5]);
	// Equal times: exactly keep survive.
	const wchar_t* twins[] = { L"a-20261009-070503", L"b-20261009-070503", L"c-20261009-070503" };
	const ULONGLONG same[] = { 5, 5, 5 };
	bool twinRemove[3];
	CHECK_EQ(UpdaterSelectOldBackups(twins, same, 3, 2, 3, twinRemove), 1);
	CHECK(twinRemove[2]);
	CHECK_EQ(UpdaterSelectOldBackups(twins, same, 3, 0, 3, twinRemove), 3);
}

static void testRelaunchArgs()
{
	printf("elevated relaunch arguments, confirmed version\n");
	wchar_t out[1024];
	UpdaterRelaunchArgs a;
	memset(&a, 0, sizeof(a));
	CHECK(UpdaterBuildRelaunchArgs(&a, out, 1024));
	CHECK_WSTR(out, L"--elevated");

	// Online run: the confirmed release is pinned.
	a.yes = true;
	a.target = L"C:\\Program Files (x86)\\Diablo II\\";
	a.registryOff = true;
	a.expectVersion = "14.4.0";
	CHECK(UpdaterBuildRelaunchArgs(&a, out, 1024));
	CHECK_WSTR(out, L"--yes --registry-off --target \"C:\\Program Files (x86)\\Diablo II\\\\\" --expect-version 14.4.0 --elevated");
	wchar_t line[1100];
	_snwprintf(line, 1100, L"PlugY4KolbotUpdater.exe %ls", out);
	line[1099] = 0;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(line, &argc);
	const wchar_t* expected[] = { L"--yes", L"--registry-off", L"--target", L"C:\\Program Files (x86)\\Diablo II\\",
		L"--expect-version", L"14.4.0", L"--elevated" };
	CHECK_EQ(argc, 8);
	if (argv && argc == 8)
		for (int i = 0; i < 7; i++)
			CHECK_WSTR(argv[i + 1], expected[i]);
	LocalFree(argv);

	// Every option, in a fixed order; empty values are left out.
	memset(&a, 0, sizeof(a));
	a.prerelease = true;
	a.workBase = L"C:\\wb";
	a.fromZip = L"C:\\z\\PlugY4Kolbot_v14.4.0.zip";
	a.sums = L"C:\\z\\SHA256SUMS.txt";
	a.version = "14.4.0-rc.1";
	a.target = L"";
	a.expectVersion = "";
	CHECK(UpdaterBuildRelaunchArgs(&a, out, 1024));
	CHECK_WSTR(out, L"--prerelease --work-base C:\\wb --from-zip C:\\z\\PlugY4Kolbot_v14.4.0.zip --sums C:\\z\\SHA256SUMS.txt --version 14.4.0-rc.1 --elevated");
	// The installer-layout test options are carried too.
	a.fromZip = NULL;
	a.fromInstaller = L"C:\\z\\PlugY4Kolbot_v14.4.0_Installer.exe";
	a.registryBase = L"SOFTWARE\\PlugY4Kolbot-test";
	CHECK(UpdaterBuildRelaunchArgs(&a, out, 1024));
	CHECK_WSTR(out, L"--prerelease --registry-base SOFTWARE\\PlugY4Kolbot-test --work-base C:\\wb --from-installer C:\\z\\PlugY4Kolbot_v14.4.0_Installer.exe --sums C:\\z\\SHA256SUMS.txt --version 14.4.0-rc.1 --elevated");
	// Too small: refused, out empty.
	CHECK(!UpdaterBuildRelaunchArgs(&a, out, 40));
	CHECK_WSTR(out, L"");
	CHECK(!UpdaterBuildRelaunchArgs(NULL, out, 1024));

	CHECK(UpdaterExpectedVersionMatches(NULL, "14.4.0"));
	CHECK(UpdaterExpectedVersionMatches("", "14.5.0"));
	CHECK(UpdaterExpectedVersionMatches("14.4.0", "14.4.0"));
	CHECK(!UpdaterExpectedVersionMatches("14.4.0", "14.5.0"));
	CHECK(!UpdaterExpectedVersionMatches("14.4.0", "14.4.0-rc.2"));
	CHECK(!UpdaterExpectedVersionMatches("14.4.0", "14.3.0"));
	CHECK(!UpdaterExpectedVersionMatches("14.4.0", ""));
	CHECK(!UpdaterExpectedVersionMatches("14.4.0", NULL));
}

static void testCtrlAndSecurity()
{
	printf("Ctrl handler decision, admin-only security\n");
	CHECK(UpdaterCtrlShouldRestore(CTRL_CLOSE_EVENT, true));
	CHECK(UpdaterCtrlShouldRestore(CTRL_C_EVENT, true));
	CHECK(UpdaterCtrlShouldRestore(CTRL_BREAK_EVENT, true));
	CHECK(!UpdaterCtrlShouldRestore(CTRL_LOGOFF_EVENT, true));
	CHECK(!UpdaterCtrlShouldRestore(CTRL_SHUTDOWN_EVENT, true));
	CHECK(!UpdaterCtrlShouldRestore(CTRL_CLOSE_EVENT, false));
	CHECK(!UpdaterCtrlShouldRestore(CTRL_C_EVENT, false));

	SECURITY_ATTRIBUTES sa;
	CHECK(UpdaterAdminOnlySecurity(&sa));
	CHECK(sa.nLength == sizeof(sa) && sa.lpSecurityDescriptor && !sa.bInheritHandle);
	SECURITY_DESCRIPTOR_CONTROL control = 0;
	DWORD revision = 0;
	CHECK(GetSecurityDescriptorControl(sa.lpSecurityDescriptor, &control, &revision));
	CHECK((control & SE_DACL_PROTECTED) != 0);
	CHECK((control & SE_DACL_PRESENT) != 0);
	LPWSTR sddl = NULL;
	CHECK(ConvertSecurityDescriptorToStringSecurityDescriptorW(sa.lpSecurityDescriptor, SDDL_REVISION_1,
		OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION, &sddl, NULL));
	if (sddl)
	{
		printf("  built: %ls\n", sddl);
		CHECK_WSTR(sddl, L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)");
		LocalFree(sddl);
	}
	BOOL present = FALSE, defaulted = FALSE;
	PACL dacl = NULL;
	CHECK(GetSecurityDescriptorDacl(sa.lpSecurityDescriptor, &present, &dacl, &defaulted) && present && dacl);
	if (dacl)
	{
		CHECK_EQ(dacl->AceCount, 2);
		for (WORD i = 0; i < dacl->AceCount; i++)
		{
			ACCESS_ALLOWED_ACE* ace = NULL;
			CHECK(GetAce(dacl, i, (LPVOID*)&ace) && ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
			if (ace)
			{
				CHECK_EQ(ace->Header.AceFlags, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
				CHECK_EQ(ace->Mask, FILE_ALL_ACCESS);
				PSID sid = (PSID)&ace->SidStart;
				CHECK(IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
			}
		}
	}
	LocalFree(sa.lpSecurityDescriptor);

	BYTE buffer[SECURITY_MAX_SID_SIZE];
	DWORD size = sizeof(buffer);
	CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, buffer, &size) && UpdaterIsAdminOwner(buffer));
	size = sizeof(buffer);
	CHECK(CreateWellKnownSid(WinLocalSystemSid, NULL, buffer, &size) && UpdaterIsAdminOwner(buffer));
	size = sizeof(buffer);
	CHECK(CreateWellKnownSid(WinBuiltinUsersSid, NULL, buffer, &size) && !UpdaterIsAdminOwner(buffer));
	size = sizeof(buffer);
	CHECK(CreateWellKnownSid(WinWorldSid, NULL, buffer, &size) && !UpdaterIsAdminOwner(buffer));
	// The account running the tests (a standard or filtered admin token's user) is never trusted as an owner.
	HANDLE token;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		BYTE user[256];
		DWORD len = 0;
		if (GetTokenInformation(token, TokenUser, user, sizeof(user), &len))
			CHECK(!UpdaterIsAdminOwner(((TOKEN_USER*)user)->User.Sid));
		CloseHandle(token);
	}
	CHECK(!UpdaterIsAdminOwner(NULL));
}

static void testMutexName()
{
	printf("mutex name\n");
	wchar_t a[128];
	wchar_t b[128];
	CHECK(UpdaterMutexName(L"C:\\Games\\Diablo II", true, a, 128));
	CHECK(!wcsncmp(a, L"Global\\PlugY4KolbotUpdater-", 27));
	CHECK_EQ(wcslen(a), 27 + 16);
	CHECK(UpdaterMutexName(L"c:/games/diablo ii\\", true, b, 128));
	CHECK_WSTR(b, a);
	CHECK(UpdaterMutexName(L"C:\\D\\\x00C9t\x00E9", true, a, 128));
	CHECK(UpdaterMutexName(L"C:\\d\\\x00E9T\x00C9", true, b, 128));
	CHECK_WSTR(b, a);
	CHECK(UpdaterMutexName(L"C:\\Games\\Diablo II - Plugy", true, b, 128));
	CHECK(UpdaterMutexName(L"C:\\Games\\Diablo II", true, a, 128));
	CHECK(wcscmp(a, b) != 0);
	CHECK(UpdaterMutexName(L"C:\\Games\\Diablo II", false, b, 128));
	CHECK(!wcsncmp(b, L"Local\\PlugY4KolbotUpdater-", 26));
	CHECK_WSTR(b + 6, a + 7);
	CHECK(!UpdaterMutexName(L"", true, a, 128));
	CHECK(!UpdaterMutexName(L"C:\\x", true, a, 20));
}


/*============================ INI merge ===========================*/
static wchar_t mergeDir[MAX_PATH];

static void mergePath(wchar_t* out, const wchar_t* name)
{
	_snwprintf(out, MAX_PATH, L"%ls\\%ls", mergeDir, name);
	out[MAX_PATH - 1] = 0;
}

static bool writeBytes(const wchar_t* name, const char* bytes)
{
	wchar_t path[MAX_PATH];
	mergePath(path, name);
	SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD len = (DWORD)strlen(bytes);
	DWORD written = 0;
	bool ok = WriteFile(file, bytes, len, &written, NULL) && written == len;
	CloseHandle(file);
	return ok;
}

// The file's bytes as a string ("" when unreadable), in a static buffer.
static const char* readBytes(const wchar_t* name)
{
	static char buffer[4096];
	wchar_t path[MAX_PATH];
	mergePath(path, name);
	buffer[0] = 0;
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return buffer;
	DWORD read = 0;
	if (ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL))
		buffer[read] = 0;
	CloseHandle(file);
	return buffer;
}

static const wchar_t* iniValue(const wchar_t* name, const wchar_t* section, const wchar_t* key)
{
	static wchar_t value[256];
	wchar_t path[MAX_PATH];
	mergePath(path, name);
	GetPrivateProfileStringW(section, key, L"<missing>", value, 256, path);
	return value;
}

static int merge(const wchar_t* user)
{
	wchar_t ini[MAX_PATH], templ[MAX_PATH], defaults[MAX_PATH];
	mergePath(ini, user);
	mergePath(templ, L"templ.ini");
	mergePath(defaults, L"defaults.ini");
	return UpdaterAddMissingOptions(ini, templ, defaults);
}

static void testAddMissingOptions()
{
	wchar_t temp[MAX_PATH];
	GetTempPathW(MAX_PATH, temp);
	_snwprintf(mergeDir, MAX_PATH, L"%lsPlugYUpdaterTests-%lu", temp, GetCurrentProcessId());
	mergeDir[MAX_PATH - 1] = 0;
	CreateDirectoryW(mergeDir, NULL);
	CHECK(writeBytes(L"templ.ini",
		"; head\r\n[LAUNCHING]\r\nParam=\r\nLibrary=PlugY.dll\r\n\r\n[GENERAL]\r\nA=1\r\n;Comment=1\r\nB=1\r\n\r\n[NEW]\r\nC=5\r\nD=\r\n;;;;"));
	CHECK(writeBytes(L"defaults.ini", "[LAUNCHING]\r\nLibrary=PlugY.dll\r\n[GENERAL]\r\nA=0\r\nB=0\r\n[NEW]\r\nC=0\r\n"));

	// The values PlugY already used: B and C from the defaults, D from the release's file (no default); [LAUNCHING] alone.
	CHECK(writeBytes(L"user.ini", "[LAUNCHING]\r\nParam=\r\n\r\n[GENERAL]\r\nA=7\r\n\r\n[END]\r\nE=1"));
	CHECK_EQ(merge(L"user.ini"), 3);
	CHECK_WSTR(iniValue(L"user.ini", L"GENERAL", L"A"), L"7");
	CHECK_WSTR(iniValue(L"user.ini", L"GENERAL", L"B"), L"0");
	CHECK_WSTR(iniValue(L"user.ini", L"GENERAL", L"Comment"), L"<missing>");
	CHECK_WSTR(iniValue(L"user.ini", L"NEW", L"C"), L"0");
	CHECK_WSTR(iniValue(L"user.ini", L"NEW", L"D"), L"");
	CHECK_WSTR(iniValue(L"user.ini", L"LAUNCHING", L"Library"), L"<missing>");
	CHECK_WSTR(iniValue(L"user.ini", L"END", L"E"), L"1");
	const char* merged = readBytes(L"user.ini");
	CHECK(strstr(merged, "[LAUNCHING]\r\nParam=\r\n") == merged);
	CHECK(strstr(merged, "E=1\r\n\r\n[NEW]\r\nC=0\r\nD=\r\n") != NULL);	// a new section after an empty line
	char before[4096];
	strcpy(before, merged);
	CHECK_EQ(merge(L"user.ini"), 0);	// a second update changes nothing
	CHECK_STR(readBytes(L"user.ini"), before);

	// An empty value is an option already there; a file ending in a line end gets one more for the empty line.
	CHECK(writeBytes(L"empty-value.ini", "[GENERAL]\r\nA=\r\nB=1\r\n"));
	CHECK_EQ(merge(L"empty-value.ini"), 2);
	CHECK_WSTR(iniValue(L"empty-value.ini", L"GENERAL", L"A"), L"");
	CHECK(strstr(readBytes(L"empty-value.ini"), "B=1\r\n\r\n[NEW]\r\nC=0\r\n") != NULL);

	// An empty file gets every option but [LAUNCHING]'s, with no empty line first.
	CHECK(writeBytes(L"blank.ini", ""));
	CHECK_EQ(merge(L"blank.ini"), 4);
	CHECK(strncmp(readBytes(L"blank.ini"), "[GENERAL]\r\n", 11) == 0);

	// [launching] in any case is left alone; a section the file knows by its second option gets no empty line; a header
	// without its "]" names no section; the release file's last line, with no line end, still counts.
	CHECK(writeBytes(L"templ2.ini", "[launching]\r\nLibrary=x\r\n[NEW]\r\nC=5\r\nD=6\r\n[BROKEN\r\nZ=1\r\n[LAST]\r\nL=9"));
	CHECK(writeBytes(L"user2.ini", "[NEW]\r\nD=1\r\n"));
	{
		wchar_t user2[MAX_PATH], templ2[MAX_PATH], noDefaults[MAX_PATH];
		mergePath(user2, L"user2.ini");
		mergePath(templ2, L"templ2.ini");
		mergePath(noDefaults, L"no-defaults.ini");
		CHECK_EQ(UpdaterAddMissingOptions(user2, templ2, noDefaults), 2);
	}
	CHECK_WSTR(iniValue(L"user2.ini", L"NEW", L"C"), L"5");
	CHECK_WSTR(iniValue(L"user2.ini", L"NEW", L"D"), L"1");
	CHECK_WSTR(iniValue(L"user2.ini", L"LAST", L"L"), L"9");
	CHECK_WSTR(iniValue(L"user2.ini", L"BROKEN", L"Z"), L"<missing>");
	CHECK_WSTR(iniValue(L"user2.ini", L"launching", L"Library"), L"<missing>");
	CHECK_STR(readBytes(L"user2.ini"), "[NEW]\r\nD=1\r\nC=5\r\n\r\n[LAST]\r\nL=9\r\n");

	// An empty default is a value: a missing SavePath gets PlugYDefault.ini's empty one, not the release's "Mod PlugY".
	CHECK(writeBytes(L"templ3.ini", "[SAVEPATH]\r\nActiveSavePathChange=0\r\nSavePath=Mod PlugY\r\n"));
	CHECK(writeBytes(L"defaults3.ini", "[SAVEPATH]\r\nActiveSavePathChange=0\r\nSavePath=\r\n"));
	CHECK(writeBytes(L"user3.ini", "[SAVEPATH]\r\nActiveSavePathChange=1\r\n"));
	{
		wchar_t user3[MAX_PATH], templ3[MAX_PATH], defaults3[MAX_PATH];
		mergePath(user3, L"user3.ini");
		mergePath(templ3, L"templ3.ini");
		mergePath(defaults3, L"defaults3.ini");
		CHECK_EQ(UpdaterAddMissingOptions(user3, templ3, defaults3), 1);
	}
	CHECK_WSTR(iniValue(L"user3.ini", L"SAVEPATH", L"SavePath"), L"");
	CHECK_STR(readBytes(L"user3.ini"), "[SAVEPATH]\r\nActiveSavePathChange=1\r\nSavePath=\r\n");

	// No release file to read, or a file that can't be written: -1, and nothing changes.
	wchar_t ini[MAX_PATH], missing[MAX_PATH], defaults[MAX_PATH];
	mergePath(ini, L"empty-value.ini");
	mergePath(missing, L"no-such.ini");
	mergePath(defaults, L"defaults.ini");
	CHECK_EQ(UpdaterAddMissingOptions(ini, missing, defaults), -1);
	CHECK_EQ(UpdaterAddMissingOptions(NULL, missing, defaults), -1);
	CHECK(writeBytes(L"readonly.ini", "[GENERAL]\r\nA=1\r\n"));
	wchar_t readonlyPath[MAX_PATH];
	mergePath(readonlyPath, L"readonly.ini");
	SetFileAttributesW(readonlyPath, FILE_ATTRIBUTE_READONLY);
	CHECK_EQ(merge(L"readonly.ini"), -1);
	CHECK_STR(readBytes(L"readonly.ini"), "[GENERAL]\r\nA=1\r\n");
	SetFileAttributesW(readonlyPath, FILE_ATTRIBUTE_NORMAL);

	const wchar_t* names[] = { L"templ.ini", L"defaults.ini", L"user.ini", L"empty-value.ini", L"blank.ini", L"readonly.ini",
		L"templ2.ini", L"user2.ini", L"templ3.ini", L"defaults3.ini", L"user3.ini" };
	for (size_t i = 0; i < _countof(names); i++)
	{
		wchar_t path[MAX_PATH];
		mergePath(path, names[i]);
		DeleteFileW(path);
	}
	RemoveDirectoryW(mergeDir);
}


/*============================== Main =============================*/

int main(int argc, char** argv)
{
	if (argc > 1)
	{
		printf("usage: UpdaterTests\n");
		return 2;
	}
	printf("UpdaterTests\n");
	testExitCodes();
	testElevation();
	testSafeRelativePath();
	testSamePath();
	testBackupName();
	testFilePlan();
	testAppendArg();
	testLayout();
	testInstalledVersion();
	testNotesHead();
	testChildAndInstaller();
	testApplyOrder();
	testLeftoverNames();
	testBackupSelection();
	testMutexName();
	testCtrlAndSecurity();
	testRelaunchArgs();
	testAddMissingOptions();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

///////////////////////// END OF FILE ///////////////////////

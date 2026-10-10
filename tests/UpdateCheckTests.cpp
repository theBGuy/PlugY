/*=================================================================
	Unit tests for src/common/UpdateCheck and src/common/UpdateDownload.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.

	UpdateCheckTests.exe [--fixtures DIR] [--scratch DIR] [--live]

	--fixtures defaults to tests\fixtures (relative to the working
	directory), --scratch to a scratch folder beside the exe. The
	default run never touches the network; --live adds checks against
	the real GitHub API.
=================================================================*/

#include "../src/common/UpdateCheck.h"
#include "../src/common/UpdateDownload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define CHECK_STR(actual, expected) checkStr((actual), (expected), #actual, __LINE__)
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define HTTP_TEST_URL_MAX 8192	// the transport's URL buffer size

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

static char fixtureDir[MAX_PATH] = "tests\\fixtures";
static char scratchDir[MAX_PATH] = "";


/*========================== Helpers ==========================*/

/* Copies len bytes so they end exactly where a PAGE_NOACCESS page starts:
   a parser reading even one byte past len faults the test run. */
typedef struct
{
	BYTE* base;
	BYTE* guard;
} Guarded;

static void guardedInit(Guarded* g, DWORD maxLen)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	DWORD page = si.dwPageSize;
	DWORD dataPages = (maxLen + page - 1) / page + 1;
	g->base = (BYTE*)VirtualAlloc(NULL, (dataPages + 1) * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	g->guard = g->base + dataPages * page;
	DWORD old;
	VirtualProtect(g->guard, page, PAGE_NOACCESS, &old);
}

static const char* guardedPlace(Guarded* g, const char* data, DWORD len)
{
	BYTE* start = g->guard - len;
	memcpy(start, data, len);
	return (const char*)start;
}

static void guardedFree(Guarded* g)
{
	VirtualFree(g->base, 0, MEM_RELEASE);
}

static UpdateRelease release;	// ~16 KB, too big to put on the stack of every test

static int selectGuarded(const char* json, DWORD len, bool includePrerelease)
{
	Guarded g;
	guardedInit(&g, len);
	int result = UpdateSelectRelease(guardedPlace(&g, json, len), len, includePrerelease, &release);
	guardedFree(&g);
	return result;
}

static int selectText(const char* json, bool includePrerelease)
{
	return selectGuarded(json, (DWORD)strlen(json), includePrerelease);
}

static char* readFixture(const char* name, DWORD* len)
{
	char path[MAX_PATH];
	_snprintf(path, sizeof(path), "%s\\%s", fixtureDir, name);
	path[MAX_PATH - 1] = 0;
	*len = 0;
	FILE* f = fopen(path, "rb");
	if (!f)
	{
		printf("  cannot open fixture %s\n", path);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* data = (char*)malloc(size + 1);
	*len = (DWORD)fread(data, 1, size, f);
	data[*len] = 0;
	fclose(f);
	return data;
}

static void scratchPath(char* out, const char* name)
{
	_snprintf(out, MAX_PATH, "%s\\%s", scratchDir, name);
	out[MAX_PATH - 1] = 0;
}

static void scratchPathW(wchar_t* out, const char* name)
{
	char path[MAX_PATH];
	scratchPath(path, name);
	MultiByteToWideChar(CP_ACP, 0, path, -1, out, MAX_PATH);
}

static bool writeBytes(const char* path, const char* data, DWORD len)
{
	FILE* f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, len, f) == len;
	fclose(f);
	return ok;
}

static bool writeTextW(const wchar_t* path, const char* text)
{
	FILE* f = _wfopen(path, L"wb");
	if (!f)
		return false;
	size_t len = strlen(text);
	bool ok = fwrite(text, 1, len, f) == len;
	fclose(f);
	return ok;
}

static bool fileExists(const char* path)
{
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int countScratchFiles(const char* pattern)
{
	char path[MAX_PATH];
	scratchPath(path, pattern);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(path, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	int n = 0;
	do n++; while (FindNextFileA(h, &fd));
	FindClose(h);
	return n;
}

static int discardSink(void* context, const BYTE* data, DWORD size)
{
	return UPDATE_OK;
}

static int sign(int v)
{
	return v < 0 ? -1 : v > 0 ? 1 : 0;
}


/*========================== Semver ===========================*/

static void testSemver()
{
	printf("semver\n");
	UpdateVersion v;
	CHECK(UpdateParseVersion("14.3.0", &v));
	CHECK(v.major == 14 && v.minor == 3 && v.patch == 0 && v.channel == UPDATE_PRE_NONE && v.number == 0);
	CHECK(UpdateParseVersion("14.4.0-rc.2", &v));
	CHECK(v.major == 14 && v.minor == 4 && v.patch == 0 && v.channel == UPDATE_PRE_RC && v.number == 2);
	CHECK(UpdateParseVersion("1.2.3-alpha.0", &v) && v.channel == UPDATE_PRE_ALPHA && v.number == 0);
	CHECK(UpdateParseVersion("0.0.0-beta.10", &v) && v.channel == UPDATE_PRE_BETA && v.number == 10);
	CHECK(UpdateParseVersion("999999999.0.0", &v) && v.major == 999999999);

	// The malformed list of scripts/version-lib.test.mjs, then the extra edges of this parser.
	const char* bad[] = {
		"14.03", "v14.3.0", "14.3", "01.3.0", "14.3.0-rc", "14.3.0-rc.01", "14.3.0-RC.1",
		"14.3.0-preview.1", "14.3.0+build", "",
		"1234567890.0.0", "14.3.0 ", " 14.3.0", "14.3.0-rc.1.2", "14.3.0-", "14.3.", "-1.0.0",
		"14.3.0-beta.1-rc.1", "14.03.0", "14.3.00", "14..0", "14.3.0-rc.-1", "1.2.3\n",
	};
	for (int i = 0; i < COUNT(bad); i++)
	{
		bool parsed = UpdateParseVersion(bad[i], &v);
		check(!parsed, bad[i], __LINE__);
	}
	CHECK(!UpdateParseVersion(NULL, &v));

	CHECK(UpdateParseTag("v14.3.0", &v) && v.major == 14);
	CHECK(UpdateParseTag("14.3.0", &v));
	CHECK(UpdateParseTag("v14.5.0-rc.1", &v) && v.channel == UPDATE_PRE_RC);
	const char* badTags[] = { "V14.3.0", "vv14.3.0", "v14.03", "12.00", "v", "", "v14.04.0", "release-14.3.0" };
	for (int i = 0; i < COUNT(badTags); i++)
	{
		bool parsed = UpdateParseTag(badTags[i], &v);
		check(!parsed, badTags[i], __LINE__);
	}

	// The precedence table of scripts/version-lib.test.mjs, every pair both ways.
	const char* ordered[] = {
		"14.3.0", "14.3.1", "14.4.0-alpha.1", "14.4.0-alpha.2", "14.4.0-alpha.10", "14.4.0-beta.1",
		"14.4.0-rc.1", "14.4.0-rc.2", "14.4.0", "14.10.0", "15.0.0",
	};
	const int n = COUNT(ordered);
	UpdateVersion parsed[n];
	for (int i = 0; i < n; i++)
		CHECK(UpdateParseVersion(ordered[i], &parsed[i]));
	int wrong = 0;
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++)
			if (UpdateCompareVersions(&parsed[i], &parsed[j]) != sign(i - j))
			{
				printf("  compare %s vs %s gave %d\n", ordered[i], ordered[j], UpdateCompareVersions(&parsed[i], &parsed[j]));
				wrong++;
			}
	CHECK_EQ(wrong, 0);
}


/*======================= Release parsing =====================*/

#define SHA_1 "9e3779b19e3817e89e38b61f9e3954569e39f28d9e3a90c49e3b2efb9e3bcd32"
#define SHA_2 "3c6ef3623c6f91993c702fd03c70ce073c716c3e3c720a753c72a8ac3c7346e3"
#define SHA_115 "12ebaa8312ec48ba12ece6f112ed852812ee235f12eec19612ef5fcd12effe04"
#define SHA_ABC "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define PAGE_BASE "https://github.com/theBGuy/PlugY4Kolbot/releases/tag/"

static void testReleasesFixture()
{
	printf("releases.json\n");
	DWORD len;
	char* json = readFixture("releases.json", &len);
	CHECK(json != NULL);
	if (!json)
		return;

	// Stable: the drafted v15.0.0, the flagged or -beta/-rc tags, v99/v98 missing a flag and
	// the non-semver tags all lose to v14.4.0, which is not first in the list.
	CHECK_EQ(selectGuarded(json, len, false), UPDATE_OK);
	CHECK_STR(release.tag, "v14.4.0");
	CHECK(release.version.major == 14 && release.version.minor == 4 && release.version.patch == 0);
	CHECK(release.version.channel == UPDATE_PRE_NONE);
	CHECK(!release.prerelease);
	CHECK_STR(release.pageUrl, PAGE_BASE "v14.4.0");
	CHECK_STR(release.notes, "## Fixed\r\n\r\n- Big Stash keeps its items.\r\n");
	CHECK(!release.notesTruncated);
	CHECK_EQ(release.nbAssets, 6);
	CHECK(!release.assetsOmitted);
	CHECK_STR(release.assets[0].name, "PlugY4Kolbot_v14.4.0.zip");
	CHECK_STR(release.assets[0].url, "https://github.com/theBGuy/PlugY4Kolbot/releases/download/v14.4.0/PlugY4Kolbot_v14.4.0.zip");
	CHECK_STR(release.assets[0].sha256, SHA_1);
	CHECK_STR(release.assets[1].sha256, SHA_2);	// published uppercase, stored lowercase
	CHECK_STR(release.assets[3].name, "PlugY4Kolbot_v14.4.0_symbols.zip");
	CHECK_STR(release.assets[3].sha256, "");	// digest null
	CHECK_STR(release.assets[4].name, "SHA256SUMS.txt");
	CHECK_STR(release.assets[4].sha256, "");	// digest absent
	CHECK_STR(release.assets[5].sha256, "");	// sha1 digest

	// Pre-release channel: v14.6.0-beta.1 is GitHub-flagged as a release but its tag says beta.
	CHECK_EQ(selectGuarded(json, len, true), UPDATE_OK);
	CHECK_STR(release.tag, "v14.6.0-beta.1");
	CHECK(release.prerelease);
	CHECK(release.version.channel == UPDATE_PRE_BETA && release.version.number == 1);
	CHECK_STR(release.notes, "");	// body null
	CHECK_EQ(release.nbAssets, 0);

	Guarded g;
	guardedInit(&g, len);
	const char* placed = guardedPlace(&g, json, len);
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, "v14.03", &release), UPDATE_OK);
	CHECK_STR(release.tag, "v14.03");
	CHECK(release.version.major == 0 && release.version.minor == 0 && !release.prerelease);
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, "14.4.1", &release), UPDATE_OK);
	CHECK(release.prerelease);	// flagged by GitHub only
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, "v15.0.0", &release), UPDATE_NO_RELEASE);	// draft
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, "v99.0.0", &release), UPDATE_NO_RELEASE);	// no draft field
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, "v14.4", &release), UPDATE_NO_RELEASE);
	CHECK_EQ(UpdateFindReleaseByTag(placed, len, NULL, &release), UPDATE_BAD_ARGUMENT);

	// Every proper prefix is truncated JSON. The guard page catches any read past the prefix.
	DWORD last = len;
	while (last > 0 && json[last - 1] != ']')
		last--;
	int wrong = 0;
	for (DWORD n = 0; n < last; n++)
	{
		int r = UpdateSelectRelease(guardedPlace(&g, json, n), n, true, &release);
		if (r != UPDATE_BAD_RESPONSE || release.tag[0])
			wrong++;
	}
	CHECK_EQ(wrong, 0);
	CHECK_EQ(UpdateSelectRelease(guardedPlace(&g, json, last), last, false, &release), UPDATE_OK);
	guardedFree(&g);
	free(json);
}

static void testEscapesFixture()
{
	printf("releases-escapes.json\n");
	DWORD len;
	char* json = readFixture("releases-escapes.json", &len);
	CHECK(json != NULL);
	if (!json)
		return;
	CHECK_EQ(selectGuarded(json, len, false), UPDATE_OK);
	CHECK_STR(release.tag, "v1.2.3");	// spelled \u00761.2.3
	CHECK_STR(release.pageUrl, PAGE_BASE "v1.2.3");	// spelled with \/
	CHECK_STR(release.notes,
		"Quote \" backslash \\ slash / b\b f\f n\n r\r t\t e-acute \xC3\xA9 CJK \xE4\xB8\xAD"
		" emoji \xF0\x9F\x98\x80 upper \xC3\x89 end");
	CHECK_EQ(release.nbAssets, 1);
	CHECK_STR(release.assets[0].name, "Asset \xC3\xA9.zip");
	CHECK_STR(release.assets[0].sha256, SHA_ABC);

	// Every single-byte mutation and deletion must come back as a result, never a crash or an over-read.
	static const char replacements[] = { '"', '\\', '{', '}', '[', ']', ',', ':', 'u', '0', 'e', '-', ' ', '\0', '\x1F', '\xFF' };
	char* copy = (char*)malloc(len);
	Guarded g;
	guardedInit(&g, len);
	int unexpected = 0;
	for (DWORD i = 0; i < len; i++)
	{
		for (int k = 0; k < COUNT(replacements); k++)
		{
			memcpy(copy, json, len);
			copy[i] = replacements[k];
			int r = UpdateSelectRelease(guardedPlace(&g, copy, len), len, true, &release);
			if (r != UPDATE_OK && r != UPDATE_NO_RELEASE && r != UPDATE_BAD_RESPONSE)
				unexpected++;
		}
		memcpy(copy, json, i);
		memcpy(copy + i, json + i + 1, len - i - 1);
		int r = UpdateSelectRelease(guardedPlace(&g, copy, len - 1), len - 1, true, &release);
		if (r != UPDATE_OK && r != UPDATE_NO_RELEASE && r != UPDATE_BAD_RESPONSE)
			unexpected++;
	}
	CHECK_EQ(unexpected, 0);
	guardedFree(&g);
	free(copy);
	free(json);
}

static void testManyAssetsFixture()
{
	printf("releases-many-assets.json\n");
	DWORD len;
	char* json = readFixture("releases-many-assets.json", &len);
	CHECK(json != NULL);
	if (!json)
		return;
	CHECK_EQ(selectGuarded(json, len, false), UPDATE_OK);
	CHECK_STR(release.tag, "v2.0.0");
	CHECK_EQ(release.nbAssets, UPDATE_MAX_ASSETS);
	CHECK(release.assetsOmitted);
	CHECK_STR(release.assets[0].name, "asset0.zip");
	CHECK_STR(release.assets[15].name, "asset15.zip");
	CHECK_STR(release.assets[15].sha256, SHA_115);
	free(json);
}

#define REL(tag, extra) "{\"tag_name\":\"" tag "\",\"draft\":false,\"prerelease\":false" extra "}"

static void testSelectEdges()
{
	printf("selection edges\n");
	CHECK_EQ(selectText("[]", false), UPDATE_NO_RELEASE);
	CHECK_EQ(selectText(" \r\n[ \t]\r\n", true), UPDATE_NO_RELEASE);
	CHECK_EQ(selectText("[" REL("v14.03", "") "," REL("12.00", "") "]", true), UPDATE_NO_RELEASE);
	CHECK_EQ(selectText("[{\"tag_name\":\"v1.0.0-rc.1\",\"draft\":false,\"prerelease\":true}]", false), UPDATE_NO_RELEASE);
	CHECK_EQ(selectText("[{\"tag_name\":\"v1.0.0-rc.1\",\"draft\":false,\"prerelease\":true}]", true), UPDATE_OK);
	CHECK_STR(release.tag, "v1.0.0-rc.1");
	// A release outranks its own pre-releases in the pre-release channel too.
	CHECK_EQ(selectText("[" REL("v1.0.0-rc.9", "") "," REL("v1.0.0", "") "," REL("v1.0.0-beta.3", "") "]", true), UPDATE_OK);
	CHECK_STR(release.tag, "v1.0.0");
	// Equal versions: the first listed wins.
	CHECK_EQ(selectText("[" REL("1.0.0", ",\"html_url\":\"https://a\"") "," REL("v1.0.0", ",\"html_url\":\"https://b\"") "]", false), UPDATE_OK);
	CHECK_STR(release.pageUrl, "https://a");
	// Duplicate keys: the last one wins.
	CHECK_EQ(selectText("[{\"tag_name\":\"v9.0.0\",\"tag_name\":\"v1.0.0\",\"draft\":true,\"draft\":false,\"prerelease\":false}]", false), UPDATE_OK);
	CHECK_STR(release.tag, "v1.0.0");
	// A key longer than the key buffer matches nothing.
	CHECK_EQ(selectText("[{\"tag_name_but_much_longer_than_32_characters\":\"v9.0.0\",\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":false}]", false), UPDATE_OK);
	CHECK_STR(release.tag, "v1.0.0");
	// Unusable URLs and odd digests.
	CHECK_EQ(selectText("[" REL("v1.0.0", ",\"html_url\":\"http://github.com/x\",\"assets\":["
		"{\"name\":\"a.zip\",\"browser_download_url\":\"http://github.com/a.zip\"},"
		"{\"name\":\"\",\"browser_download_url\":\"https://github.com/b.zip\"},"
		"{\"browser_download_url\":\"https://github.com/c.zip\"},"
		"{\"name\":\"d.zip\",\"browser_download_url\":\"https://github.com/d.zip\",\"digest\":\"sha256:" SHA_ABC "0\"},"
		"{\"name\":\"e.zip\",\"browser_download_url\":\"https://github.com/e.zip\",\"digest\":\"SHA256:" SHA_ABC "\"},"
		"{\"name\":\"f.zip\",\"browser_download_url\":\"https://github.com/f.zip\",\"digest\":\"sha256:ba7816bf\"},"
		"{\"name\":\"g.zip\",\"browser_download_url\":\"https://github.com/g.zip\",\"digest\":\"sha256:" SHA_ABC "\"}]") "]", false), UPDATE_OK);
	CHECK_STR(release.pageUrl, "");
	CHECK(release.assetsOmitted);
	CHECK_EQ(release.nbAssets, 4);
	CHECK_STR(release.assets[0].name, "d.zip");
	CHECK_STR(release.assets[0].sha256, "");
	CHECK_STR(release.assets[1].sha256, "");
	CHECK_STR(release.assets[2].sha256, "");
	CHECK_STR(release.assets[3].sha256, SHA_ABC);

	// Oversized strings: a long tag is not semver, a long URL is dropped, the notes are cut on a character boundary.
	static char json[16384];
	char filler[5000];
	memset(filler, 'x', 600);
	filler[600] = 0;
	_snprintf(json, sizeof(json), "[" REL("v1.0.0%s", "") "]", filler);
	CHECK_EQ(selectText(json, true), UPDATE_NO_RELEASE);
	_snprintf(json, sizeof(json), "[" REL("v1.0.0", ",\"html_url\":\"https://github.com/%s\",\"assets\":["
		"{\"name\":\"a.zip\",\"browser_download_url\":\"https://github.com/%s\"},"
		"{\"name\":\"%s\",\"browser_download_url\":\"https://github.com/b.zip\"}]") "]", filler, filler, filler);
	CHECK_EQ(selectText(json, true), UPDATE_OK);
	CHECK_STR(release.pageUrl, "");
	CHECK_EQ(release.nbAssets, 0);
	CHECK(release.assetsOmitted);

	struct { const char* tail; DWORD before; DWORD kept; } cuts[] = {
		{ "\\u00e9", UPDATE_NOTES_MAX - 2, UPDATE_NOTES_MAX - 2 },			// escaped 2-byte char, 1 byte left
		{ "\xC3\xA9", UPDATE_NOTES_MAX - 2, UPDATE_NOTES_MAX - 2 },			// raw 2-byte char, split
		{ "\xE4\xB8\xAD", UPDATE_NOTES_MAX - 3, UPDATE_NOTES_MAX - 3 },		// raw 3-byte char, split after 2
		{ "\xF0\x9F\x98\x80", UPDATE_NOTES_MAX - 2, UPDATE_NOTES_MAX - 2 },	// raw 4-byte char, split after 1
		{ "\xC3\xA9", UPDATE_NOTES_MAX - 3, UPDATE_NOTES_MAX - 1 },			// fits exactly
	};
	for (int i = 0; i < COUNT(cuts); i++)
	{
		memset(filler, 'a', cuts[i].before);
		filler[cuts[i].before] = 0;
		_snprintf(json, sizeof(json), "[" REL("v1.0.0", ",\"body\":\"%s%sTAIL\"") "]", filler, cuts[i].tail);
		CHECK_EQ(selectText(json, true), UPDATE_OK);
		CHECK_EQ(strlen(release.notes), cuts[i].kept);
		CHECK(release.notesTruncated);
	}
}

static void testMalformed()
{
	printf("malformed responses\n");
	const char* bad[] = {
		"", " ", "{}", "null", "\"[]\"", "[1]", "[\"x\"]", "[[]]", "[null]",
		"\xEF\xBB\xBF[]",
		"[" REL("v1.0.0", "") "] x", "[] []", "[,]", "[{},]", "[{}", "[{} {}]", "[",
		"[{\"tag_name\":1,\"draft\":false,\"prerelease\":false}]",
		"[{\"tag_name\":null,\"draft\":false,\"prerelease\":false}]",
		"[{\"tag_name\":\"v1.0.0\",\"draft\":\"false\",\"prerelease\":false}]",
		"[{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":0}]",
		"[" REL("v1.0.0", ",\"assets\":{}") "]",
		"[" REL("v1.0.0", ",\"assets\":null") "]",
		"[" REL("v1.0.0", ",\"assets\":[1]") "]",
		"[" REL("v1.0.0", ",\"assets\":[{\"name\":5}]") "]",
		"[" REL("v1.0.0", ",\"assets\":[{\"browser_download_url\":[]}]") "]",
		"[" REL("v1.0.0", ",\"assets\":[{\"digest\":5}]") "]",
		"[" REL("v1.0.0", ",\"body\":5") "]",
		"[" REL("v1.0.0", ",\"html_url\":null") "]",
		"[{\"a\":1,}]", "[{\"a\" 1}]", "[{\"a\"::1}]", "[{a:1}]", "[{'a':1}]", "[{\"a\":tru}]", "[{\"a\":nul}]",
		"[{\"a\":01}]", "[{\"a\":-}]", "[{\"a\":1.}]", "[{\"a\":1e}]", "[{\"a\":.5}]", "[{\"a\":+1}]", "[{\"a\":NaN}]",
		"[{\"a\":\"\\x\"}]", "[{\"a\":\"\\u12\"}]", "[{\"a\":\"\\u12G4\"}]", "[{\"a\":\"\\ud800\"}]",
		"[{\"a\":\"\\udc00\"}]", "[{\"a\":\"\\ud800\\u0041\"}]", "[{\"a\":\"\\ud800x\"}]", "[{\"a\":\"\\u0000\"}]",
		"[{\"a\":\"x\x01y\"}]", "[{\"a\":\"x\ny\"}]", "[{\"a\":\"abc", "[{\"a\":\"abc\\", "[{\"a\":\"\\u",
		"[{\"a\":1}{\"b\":2}]", "[{\"a\":1},{\"b\":2},]",
	};
	int wrong = 0;
	for (int i = 0; i < COUNT(bad); i++)
	{
		int r = selectText(bad[i], true);
		if (r != UPDATE_BAD_RESPONSE)
		{
			printf("  case %d gave %s\n", i, UpdateResultName(r));
			wrong++;
		}
	}
	CHECK_EQ(wrong, 0);
	// A raw NUL inside the data, which strlen-based cases cannot express.
	CHECK_EQ(selectGuarded("[{\"a\":\"x\0y\"}]", 13, true), UPDATE_BAD_RESPONSE);
	CHECK_EQ(UpdateSelectRelease(NULL, 0, true, &release), UPDATE_BAD_RESPONSE);
	CHECK_EQ(UpdateSelectRelease("[]", 2, true, NULL), UPDATE_BAD_ARGUMENT);

	// Nesting: the release array and object are levels 1 and 2, so 30 more arrays reach the cap of 32.
	static char json[300000];
	for (int extra = 30; extra <= 31; extra++)
	{
		char* p = json;
		p += sprintf(p, "[{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":false,\"x\":");
		for (int i = 0; i < extra; i++) *p++ = '[';
		for (int i = 0; i < extra; i++) *p++ = ']';
		strcpy(p, "}]");
		CHECK_EQ(selectText(json, true), extra == 30 ? UPDATE_OK : UPDATE_BAD_RESPONSE);
	}
	// Far deeper input fails at the cap instead of exhausting the stack.
	memset(json, '[', 200000);
	json[200000] = 0;
	CHECK_EQ(selectText(json, true), UPDATE_BAD_RESPONSE);
	char* p = json;
	p += sprintf(p, "[{\"x\":");
	for (int i = 0; i < 50000; i++) { *p++ = '{'; *p++ = '"'; *p++ = 'k'; *p++ = '"'; *p++ = ':'; }
	*p = 0;
	CHECK_EQ(selectText(json, true), UPDATE_BAD_RESPONSE);
}


/*=========================== SHA-256 =========================*/

static void testSha256Sums()
{
	printf("SHA256SUMS\n");
	const char* sums =
		"1111111111111111111111111111111111111111111111111111111111111111  PlugY4Kolbot_v14.4.0.zip.bak\n"
		"9e3779b19e3817e89e38b61f9e3954569e39f28d9e3a90c49e3b2efb9e3bcd32  PlugY4Kolbot_v14.4.0.zip\n"
		"3c6ef3623c6f91993c702fd03c70ce073c716c3e3c720a753c72a8ac3c7346e3 *PlugY4Kolbot_v14.4.0_Installer.exe\r\n"
		"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  upper.zip\r\n"
		"2222222222222222222222222222222222222222222222222222222222222222 single-space.zip\n"
		"333333333333333333333333333333333333333333333333333333333333333  short-hash.zip\n"
		"gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg  not-hex.zip\n"
		"\n"
		"12ebaa8312ec48ba12ece6f112ed852812ee235f12eec19612ef5fcd12effe04  last-line.zip";
	DWORD len = (DWORD)strlen(sums);
	Guarded g;
	guardedInit(&g, len);
	const char* placed = guardedPlace(&g, sums, len);
	char hex[UPDATE_SHA256_HEX];
	CHECK(UpdateFindSha256Sum(placed, len, "PlugY4Kolbot_v14.4.0.zip", hex));
	CHECK_STR(hex, SHA_1);
	CHECK(UpdateFindSha256Sum(placed, len, "PlugY4Kolbot_v14.4.0_Installer.exe", hex));
	CHECK_STR(hex, SHA_2);
	CHECK(UpdateFindSha256Sum(placed, len, "upper.zip", hex));
	CHECK_STR(hex, SHA_ABC);
	CHECK(UpdateFindSha256Sum(placed, len, "last-line.zip", hex));
	CHECK_STR(hex, SHA_115);
	CHECK(!UpdateFindSha256Sum(placed, len, "PlugY4Kolbot_v14.4.0", hex));
	CHECK_STR(hex, "");
	CHECK(!UpdateFindSha256Sum(placed, len, "plugy4kolbot_v14.4.0.zip", hex));
	CHECK(!UpdateFindSha256Sum(placed, len, "single-space.zip", hex));
	CHECK(!UpdateFindSha256Sum(placed, len, "short-hash.zip", hex));
	CHECK(!UpdateFindSha256Sum(placed, len, "not-hex.zip", hex));
	CHECK(!UpdateFindSha256Sum(placed, len, "missing.zip", hex));
	CHECK(!UpdateFindSha256Sum(placed, len, "", hex));
	// The last line cut short by len is not found.
	CHECK(!UpdateFindSha256Sum(placed, len - 1, "last-line.zip", hex));
	guardedFree(&g);
}

static void testSha256File()
{
	printf("Sha256File\n");
	char path[MAX_PATH];
	char hex[UPDATE_SHA256_HEX];
	scratchPath(path, "abc.txt");
	CHECK(writeBytes(path, "abc", 3));
	CHECK(UpdateSha256File(path, hex));
	CHECK_STR(hex, SHA_ABC);
	scratchPath(path, "empty.txt");
	CHECK(writeBytes(path, "", 0));
	CHECK(UpdateSha256File(path, hex));
	CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	// Spans many read chunks.
	char* million = (char*)malloc(1000000);
	memset(million, 'a', 1000000);
	scratchPath(path, "million-a.txt");
	CHECK(writeBytes(path, million, 1000000));
	free(million);
	CHECK(UpdateSha256File(path, hex));
	CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	scratchPath(path, "does-not-exist.txt");
	CHECK(!UpdateSha256File(path, hex));
	CHECK_STR(hex, "");
}


/*========================= Arguments =========================*/

static void testArguments()
{
	printf("argument checks (no network)\n");
	char path[MAX_PATH];
	scratchPath(path, "never-written.bin");
	DeleteFileA(path);
	UpdateHttpInfo info;
	CHECK_EQ(UpdateDownloadToFile("http://github.com/x.zip", path, 1000, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFile("ftp://github.com/x.zip", path, 1000, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFile("github.com/x.zip", path, 1000, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFile("https://github.com/x.zip", path, 0, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFile("https://github.com/x.zip", NULL, 1000, &info), UPDATE_BAD_ARGUMENT);
	CHECK(!fileExists(path));
	CHECK_EQ(countScratchFiles("never-written.bin*"), 0);
	CHECK_EQ(UpdateHttpGet("http://api.github.com/", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(info.httpStatus, 0);
	CHECK_EQ(UpdateHttpGet("https://", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://user:secret@api.github.com/", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://:pw@api.github.com/p", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://api.github.com@evil.example/p", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://api.github.com/a b", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://api.github.com/a\r\nX-Injected: 1", NULL, 1000, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFile("https://:pw@github.com/x.zip", path, 1000, &info), UPDATE_BAD_ARGUMENT);
	CHECK(!fileExists(path));
	CHECK_EQ(countScratchFiles("never-written.bin*"), 0);
	CHECK_EQ(UpdateHttpGet("https://api.github.com/", NULL, 0, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateHttpGet("https://api.github.com/", NULL, 1000, NULL, NULL, &info), UPDATE_BAD_ARGUMENT);

	char* json;
	DWORD len;
	const char* badRepos[] = { "", "theBGuy", "a/b/c", "../x", "a/..", "./b", "a b/c", "a/b?x=1", "a/b#c", "/b", "a/", "a\\b" };
	for (int i = 0; i < COUNT(badRepos); i++)
	{
		int r = UpdateFetchReleases(badRepos[i], &json, &len, &info);
		check(r == UPDATE_BAD_ARGUMENT && !json, badRepos[i], __LINE__);
	}
	CHECK_EQ(UpdateFetchReleases(NULL, &json, &len, &info), UPDATE_BAD_ARGUMENT);
	// Unknown flag bits are refused before any request.
	CHECK_EQ(UpdateHttpGetEx("https://api.github.com/", NULL, 1000, 0x2, discardSink, NULL, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateFetchReleasesEx("ChaosMarc/PlugY", 0x80000000, &json, &len, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateFetchLatestEx("ChaosMarc/PlugY", true, 0x4, &release, &info), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateDownloadToFileEx("https://github.com/x.zip", path, 1000, 0x10, &info), UPDATE_BAD_ARGUMENT);
	CHECK(!fileExists(path));
	// Appended values keep the existing ones fixed.
	CHECK(UPDATE_NO_MEMORY == 8 && UPDATE_CACHE_UNAVAILABLE == 9 && UPDATE_BUSY == 10);
	CHECK_STR(UpdateResultName(UPDATE_CACHE_UNAVAILABLE), "cache unavailable");
	CHECK_STR(UpdateResultName(UPDATE_BUSY), "busy, another game is checking");
	CHECK_STR(UpdateResultName(UPDATE_BAD_RESPONSE), "bad response");
	CHECK_STR(UpdateResultName(1234), "unknown");
}


/*============================ Cache ==========================*/

static void testStageTimeouts()
{
	printf("stage timeouts\n");
	struct { DWORD left, stage, response; } table[] = {
		{ 0, 1, 1 }, { 1, 1, 1 }, { 2, 1, 2 }, { 3, 1, 3 }, { 3000, 1000, 3000 }, { 9999, 3333, 9999 },
		{ 10000, 3333, 10000 }, { 14999, 4999, 10000 }, { 15000, 5000, 10000 }, { 180000, 5000, 10000 },
		{ 0xFFFFFFFF, 5000, 10000 },
	};
	UpdateStageTimeouts t;
	for (int i = 0; i < COUNT(table); i++)
	{
		UpdateHttpStageTimeouts(table[i].left, &t);
		bool ok = t.resolveMs == table[i].stage && t.connectMs == table[i].stage && t.sendMs == table[i].stage
			&& t.responseMs == table[i].response;
		if (!ok)
			printf("  left %u: %u/%u/%u/%u\n", table[i].left, t.resolveMs, t.connectMs, t.sendMs, t.responseMs);
		CHECK(ok);
	}
	// The send stages together, and each single wait, never outlast what is left (beyond the 1 ms floor).
	int wrong = 0;
	for (DWORD left = 3; left < 200000; left += 7)
	{
		UpdateHttpStageTimeouts(left, &t);
		if (t.resolveMs + t.connectMs + t.sendMs > left || t.responseMs > left || t.resolveMs > 5000
			|| t.responseMs > 10000 || !t.resolveMs || !t.connectMs || !t.sendMs || !t.responseMs)
			wrong++;
	}
	CHECK_EQ(wrong, 0);
}

static void testRedirects()
{
	printf("redirect resolution\n");
	const char* current = "https://github.com/o/r/releases/download/v1/a.zip";
	char longLocation[9000];
	memcpy(longLocation, "https://h/", 10);
	memset(longLocation + 10, 'a', sizeof(longLocation) - 11);
	longLocation[sizeof(longLocation) - 1] = 0;
	struct { const char* from; const char* location; int result; const char* expected; } table[] = {
		{ current, "https://release-assets.githubusercontent.com/x?sig=1&exp=2", UPDATE_OK, "https://release-assets.githubusercontent.com/x?sig=1&exp=2" },
		{ current, "HTTPS://Objects.example/x", UPDATE_OK, "HTTPS://Objects.example/x" },
		{ current, "/o/r/other", UPDATE_OK, "https://github.com/o/r/other" },
		{ current, "/p?x=1&y=2", UPDATE_OK, "https://github.com/p?x=1&y=2" },
		{ current, "https://host?x=1", UPDATE_OK, "https://host?x=1" },
		{ "https://example.com:8443/a?q", "/b", UPDATE_OK, "https://example.com:8443/b" },
		{ "https://example.com?q=1", "/b", UPDATE_OK, "https://example.com/b" },
		{ current, "http://github.com/x", UPDATE_BAD_RESPONSE, "" },
		{ current, "//evil.example/x", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://user@evil.example/", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://:pw@host/p", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://github.com@evil.example/", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://:443/x", UPDATE_BAD_RESPONSE, "" },
		{ current, longLocation, UPDATE_BAD_RESPONSE, "" },
		{ current, "/x\r\nSet-Cookie: a=1", UPDATE_BAD_RESPONSE, "" },
		{ current, "/x\ny", UPDATE_BAD_RESPONSE, "" },
		{ current, "/x y", UPDATE_BAD_RESPONSE, "" },
		{ current, "/\\evil.example", UPDATE_BAD_RESPONSE, "" },
		{ current, "https://h/\xC3\xA9", UPDATE_BAD_RESPONSE, "" },
		{ current, "", UPDATE_BAD_RESPONSE, "" },
		// Relative forms are refused on purpose (fail closed); GitHub never sends them.
		{ current, "next.zip", UPDATE_BAD_RESPONSE, "" },
		{ current, "?x=1", UPDATE_BAD_RESPONSE, "" },
		{ current, "../x", UPDATE_BAD_RESPONSE, "" },
		{ current, "./x", UPDATE_BAD_RESPONSE, "" },
		{ current, "ftp://host/x", UPDATE_BAD_RESPONSE, "" },
		{ current, "javascript:alert(1)", UPDATE_BAD_RESPONSE, "" },
		{ "http://github.com/a", "/b", UPDATE_BAD_ARGUMENT, "" },
		{ "https://u@github.com/a", "/b", UPDATE_BAD_ARGUMENT, "" },
		{ "https://github.com/a b", "/b", UPDATE_BAD_ARGUMENT, "" },
	};
	static char out[HTTP_TEST_URL_MAX];
	for (int i = 0; i < COUNT(table); i++)
	{
		strcpy(out, "stale");
		int r = UpdateResolveRedirect(table[i].from, table[i].location, out, sizeof(out));
		if (r != table[i].result || strcmp(out, table[i].expected))
			printf("  case %d: %s \"%s\"\n", i, UpdateResultName(r), out);
		CHECK(r == table[i].result && !strcmp(out, table[i].expected));
	}
	// A result that does not fit the caller's buffer is refused, not cut.
	char tiny[24];
	CHECK_EQ(UpdateResolveRedirect(current, "/o/r/releases/x", tiny, sizeof(tiny)), UPDATE_BAD_RESPONSE);
	CHECK_STR(tiny, "");
	CHECK_EQ(UpdateResolveRedirect(current, "/x", NULL, 10), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateResolveRedirect(NULL, "/x", tiny, sizeof(tiny)), UPDATE_BAD_ARGUMENT);
	CHECK_EQ(UpdateResolveRedirect(current, NULL, tiny, sizeof(tiny)), UPDATE_BAD_ARGUMENT);
}

static void cacheFill(UpdateCache* c, ULONGLONG checkedAt, int channel, bool ok, const char* tag, const char* url)
{
	memset(c, 0, sizeof(*c));
	c->checkedAt = checkedAt;
	c->channel = channel;
	c->ok = ok;
	strcpy(c->latestTag, tag);
	strcpy(c->pageUrl, url);
}

static void testCache()
{
	printf("cache\n");
	wchar_t path[MAX_PATH];
	scratchPathW(path, "update-check.txt");
	DeleteFileW(path);
	UpdateCache in, out;
	CHECK(!UpdateCacheRead(path, &out));

	cacheFill(&in, 1760000000ULL, UPDATE_CHANNEL_PRERELEASE, true, "v14.5.0-rc.1", PAGE_BASE "v14.5.0-rc.1");
	CHECK(UpdateCacheWrite(path, &in));
	CHECK(UpdateCacheRead(path, &out));
	CHECK(out.checkedAt == 1760000000ULL && out.channel == UPDATE_CHANNEL_PRERELEASE && out.ok);
	CHECK_STR(out.latestTag, "v14.5.0-rc.1");
	CHECK_STR(out.pageUrl, PAGE_BASE "v14.5.0-rc.1");
	// The second write replaces the first.
	cacheFill(&in, 1760003600ULL, UPDATE_CHANNEL_STABLE, false, "", "");
	CHECK(UpdateCacheWrite(path, &in));
	CHECK(UpdateCacheRead(path, &out));
	CHECK(out.checkedAt == 1760003600ULL && out.channel == UPDATE_CHANNEL_STABLE && !out.ok);
	CHECK_STR(out.latestTag, "");
	CHECK_STR(out.pageUrl, "");
	// A read-only cache file is still replaced; otherwise every game start would fetch again.
	CHECK(SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE));
	cacheFill(&in, 1760003600ULL, UPDATE_CHANNEL_STABLE, false, "v1.0.0", "");
	DWORD start = GetTickCount();
	CHECK(UpdateCacheWrite(path, &in));
	CHECK(GetTickCount() - start < 1000);	// not the sharing-retry loop giving up
	CHECK(UpdateCacheRead(path, &out));
	CHECK_STR(out.latestTag, "v1.0.0");
	CHECK(!(GetFileAttributesW(path) & FILE_ATTRIBUTE_READONLY));
	CHECK_EQ(countScratchFiles("update-check.txt.*.tmp"), 0);	// no temp file left behind (Windows matches "name.*" to "name" too)

	// The writer refuses what the reader would reject.
	cacheFill(&in, 1, 7, true, "", "");
	CHECK(!UpdateCacheWrite(path, &in));
	cacheFill(&in, 1, UPDATE_CHANNEL_STABLE, true, "v14.03", "");
	CHECK(!UpdateCacheWrite(path, &in));
	cacheFill(&in, 1, UPDATE_CHANNEL_STABLE, true, "v1.0.0", "http://github.com/");
	CHECK(!UpdateCacheWrite(path, &in));
	cacheFill(&in, 1, UPDATE_CHANNEL_STABLE, true, "v1.0.0", "https://github.com/\nstatus=ok");
	CHECK(!UpdateCacheWrite(path, &in));
	CHECK(UpdateCacheRead(path, &out) && out.checkedAt == 1760003600ULL);	// untouched by the refusals

	struct { const char* text; bool valid; } files[] = {
		{ "checkedAt=5\nchannel=stable\nstatus=ok\n", true },
		{ "checkedAt=5\r\nchannel=prerelease\r\nstatus=error\r\nlatestTag=v1.0.0\r\npageUrl=https://x", true },
		{ "future=1\nchannel=stable\nstatus=ok\ncheckedAt=5\n\n\n", true },
		{ "", false },
		{ "\r\n", false },
		{ "checkedAt=abc\nchannel=stable\nstatus=ok\n", false },
		{ "checkedAt=\nchannel=stable\nstatus=ok\n", false },
		{ "checkedAt=-5\nchannel=stable\nstatus=ok\n", false },
		{ "checkedAt=12345678901234567890\nchannel=stable\nstatus=ok\n", false },
		{ "channel=stable\nstatus=ok\n", false },
		{ "checkedAt=5\nstatus=ok\n", false },
		{ "checkedAt=5\nchannel=stable\n", false },
		{ "checkedAt=5\nchannel=beta\nstatus=ok\n", false },
		{ "checkedAt=5\nchannel=stable\nstatus=maybe\n", false },
		{ "checkedAt=5\nchannel=stable\nstatus=ok\nlatestTag=v14.03\n", false },
		{ "checkedAt=5\nchannel=stable\nstatus=ok\npageUrl=http://x\n", false },
		{ "checkedAt=5\nchannel=stable\nstatus=ok\nstatus=error\n", false },
		{ "checkedAt=5\nchannel=stable\nstatus=ok\ngarbage\n", false },
		{ "checkedAt=5\nchannel=Stable\nstatus=ok\n", false },
		{ "checkedAt=5 \nchannel=stable\nstatus=ok\n", false },
		{ "\xFF\xFE" "c\0h\0", false },
	};
	char* text = (char*)malloc(4096);
	for (int i = 0; i < COUNT(files); i++)
	{
		CHECK(writeTextW(path, files[i].text));
		bool read = UpdateCacheRead(path, &out);
		if (read != files[i].valid)
			printf("  cache file case %d read as %s\n", i, read ? "valid" : "garbled");
		CHECK(read == files[i].valid);
	}
	// A NUL byte, an unknown key with a long value, and an oversized file.
	FILE* f = _wfopen(path, L"wb");
	fwrite("checkedAt=5\nchannel=stable\nstatus=ok\n\0", 1, 38, f);
	fclose(f);
	CHECK(!UpdateCacheRead(path, &out));
	memset(text, 'z', 1500);
	strcpy(text + 1500, "\nchecked");
	memcpy(text, "future=", 7);
	strcat(text, "At=5\nchannel=stable\nstatus=ok\n");
	CHECK(writeTextW(path, text));
	CHECK(UpdateCacheRead(path, &out));
	memset(text, 'z', 3000);
	strcpy(text + 3000, "\nchecked");
	memcpy(text, "future=", 7);
	strcat(text, "At=5\nchannel=stable\nstatus=ok\n");
	CHECK(writeTextW(path, text));
	CHECK(!UpdateCacheRead(path, &out));
	free(text);
	DeleteFileW(path);

	printf("check policy\n");
	const ULONGLONG now = 1760000000ULL;
	UpdateCache c;
	CHECK(UpdateShouldCheck(NULL, UPDATE_CHANNEL_STABLE, now));
	struct { ULONGLONG age; bool ok; int channel; bool future; bool expected; } table[] = {
		{ 0, true, UPDATE_CHANNEL_STABLE, false, false },
		{ UPDATE_RECHECK_OK_SECONDS - 1, true, UPDATE_CHANNEL_STABLE, false, false },
		{ UPDATE_RECHECK_OK_SECONDS, true, UPDATE_CHANNEL_STABLE, false, true },
		{ 0, false, UPDATE_CHANNEL_STABLE, false, false },
		{ UPDATE_RECHECK_ERROR_SECONDS - 1, false, UPDATE_CHANNEL_STABLE, false, false },
		{ UPDATE_RECHECK_ERROR_SECONDS, false, UPDATE_CHANNEL_STABLE, false, true },
		{ 0, true, UPDATE_CHANNEL_PRERELEASE, false, true },	// channel changed
		{ 0, false, UPDATE_CHANNEL_PRERELEASE, false, true },
		{ 10, true, UPDATE_CHANNEL_STABLE, true, true },		// checkedAt in the future
	};
	for (int i = 0; i < COUNT(table); i++)
	{
		cacheFill(&c, table[i].future ? now + table[i].age : now - table[i].age, table[i].channel, table[i].ok, "", "");
		bool should = UpdateShouldCheck(&c, UPDATE_CHANNEL_STABLE, now);
		if (should != table[i].expected)
			printf("  policy case %d\n", i);
		CHECK(should == table[i].expected);
	}
}

static void testCheckForNewerOffline()
{
	printf("UpdateCheckForNewerEx from the cache (no network)\n");
	wchar_t path[MAX_PATH];
	scratchPathW(path, "check-for-newer.txt");
	UpdateCache c;
	UpdateHint hint;
	ULONGLONG now = UpdateUnixTimeNow();
	// An invalid repo makes any fetch attempt fail before it reaches the network, and shows up as fetched.
	const char* noFetch = "not a repo";

	cacheFill(&c, now, UPDATE_CHANNEL_STABLE, true, "v14.4.0", PAGE_BASE "v14.4.0");
	CHECK(UpdateCacheWrite(path, &c));
	CHECK(UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(hint.newer && !hint.fetched && hint.result == UPDATE_OK);
	CHECK_STR(hint.tag, "v14.4.0");
	CHECK_STR(hint.version, "14.4.0");
	CHECK_STR(hint.pageUrl, PAGE_BASE "v14.4.0");
	CHECK(UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.4.0-rc.3", &hint));
	CHECK(!UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.4.0", &hint));
	CHECK(!hint.newer && !hint.fetched && !hint.tag[0]);
	CHECK(!UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.5.0-rc.1", &hint));

	// A recent failed check still reports the release it kept.
	cacheFill(&c, now, UPDATE_CHANNEL_STABLE, false, "v14.4.0", "");
	CHECK(UpdateCacheWrite(path, &c));
	CHECK(UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(!hint.fetched);
	CHECK_STR(hint.pageUrl, "");

	CHECK(!UpdateCheckForNewerEx(noFetch, path, 7, "14.3.0", &hint));
	CHECK_EQ(hint.result, UPDATE_BAD_ARGUMENT);
	CHECK(!UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.03", &hint));
	CHECK_EQ(hint.result, UPDATE_BAD_ARGUMENT);
	CHECK(!UpdateCheckForNewerEx(NULL, path, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(!UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, NULL, &hint));

	// Stale: the fetch runs and fails, the entry is rewritten as an error that keeps v14.4.0.
	cacheFill(&c, now - 2 * UPDATE_RECHECK_OK_SECONDS, UPDATE_CHANNEL_STABLE, true, "v14.4.0", PAGE_BASE "v14.4.0");
	CHECK(UpdateCacheWrite(path, &c));
	CHECK(UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(hint.fetched && hint.result == UPDATE_BAD_ARGUMENT);
	CHECK_STR(hint.pageUrl, PAGE_BASE "v14.4.0");
	UpdateCache after;
	CHECK(UpdateCacheRead(path, &after));
	CHECK(!after.ok && after.checkedAt >= now);
	CHECK_STR(after.latestTag, "v14.4.0");
	// A channel switch discards the other channel's release.
	CHECK(!UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_PRERELEASE, "14.3.0", &hint));
	CHECK(hint.fetched);
	CHECK(UpdateCacheRead(path, &after));
	CHECK(after.channel == UPDATE_CHANNEL_PRERELEASE && !after.ok && !after.latestTag[0]);
	char lockName[MAX_PATH];
	scratchPath(lockName, "check-for-newer.txt.lock");
	CHECK(!fileExists(lockName));	// deleted on close

	// Another process holding the fetch lock: wait for it, then answer from the cache without fetching.
	printf("  holding the fetch lock (about a 20 s wait)\n");
	cacheFill(&c, now - 2 * UPDATE_RECHECK_OK_SECONDS, UPDATE_CHANNEL_STABLE, true, "v14.4.0", PAGE_BASE "v14.4.0");
	CHECK(UpdateCacheWrite(path, &c));
	HANDLE hLock = CreateFileA(lockName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	CHECK(hLock != INVALID_HANDLE_VALUE);
	DWORD start = GetTickCount();
	CHECK(UpdateCheckForNewerEx(noFetch, path, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	DWORD waited = GetTickCount() - start;
	CHECK(!hint.fetched);
	CHECK_EQ(hint.result, UPDATE_BUSY);
	CHECK(waited >= 19000 && waited < 30000);
	CHECK(UpdateCacheRead(path, &after) && after.ok);	// not rewritten
	CloseHandle(hLock);

	// No cache, no request.
	CHECK(!UpdateCheckForNewerEx(noFetch, NULL, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(!hint.fetched);
	CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
	CHECK(!UpdateCheckForNewerEx(noFetch, L"relative-cache.txt", UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
	CHECK(!hint.fetched);
	CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
	DeleteFileW(path);
}


/*======================= Reparse points ======================*/

// Deletes a test tree without ever following a link: a junction or symlink is removed itself.
static void removeTree(const char* path)
{
	DWORD attributes = GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return;
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
			RemoveDirectoryA(path);
		else
			DeleteFileA(path);
		return;
	}
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
		DeleteFileA(path);
		return;
	}
	char pattern[MAX_PATH];
	_snprintf(pattern, MAX_PATH, "%s\\*", path);
	pattern[MAX_PATH - 1] = 0;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
				continue;
			char child[MAX_PATH];
			_snprintf(child, MAX_PATH, "%s\\%s", path, fd.cFileName);
			child[MAX_PATH - 1] = 0;
			removeTree(child);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(path);
}

static bool isReparsePoint(const char* path)
{
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static bool makeJunction(const char* link, const char* target)
{
	char command[MAX_PATH * 3];
	_snprintf(command, sizeof(command), "mklink /J \"%s\" \"%s\" >nul", link, target);
	command[sizeof(command) - 1] = 0;
	return system(command) == 0 && isReparsePoint(link);
}

static int countEntries(const char* dir)
{
	char pattern[MAX_PATH];
	_snprintf(pattern, MAX_PATH, "%s\\*", dir);
	pattern[MAX_PATH - 1] = 0;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return -1;
	int n = 0;
	do
		if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
			n++;
	while (FindNextFileA(h, &fd));
	FindClose(h);
	return n;
}

static void widen(wchar_t* out, const char* path)
{
	MultiByteToWideChar(CP_ACP, 0, path, -1, out, MAX_PATH);
}

static bool setAge(const char* path, ULONGLONG seconds)
{
	HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER t;
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	t.QuadPart -= seconds * 10000000ULL;
	ft.dwLowDateTime = t.LowPart;
	ft.dwHighDateTime = t.HighPart;
	bool ok = SetFileTime(h, NULL, NULL, &ft) != FALSE;
	CloseHandle(h);
	return ok;
}

static void testReparsePoints()
{
	printf("reparse points and unusable caches (no network)\n");
	char root[MAX_PATH], target[MAX_PATH], path[MAX_PATH];
	wchar_t wide[MAX_PATH];
	UpdateCache c;
	UpdateHint hint;
	const char* noFetch = "not a repo";
	scratchPath(root, "reparse");
	removeTree(root);
	CHECK(CreateDirectoryA(root, NULL));
	_snprintf(target, MAX_PATH, "%s\\target", root);
	CHECK(CreateDirectoryA(target, NULL));
	cacheFill(&c, UpdateUnixTimeNow(), UPDATE_CHANNEL_STABLE, true, "v14.4.0", "");

	// The pin the module relies on: a directory open for listing without delete sharing
	// cannot be renamed, and neither can its parent.
	char pinParent[MAX_PATH], pinChild[MAX_PATH], moved[MAX_PATH];
	_snprintf(pinParent, MAX_PATH, "%s\\pin", root);
	_snprintf(pinChild, MAX_PATH, "%s\\pin\\folder", root);
	CreateDirectoryA(pinParent, NULL);
	CreateDirectoryA(pinChild, NULL);
	HANDLE hPin = CreateFileA(pinChild, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	CHECK(hPin != INVALID_HANDLE_VALUE);
	_snprintf(moved, MAX_PATH, "%s\\pin\\moved", root);
	CHECK(!MoveFileA(pinChild, moved));
	_snprintf(moved, MAX_PATH, "%s\\pin-moved", root);
	CHECK(!MoveFileA(pinParent, moved));
	CloseHandle(hPin);
	CHECK(MoveFileA(pinParent, moved));

	// A junction as the cache folder.
	char junction[MAX_PATH];
	_snprintf(junction, MAX_PATH, "%s\\junction-folder", root);
	bool haveJunction = makeJunction(junction, target);
	printf("  junctions via mklink /J: %s\n", haveJunction ? "created" : "NOT CREATED");
	CHECK(haveJunction);
	if (haveJunction)
	{
		_snprintf(path, MAX_PATH, "%s\\update-check-stable.txt", junction);
		widen(wide, path);
		CHECK(!UpdateCacheWrite(wide, &c));
		UpdateCache read;
		CHECK(!UpdateCacheRead(wide, &read));
		CHECK(!UpdateCheckForNewerEx(noFetch, wide, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK(!hint.fetched);
		CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
		CHECK_EQ(countEntries(target), 0);	// nothing created through the junction
	}

	// A junction as the cache file, then as the lock file, in a real folder.
	char folder[MAX_PATH];
	_snprintf(folder, MAX_PATH, "%s\\folder", root);
	CHECK(CreateDirectoryA(folder, NULL));
	_snprintf(path, MAX_PATH, "%s\\junction-file.txt", folder);
	if (makeJunction(path, target))
	{
		widen(wide, path);
		CHECK(!UpdateCacheWrite(wide, &c));
		CHECK(!UpdateCheckForNewerEx(noFetch, wide, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK(!hint.fetched);
		CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
		CHECK(isReparsePoint(path));	// refused, not replaced
	}
	char lock[MAX_PATH];
	_snprintf(path, MAX_PATH, "%s\\stale.txt", folder);
	_snprintf(lock, MAX_PATH, "%s\\stale.txt.lock", folder);
	widen(wide, path);
	cacheFill(&c, UpdateUnixTimeNow() - 2 * UPDATE_RECHECK_OK_SECONDS, UPDATE_CHANNEL_STABLE, true, "v14.4.0", "");
	CHECK(UpdateCacheWrite(wide, &c));
	if (makeJunction(lock, target))
	{
		DWORD start = GetTickCount();
		CHECK(!UpdateCheckForNewerEx(noFetch, wide, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK(!hint.fetched);
		CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
		CHECK(GetTickCount() - start < 5000);	// refused at once, not waited on as a busy lock
	}
	CHECK_EQ(countEntries(target), 0);

	// A file symlink needs Developer Mode or the symlink privilege; report which case ran.
	char victim[MAX_PATH], link[MAX_PATH];
	_snprintf(victim, MAX_PATH, "%s\\victim.txt", root);
	_snprintf(link, MAX_PATH, "%s\\symlink.txt", folder);
	CHECK(writeBytes(victim, "victim", 6));
	bool haveSymlink = CreateSymbolicLinkA(link, victim, 0x2 /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */) != FALSE;
	printf("  file symlink via CreateSymbolicLink: %s\n", haveSymlink ? "created" : "not permitted here, skipped");
	if (haveSymlink)
	{
		widen(wide, link);
		UpdateCache read;
		CHECK(!UpdateCacheRead(wide, &read));
		cacheFill(&c, UpdateUnixTimeNow(), UPDATE_CHANNEL_STABLE, true, "v14.4.0", "");
		// The read-only clearing never reaches through a link to its target.
		CHECK(SetFileAttributesA(victim, FILE_ATTRIBUTE_READONLY));
		CHECK(!UpdateCacheWrite(wide, &c));
		CHECK(GetFileAttributesA(victim) & FILE_ATTRIBUTE_READONLY);
		CHECK(SetFileAttributesA(victim, FILE_ATTRIBUTE_NORMAL));
		CHECK(!UpdateCheckForNewerEx(noFetch, wide, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
		char hex[UPDATE_SHA256_HEX];
		CHECK(!UpdateSha256File(link, hex));
		UpdateHttpInfo info;
		CHECK_EQ(UpdateDownloadToFile("https://github.com/x.zip", link, 1000, &info), UPDATE_FILE_ERROR);
		FILE* f = fopen(victim, "rb");
		char buf[16] = "";
		size_t got = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
		if (f)
			fclose(f);
		CHECK(got == 6 && !memcmp(buf, "victim", 6));
	}
	// A download onto a junction is refused before any request.
	UpdateHttpInfo info;
	_snprintf(path, MAX_PATH, "%s\\junction-file.txt", folder);
	if (isReparsePoint(path))
	{
		CHECK_EQ(UpdateDownloadToFile("https://github.com/x.zip", path, 1000, &info), UPDATE_FILE_ERROR);
		CHECK_EQ(info.httpStatus, 0);
	}

	// A folder this user may not add files to: the lock cannot be created, so nothing is fetched.
	char locked[MAX_PATH], command[MAX_PATH * 2];
	_snprintf(locked, MAX_PATH, "%s\\no-write", root);
	CHECK(CreateDirectoryA(locked, NULL));
	_snprintf(command, sizeof(command), "icacls \"%s\" /deny *S-1-1-0:(WD,AD) >nul", locked);
	bool denied = system(command) == 0;
	printf("  write-denied folder via icacls: %s\n", denied ? "created" : "NOT CREATED");
	CHECK(denied);
	if (denied)
	{
		_snprintf(path, MAX_PATH, "%s\\c.txt", locked);
		widen(wide, path);
		DWORD start = GetTickCount();
		CHECK(!UpdateCheckForNewerEx(noFetch, wide, UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK(!hint.fetched);
		CHECK_EQ(hint.result, UPDATE_CACHE_UNAVAILABLE);
		CHECK(GetTickCount() - start < 5000);
		_snprintf(command, sizeof(command), "icacls \"%s\" /remove:d *S-1-1-0 >nul", locked);
		CHECK(system(command) == 0);
	}

	// %LOCALAPPDATA% that cannot hold a safe cache: no path, and UpdateCheckForNewer never fetches.
	wchar_t saved[MAX_PATH];
	DWORD savedLen = GetEnvironmentVariableW(L"LOCALAPPDATA", saved, MAX_PATH);
	char base[MAX_PATH], plugyDir[MAX_PATH];
	wchar_t channelPath[MAX_PATH];
	_snprintf(base, MAX_PATH, "%s\\appdata-file", root);
	CHECK(CreateDirectoryA(base, NULL));
	_snprintf(plugyDir, MAX_PATH, "%s\\PlugY4Kolbot", base);
	CHECK(writeBytes(plugyDir, "", 0));	// a file where the folder should be
	_snprintf(locked, MAX_PATH, "%s\\appdata-junction", root);
	CHECK(CreateDirectoryA(locked, NULL));
	_snprintf(plugyDir, MAX_PATH, "%s\\PlugY4Kolbot", locked);
	CHECK(makeJunction(plugyDir, target));
	wchar_t longValue[400];
	for (int i = 0; i < 399; i++)
		longValue[i] = L'a';
	longValue[0] = L'C';
	longValue[1] = L':';
	longValue[2] = L'\\';
	longValue[399] = 0;
	wchar_t wideBase[MAX_PATH], wideJunction[MAX_PATH];
	widen(wideBase, base);
	widen(wideJunction, locked);
	const wchar_t* values[] = { NULL, L"relative\\appdata", longValue, wideBase, wideJunction };
	for (int i = 0; i < COUNT(values); i++)
	{
		SetEnvironmentVariableW(L"LOCALAPPDATA", values[i]);
		bool gotPath = UpdateCacheChannelPath(UPDATE_CHANNEL_STABLE, channelPath, MAX_PATH);
		check(!gotPath && !channelPath[0], "no cache path for a bad LOCALAPPDATA", __LINE__);
		CHECK(!UpdateCacheDefaultPath(channelPath, MAX_PATH));
		CHECK(!UpdateCheckForNewer(UPDATE_CHANNEL_PRERELEASE, "14.3.0", &hint));
		if (hint.fetched || hint.result != UPDATE_CACHE_UNAVAILABLE)
			printf("  LOCALAPPDATA case %d: fetched %d, %s\n", i, hint.fetched, UpdateResultName(hint.result));
		CHECK(!hint.fetched && hint.result == UPDATE_CACHE_UNAVAILABLE);
	}
	CHECK_EQ(countEntries(target), 0);

	// One cache per channel: each channel answers from its own file, neither touches the other.
	printf("per-channel caches\n");
	_snprintf(base, MAX_PATH, "%s\\appdata", root);
	CHECK(CreateDirectoryA(base, NULL));
	widen(wideBase, base);
	SetEnvironmentVariableW(L"LOCALAPPDATA", wideBase);
	wchar_t stablePath[MAX_PATH], prePath[MAX_PATH], defaultPath[MAX_PATH];
	CHECK(UpdateCacheChannelPath(UPDATE_CHANNEL_STABLE, stablePath, MAX_PATH));
	CHECK(UpdateCacheChannelPath(UPDATE_CHANNEL_PRERELEASE, prePath, MAX_PATH));
	CHECK(UpdateCacheDefaultPath(defaultPath, MAX_PATH) && !wcscmp(defaultPath, stablePath));
	CHECK(wcsstr(stablePath, L"\\PlugY4Kolbot\\update-check-stable.txt") != NULL);
	CHECK(wcsstr(prePath, L"\\PlugY4Kolbot\\update-check-prerelease.txt") != NULL);
	CHECK(!UpdateCacheChannelPath(7, channelPath, MAX_PATH));
	ULONGLONG now = UpdateUnixTimeNow();
	cacheFill(&c, now, UPDATE_CHANNEL_STABLE, true, "v14.4.0", PAGE_BASE "v14.4.0");
	CHECK(UpdateCacheWrite(stablePath, &c));
	cacheFill(&c, now, UPDATE_CHANNEL_PRERELEASE, true, "v14.5.0-rc.1", PAGE_BASE "v14.5.0-rc.1");
	CHECK(UpdateCacheWrite(prePath, &c));
	for (int round = 0; round < 2; round++)
	{
		CHECK(UpdateCheckForNewer(UPDATE_CHANNEL_STABLE, "14.3.0", &hint));
		CHECK(!hint.fetched && hint.result == UPDATE_OK);
		CHECK_STR(hint.tag, "v14.4.0");
		CHECK(UpdateCheckForNewer(UPDATE_CHANNEL_PRERELEASE, "14.3.0", &hint));
		CHECK(!hint.fetched && hint.result == UPDATE_OK);
		CHECK_STR(hint.tag, "v14.5.0-rc.1");
	}
	UpdateCache read;
	CHECK(UpdateCacheRead(stablePath, &read) && read.channel == UPDATE_CHANNEL_STABLE && read.checkedAt == now);
	CHECK(UpdateCacheRead(prePath, &read) && read.channel == UPDATE_CHANNEL_PRERELEASE && read.checkedAt == now);
	SetEnvironmentVariableW(L"LOCALAPPDATA", savedLen && savedLen < MAX_PATH ? saved : NULL);

	// The temp sweep: only this module's own stale names for the file being written go.
	printf("stale temp sweep\n");
	char sweep[MAX_PATH];
	_snprintf(sweep, MAX_PATH, "%s\\sweep", root);
	CHECK(CreateDirectoryA(sweep, NULL));
	struct { const char* name; bool dir; bool old; bool expectGone; } entries[] = {
		{ "c.txt.111.222.tmp", false, true, true },
		{ "c.txt.111.333.TMP", false, true, true },
		{ "c.txt.111.223.tmp", false, false, false },	// recent: a writer may still own it
		{ "c.txt.abc.222.tmp", false, true, false },
		{ "c.txt.111.tmp", false, true, false },
		{ "c.txt.12345678901.1.tmp", false, true, false },
		{ "c.txt.111.222.tmp.bak", false, true, false },
		{ "other.txt.111.222.tmp", false, true, false },
		{ "c.txt.333.444.tmp", true, true, false },
	};
	for (int i = 0; i < COUNT(entries); i++)
	{
		_snprintf(path, MAX_PATH, "%s\\%s", sweep, entries[i].name);
		CHECK(entries[i].dir ? CreateDirectoryA(path, NULL) != FALSE : writeBytes(path, "x", 1));
		if (entries[i].old)
			CHECK(setAge(path, 2 * 3600));
	}
	char sweepJunction[MAX_PATH];
	_snprintf(sweepJunction, MAX_PATH, "%s\\c.txt.555.666.tmp", sweep);
	CHECK(makeJunction(sweepJunction, target));
	_snprintf(path, MAX_PATH, "%s\\c.txt", sweep);
	widen(wide, path);
	cacheFill(&c, now, UPDATE_CHANNEL_STABLE, true, "", "");
	CHECK(UpdateCacheWrite(wide, &c));
	for (int i = 0; i < COUNT(entries); i++)
	{
		_snprintf(path, MAX_PATH, "%s\\%s", sweep, entries[i].name);
		bool gone = !fileExists(path);
		if (gone != entries[i].expectGone)
			printf("  sweep: %s %s\n", entries[i].name, gone ? "deleted" : "kept");
		CHECK(gone == entries[i].expectGone);
	}
	CHECK(isReparsePoint(sweepJunction));
	CHECK_EQ(countEntries(target), 0);

	removeTree(root);
	CHECK(!fileExists(root));
	CHECK(fileExists(scratchDir));
}


/*============================= Live ==========================*/

static void testLive()
{
	printf("live: GitHub API (ChaosMarc/PlugY)\n");
	char* json = NULL;
	DWORD len = 0;
	UpdateHttpInfo info;
	DWORD start = GetTickCount();
	int r = UpdateFetchReleases("ChaosMarc/PlugY", &json, &len, &info);
	printf("  fetch: %s, HTTP %u, error %u, %u bytes, %u ms\n", UpdateResultName(r), info.httpStatus, info.winError, len, GetTickCount() - start);
	CHECK_EQ(r, UPDATE_OK);
	CHECK_EQ(info.httpStatus, 200);
	if (r != UPDATE_OK)
		return;
	CHECK_EQ(UpdateSelectRelease(json, len, false, &release), UPDATE_NO_RELEASE);
	CHECK_EQ(UpdateSelectRelease(json, len, true, &release), UPDATE_NO_RELEASE);
	CHECK_EQ(UpdateFindReleaseByTag(json, len, "v14.03", &release), UPDATE_OK);
	UpdateFree(json);
	const UpdateAsset* patcher = NULL;
	for (DWORD i = 0; i < release.nbAssets; i++)
		if (!strcmp(release.assets[i].name, "PlugY_The_Survival_Kit_v14.03_D2FilePatcher.zip"))
			patcher = &release.assets[i];
	printf("  v14.03: %u assets, page %s\n", release.nbAssets, release.pageUrl);
	CHECK(patcher != NULL);
	if (!patcher)
		return;
	printf("  asset url %s\n  API digest: %s\n", patcher->url, patcher->sha256[0] ? patcher->sha256 : "(none reported)");

	printf("live: asset download\n");
	char path[MAX_PATH];
	scratchPath(path, "PlugY_The_Survival_Kit_v14.03_D2FilePatcher.zip");
	DeleteFileA(path);
	start = GetTickCount();
	r = UpdateDownloadToFile(patcher->url, path, 1024 * 1024, &info);
	printf("  download: %s, HTTP %u, error %u, %u ms\n", UpdateResultName(r), info.httpStatus, info.winError, GetTickCount() - start);
	CHECK_EQ(r, UPDATE_OK);
	WIN32_FILE_ATTRIBUTE_DATA fa;
	CHECK(GetFileAttributesExA(path, GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow == 13579);
	char hex[UPDATE_SHA256_HEX];
	CHECK(UpdateSha256File(path, hex));
	printf("  Sha256File: %s\n", hex);
	if (patcher->sha256[0])
		CHECK_STR(hex, patcher->sha256);
	else
		printf("  the API reports no digest for this asset, so there is nothing to compare against\n");
	CHECK_EQ(countScratchFiles("PlugY_The_Survival_Kit_v14.03_D2FilePatcher.zip.*.part"), 0);

	// Over the cap: refused, and nothing is left on disk.
	scratchPath(path, "too-large.zip");
	DeleteFileA(path);
	r = UpdateDownloadToFile(patcher->url, path, 1000, &info);
	printf("  capped download: %s, HTTP %u\n", UpdateResultName(r), info.httpStatus);
	CHECK_EQ(r, UPDATE_TOO_LARGE);
	CHECK(!fileExists(path));
	CHECK_EQ(countScratchFiles("too-large.zip*"), 0);

	printf("live: certificate revocation checking (UPDATE_HTTP_CHECK_REVOCATION)\n");
	r = UpdateFetchReleasesEx("ChaosMarc/PlugY", UPDATE_HTTP_CHECK_REVOCATION, &json, &len, &info);
	printf("  API fetch: %s, HTTP %u, error %u\n", UpdateResultName(r), info.httpStatus, info.winError);
	CHECK_EQ(r, UPDATE_OK);
	if (r == UPDATE_OK)
		UpdateFree(json);
	scratchPath(path, "revocation-checked.zip");
	DeleteFileA(path);
	r = UpdateDownloadToFileEx(patcher->url, path, 1024 * 1024, UPDATE_HTTP_CHECK_REVOCATION, &info);
	printf("  download through the redirect: %s, HTTP %u, error %u\n", UpdateResultName(r), info.httpStatus, info.winError);
	CHECK_EQ(r, UPDATE_OK);
	char revocationHex[UPDATE_SHA256_HEX];
	CHECK(UpdateSha256File(path, revocationHex));
	CHECK_STR(revocationHex, hex);
	DeleteFileA(path);

	printf("live: errors\n");
	r = UpdateFetchReleases("ChaosMarc/this-repository-does-not-exist-4c1d", &json, &len, &info);
	printf("  missing repo: %s, HTTP %u\n", UpdateResultName(r), info.httpStatus);
	CHECK_EQ(r, UPDATE_HTTP_ERROR);
	CHECK_EQ(info.httpStatus, 404);
	r = UpdateDownloadToFile("https://github.com/ChaosMarc/PlugY/releases/download/v14.03/no-such-asset.zip", path, 1000, &info);
	printf("  missing asset: %s, HTTP %u\n", UpdateResultName(r), info.httpStatus);
	CHECK_EQ(r, UPDATE_HTTP_ERROR);
	CHECK(!fileExists(path));

	printf("live: UpdateCheckForNewerEx (ChaosMarc/PlugY, scratch cache)\n");
	wchar_t cachePath[MAX_PATH];
	scratchPathW(cachePath, "live-check.txt");
	DeleteFileW(cachePath);
	UpdateHint hint;
	CHECK(!UpdateCheckForNewerEx("ChaosMarc/PlugY", cachePath, UPDATE_CHANNEL_STABLE, PLUGY_VERSION, &hint));
	printf("  first call: fetched %d, %s, HTTP %u\n", hint.fetched, UpdateResultName(hint.result), hint.info.httpStatus);
	CHECK(hint.fetched && hint.result == UPDATE_NO_RELEASE);
	UpdateCache c;
	CHECK(UpdateCacheRead(cachePath, &c) && c.ok && !c.latestTag[0]);
	CHECK(!UpdateCheckForNewerEx("ChaosMarc/PlugY", cachePath, UPDATE_CHANNEL_STABLE, PLUGY_VERSION, &hint));
	printf("  second call: fetched %d\n", hint.fetched);
	CHECK(!hint.fetched);
	DeleteFileW(cachePath);

	printf("live: this project's repo (%s), informational\n", PLUGY_UPDATE_REPO);
	r = UpdateFetchLatest(PLUGY_UPDATE_REPO, true, &release, &info);
	printf("  %s, HTTP %u%s%s\n", UpdateResultName(r), info.httpStatus, r == UPDATE_OK ? ", newest " : "", r == UPDATE_OK ? release.tag : "");
}


/*============================= Main ==========================*/

int main(int argc, char** argv)
{
	bool live = false;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--live"))
			live = true;
		else if (!strcmp(argv[i], "--fixtures") && i + 1 < argc)
			strncpy(fixtureDir, argv[++i], MAX_PATH - 1);
		else if (!strcmp(argv[i], "--scratch") && i + 1 < argc)
			strncpy(scratchDir, argv[++i], MAX_PATH - 1);
		else
		{
			printf("usage: UpdateCheckTests [--fixtures DIR] [--scratch DIR] [--live]\n");
			return 2;
		}
	}
	if (!scratchDir[0])
	{
		GetModuleFileNameA(NULL, scratchDir, MAX_PATH);
		char* slash = strrchr(scratchDir, '\\');
		strcpy(slash ? slash + 1 : scratchDir, "scratch");
	}
	CreateDirectoryA(scratchDir, NULL);
	printf("UpdateCheckTests (PlugY4Kolbot %s): fixtures %s, scratch %s%s\n", PLUGY_VERSION, fixtureDir, scratchDir, live ? ", live" : "");

	testSemver();
	testReleasesFixture();
	testEscapesFixture();
	testManyAssetsFixture();
	testSelectEdges();
	testMalformed();
	testSha256Sums();
	testSha256File();
	testArguments();
	testStageTimeouts();
	testRedirects();
	testCache();
	testCheckForNewerOffline();
	testReparsePoints();
	if (live)
		testLive();

	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

///////////////////////// END OF FILE ///////////////////////

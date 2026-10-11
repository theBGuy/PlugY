/*=================================================================
	Unit tests for src/plugy/movedItemsPage.h and the labels
	game-files/PlugY/LocalizedStrings.ini ships (its path is the first
	argument). Built and run by scripts/build.ps1 -Test; exits 1 on any
	failure.
=================================================================*/

#include "../src/plugy/movedItemsPage.h"
#include <stdio.h>
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

// Names the ini key, so a translator sees which language broke the rule.
static void checkKey(bool ok, LPCWSTR key, const char* rule, int line)
{
	if (ok)
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %ls=: %s\n", line, key, rule);
}

#define CHECK(cond) check((cond) ? true : false, #cond, __LINE__)

static void testPageName()
{
	CHECK(isMovedItemsPageName("Moved items"));
	CHECK(!isMovedItemsPageName(NULL));
	CHECK(!isMovedItemsPageName(""));
	CHECK(!isMovedItemsPageName("Moved items "));
	CHECK(!isMovedItemsPageName("moved items"));
	CHECK(!isMovedItemsPageName("Verschobene Items"));
	CHECK(!wcscmp(MOVED_ITEMS_PAGE_NAME_W, L"Moved items"));
}

static void testLabel()
{
	LPCWSTR german = L"Verschobene Items";
	CHECK(movedItemsPageLabel(german) == german);
	CHECK(!wcscmp(movedItemsPageLabel(L"Moved items"), L"Moved items"));
	// what the ini loader returns for an entry the file lacks, or never loaded
	CHECK(!wcscmp(movedItemsPageLabel(L"STR_MOVED_ITEMS_PAGE"), L"Moved items"));
	CHECK(!wcscmp(movedItemsPageLabel(L""), L"Moved items"));
	CHECK(!wcscmp(movedItemsPageLabel(NULL), L"Moved items"));
}

static void testPrefill()
{
	LPCWSTR german = L"Verschobene Items";
	CHECK(movedItemsPagePrefill(german) == german);
	CHECK(!wcscmp(movedItemsPagePrefill(L"Moved items"), L"Moved items"));
	// a label that isn't ASCII prefills the saved name, which /rp sends back unchanged in any code page
	CHECK(!wcscmp(movedItemsPagePrefill(L"Objets d\x00E9plac\x00E9s"), L"Moved items"));
	CHECK(!wcscmp(movedItemsPagePrefill(L"\x5DF2\x79FB\x52D5\x7684\x7269\x54C1"), L"Moved items"));
	CHECK(!wcscmp(movedItemsPagePrefill(L"\x041F\x0435\x0440\x0435\x043C\x0435\x0449\x0435\x043D\x043D\x044B\x0435"), L"Moved items"));
	CHECK(!wcscmp(movedItemsPagePrefill(L""), L""));
}

static void testShownLabel()
{
	CHECK(isShownMovedItemsLabel("Verschobene Items", L"Verschobene Items"));
	CHECK(isShownMovedItemsLabel("Moved items", L"Moved items"));
	CHECK(!isShownMovedItemsLabel("Verschobene Item", L"Verschobene Items"));
	CHECK(!isShownMovedItemsLabel("Verschobene Itemsx", L"Verschobene Items"));
	CHECK(!isShownMovedItemsLabel("verschobene items", L"Verschobene Items"));
	CHECK(!isShownMovedItemsLabel("", L"Verschobene Items"));
	CHECK(!isShownMovedItemsLabel(NULL, L"Verschobene Items"));
	CHECK(!isShownMovedItemsLabel("Verschobene Items", NULL));
	// the stash shows the first 20 characters of a longer label, and /rp keeps at most 20
	CHECK(isShownMovedItemsLabel("ABCDEFGHIJKLMNOPQRST", L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
	CHECK(!isShownMovedItemsLabel("ABCDEFGHIJKLMNOPQRS", L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
	// renamePage trims the spaces the prefill carries, including one the 20-character cut leaves at the end
	CHECK(isShownMovedItemsLabel("Verschobene Items", L"  Verschobene Items "));
	CHECK(isShownMovedItemsLabel("ABCDEFGHIJKLMNOPQRS", L"ABCDEFGHIJKLMNOPQRS TUV"));
	// a label that isn't ASCII never matches: its prefill is the saved name, which renamePage's own check catches
	CHECK(!isShownMovedItemsLabel("Objets d\xE9plac\xE9s", L"Objets d\x00E9plac\x00E9s"));
	CHECK(!isShownMovedItemsLabel("\xE5\xB7\xB2\xE7\xA7\xBB", L"\x5DF2\x79FB\x52D5\x7684\x7269\x54C1"));
}

// The shipped labels: every game language has one, English matches the saved name, and each fits the stash name
// field as it is (at most 20 characters, no '#', which the stash replaces by the page number, no edge spaces).
static void testShippedLabels(const char* iniPath)
{
	FILE* f = iniPath ? fopen(iniPath, "rb") : NULL;
	CHECK(f != NULL);
	if (!f)
		return;
	static BYTE raw[0x40000];
	size_t size = fread(raw, 1, sizeof(raw), f);
	fclose(f);
	bool utf16 = size > 2 && size < sizeof(raw) && raw[0] == 0xFF && raw[1] == 0xFE;
	check(utf16, "LocalizedStrings.ini must be UTF-16LE with a byte-order mark", __LINE__);
	if (!utf16)
		return;
	static WCHAR text[sizeof(raw) / 2 + 1];
	size_t n = (size - 2) / 2;
	memcpy(text, raw + 2, n * 2);
	text[n] = 0;
	LPCWSTR section = wcsstr(text, L"\r\n[STR_MOVED_ITEMS_PAGE]\r\n");
	CHECK(section != NULL);
	if (!section)
		return;
	section += wcslen(L"\r\n[STR_MOVED_ITEMS_PAGE]");
	LPCWSTR end = wcsstr(section, L"\r\n[");
	if (!end)
		end = text + n;
	static const WCHAR* keys[] = { L"ENG", L"ESP", L"DEU", L"FRA", L"POR", L"ITA", L"JPN", L"KOR", L"SIN", L"CHI", L"POL", L"RUS" };
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
	{
		WCHAR key[8];
		_snwprintf(key, 8, L"\r\n%s=", keys[k]);
		LPCWSTR at = wcsstr(section, key);
		bool found = at && at < end;
		checkKey(found, keys[k], "missing from the section", __LINE__);
		if (!found)
			continue;
		LPCWSTR value = at + wcslen(key);
		LPCWSTR stop = value;
		while (stop < end && *stop != L'\r' && *stop != L'\n')
			stop++;
		size_t len = stop - value;
		checkKey(len >= 1 && len <= STASH_NAME_MAX_SHOWN, keys[k], "label is empty or longer than 20 characters", __LINE__);
		checkKey(!wmemchr(value, L'#', len), keys[k], "label has a '#'", __LINE__);
		checkKey(len && value[0] != L' ' && value[len - 1] != L' ', keys[k], "label starts or ends with a space", __LINE__);
		if (!wcscmp(keys[k], L"ENG"))
			checkKey(len == wcslen(MOVED_ITEMS_PAGE_NAME_W) && !wcsncmp(value, MOVED_ITEMS_PAGE_NAME_W, len), keys[k], "must be the saved name \"Moved items\"", __LINE__);
	}
}

int main(int argc, char** argv)
{
	testPageName();
	testLabel();
	testPrefill();
	testShownLabel();
	testShippedLabels(argc > 1 ? argv[1] : NULL);
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

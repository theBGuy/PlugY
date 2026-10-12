/*=================================================================
	Unit tests for src/plugy/serverMessageRules.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/serverMessageRules.h"
#include <stdio.h>

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

static void testVanillaSplit()
{
	// The game's stat point requests: stat ids 0-3, and every lower type up to PlugY's first.
	for (int type = 0; type < 0x10; type++)
		CHECK(isVanillaServerMessage(type));
	CHECK(!isVanillaServerMessage(US_UNASSIGN_STR_POINT));
	CHECK(!isVanillaServerMessage(US_RENAME));
	CHECK(!isVanillaServerMessage(US_MAXGOLD));
	CHECK(!isVanillaServerMessage(US_PY_CLIENT_GRID));
	CHECK(!isVanillaServerMessage(0xFF));
	// The wire values the clients already send.
	CHECK(US_UNASSIGN_STR_POINT == 0x10);
	CHECK(US_SAVE == 0x24);
	CHECK(US_MAXGOLD == 0x25);
	CHECK(US_RENAME == 0x28);
	CHECK(US_PAGENAME == 0x29);
	CHECK(US_SWAP3 == 0x2F);
	CHECK(US_SWAP0_TOGGLE == 0x33);
}

static void testRenameNames()
{
	CHECK(isRenameCharName("ab"));
	CHECK(isRenameCharName("Bob"));
	CHECK(isRenameCharName("Under_score"));
	CHECK(isRenameCharName("_lead"));
	CHECK(isRenameCharName("trail_"));
	CHECK(isRenameCharName("abcdefghijklmno"));		// 15
	CHECK(isRenameCharName("ABCDEFGHIJKLMNO"));
	CHECK(!isRenameCharName("abcdefghijklmnop"));		// 16
	CHECK(!isRenameCharName("a"));
	CHECK(!isRenameCharName(""));
	CHECK(!isRenameCharName(NULL));
	CHECK(!isRenameCharName("a-b"));
	CHECK(!isRenameCharName("a b"));
	CHECK(!isRenameCharName("abc1"));
	CHECK(!isRenameCharName("..\\host"));
	CHECK(!isRenameCharName("a/b"));
	CHECK(!isRenameCharName("a:b"));
	CHECK(!isRenameCharName("tab\there"));
	const char high[] = { 'a', (char)0xE9, 'b', 0 };	// an accented letter in Windows-1252
	CHECK(!isRenameCharName(high));
	// Every byte the client could send: only letters and the underscore pass.
	int accepted = 0;
	for (int c = 1; c < 256; c++)
		if (isRenameNameChar((char)c))
			accepted++;
	CHECK(accepted == 26 + 26 + 1);
	CHECK(RENAME_NAME_MIN == 2);
	CHECK(RENAME_NAME_MAX == 15);
}

static void testGameMode()
{
	CHECK(isSinglePlayerGameMode(0));
	CHECK(!isSinglePlayerGameMode(1));	// TCP/IP host
	CHECK(!isSinglePlayerGameMode(6));	// the other modes that run a server in this process
	CHECK(!isSinglePlayerGameMode(8));
	CHECK(!isSinglePlayerGameMode(2));
	CHECK(!isSinglePlayerGameMode(0xFFFFFFFF));
}

int main()
{
	testVanillaSplit();
	testRenameNames();
	testGameMode();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

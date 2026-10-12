/*=================================================================
	Unit tests for src/plugy/statBonusRules.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/statBonusRules.h"
#include <stdio.h>
#include <limits.h>

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

#define CHECK(cond) check((cond) ? true : false, #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkInt((int)(actual), (int)(expected), #actual, __LINE__)

static void testClamp()
{
	CHECK_EQ(clampStatBonus(0), 0);
	CHECK_EQ(clampStatBonus(1), 1);
	CHECK_EQ(clampStatBonus(999), 999);
	CHECK_EQ(clampStatBonus(STAT_BONUS_MAX), STAT_BONUS_MAX);
	CHECK_EQ(clampStatBonus(STAT_BONUS_MAX + 1), STAT_BONUS_MAX);
	CHECK_EQ(clampStatBonus(INT_MAX), STAT_BONUS_MAX);
	CHECK_EQ(clampStatBonus(-1), 0);
	CHECK_EQ(clampStatBonus(INT_MIN), 0);
}

static void testParse()
{
	CHECK_EQ(parseStatBonus("999"), 999);
	CHECK_EQ(parseStatBonus("0"), 0);
	CHECK_EQ(parseStatBonus(""), 0);
	CHECK_EQ(parseStatBonus("abc"), 0);
	CHECK_EQ(parseStatBonus("-50"), 0);
	CHECK_EQ(parseStatBonus("20000"), STAT_BONUS_MAX);
	CHECK_EQ(parseStatBonus("99999999999999999999"), STAT_BONUS_MAX);
	CHECK_EQ(parseStatBonus("-99999999999999999999"), 0);
	// PlugY's INI reader copies a value to the end of its line, untrimmed
	CHECK_EQ(parseStatBonus(" 300"), 300);
	CHECK_EQ(parseStatBonus("300 ; magic find"), 300);
	CHECK_EQ(parseStatBonus(NULL), 0);
}

static void testAllowList()
{
	CHECK(isStatBonusStat(79));
	CHECK(isStatBonusStat(80));
	CHECK(!isStatBonusStat(0));
	CHECK(!isStatBonusStat(78));
	CHECK(!isStatBonusStat(81));
	CHECK(!isStatBonusStat(0xFFFFFFFF));
}

int main()
{
	testClamp();
	testParse();
	testAllowList();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

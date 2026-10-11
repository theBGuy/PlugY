/*=================================================================
	Unit tests for src/plugy/releasedConnections.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/releasedConnections.h"
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

static int serverA, serverB;
static void* const NET_A = &serverA;
static void* const NET_B = &serverB;

static void testTakeOnce()
{
	ReleasedConnections list = {};
	addReleased(&list, NET_A, 3);
	CHECK(takeReleased(&list, NET_A, 3));
	CHECK(!takeReleased(&list, NET_A, 3));
	CHECK_EQ(list.count, 0);
}

static void testSameIdTwice()
{
	ReleasedConnections list = {};
	addReleased(&list, NET_A, 5);
	addReleased(&list, NET_A, 5);
	CHECK(takeReleased(&list, NET_A, 5));
	CHECK_EQ(list.count, 1);
	CHECK(takeReleased(&list, NET_A, 5));
	CHECK(!takeReleased(&list, NET_A, 5));
}

static void testOtherServerDropped()
{
	ReleasedConnections list = {};
	addReleased(&list, NET_B, 7);
	addReleased(&list, NET_A, 9);
	addReleased(&list, NET_B, 11);
	// An entry of an earlier server never matches, even for the same id, and any take drops it.
	CHECK(!takeReleased(&list, NET_A, 7));
	CHECK_EQ(list.count, 1);
	CHECK_EQ(list.entries[0].clientID, 9);
	CHECK(list.entries[0].net == NET_A);
	CHECK(!takeReleased(&list, NET_B, 11));
	CHECK_EQ(list.count, 0);
}

static void testUnrecordedKeepsOrder()
{
	ReleasedConnections list = {};
	addReleased(&list, NET_A, 1);
	addReleased(&list, NET_A, 3);
	addReleased(&list, NET_A, 5);
	CHECK(!takeReleased(&list, NET_A, 4));
	CHECK_EQ(list.count, 3);
	CHECK(takeReleased(&list, NET_A, 3));
	CHECK_EQ(list.count, 2);
	CHECK_EQ(list.entries[0].clientID, 1);
	CHECK_EQ(list.entries[1].clientID, 5);
}

static void testFullListEvictsOldest()
{
	ReleasedConnections list = {};
	for (DWORD id = 1; id <= RELEASED_CONNECTIONS_MAX + 1; id++)
		addReleased(&list, NET_A, id);
	CHECK_EQ(list.count, RELEASED_CONNECTIONS_MAX);
	CHECK_EQ(list.entries[0].clientID, 2);
	CHECK_EQ(list.entries[RELEASED_CONNECTIONS_MAX - 1].clientID, RELEASED_CONNECTIONS_MAX + 1);
	CHECK(!takeReleased(&list, NET_A, 1));
	CHECK(takeReleased(&list, NET_A, RELEASED_CONNECTIONS_MAX + 1));
	CHECK(takeReleased(&list, NET_A, 2));
	CHECK_EQ(list.count, RELEASED_CONNECTIONS_MAX - 2);
}

static void testEmpty()
{
	ReleasedConnections list = {};
	CHECK(!takeReleased(&list, NET_A, 1));
	CHECK(!takeReleased(&list, NULL, 1));
	CHECK_EQ(list.count, 0);
}

int main()
{
	testTakeOnce();
	testSameIdTwice();
	testOtherServerDropped();
	testUnrecordedKeepsOrder();
	testFullListEvictsOldest();
	testEmpty();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

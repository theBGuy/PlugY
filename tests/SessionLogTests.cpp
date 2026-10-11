/*=================================================================
	Unit tests for src/common/SessionLog.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/common/SessionLog.h"
#include <stdio.h>
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

#define CHECK(cond) check((cond) ? true : false, #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkInt((int)(actual), (int)(expected), #actual, __LINE__)
#define CHECK_STR(actual, expected) checkStr((actual), (expected), #actual, __LINE__)

static SYSTEMTIME at(WORD y, WORD mo, WORD d, WORD h, WORD mi, WORD s, WORD ms)
{
	SYSTEMTIME t = {};
	t.wYear = y;
	t.wMonth = mo;
	t.wDay = d;
	t.wHour = h;
	t.wMinute = mi;
	t.wSecond = s;
	t.wMilliseconds = ms;
	return t;
}

static void testName()
{
	printf("SessionLogName\n");
	SYSTEMTIME t = at(2026, 10, 10, 20, 44, 47, 123);
	char name[64];
	CHECK(SessionLogName(&t, 287472, name, sizeof(name)));
	CHECK_STR(name, "PlugY_2026-10-10_20-44-47_287472.log");
	t = at(2027, 1, 2, 3, 4, 5, 0);
	CHECK(SessionLogName(&t, 4294967295UL, name, sizeof(name)));
	CHECK_STR(name, "PlugY_2027-01-02_03-04-05_4294967295.log");
	CHECK(SessionLogIsOurs(name));

	char tooSmall[20] = "untouched";
	CHECK(!SessionLogName(&t, 1, tooSmall, sizeof(tooSmall)));
	CHECK_STR(tooSmall, "");
	CHECK(!SessionLogName(&t, 1, NULL, 64));
}

static void testIsOurs()
{
	printf("SessionLogIsOurs\n");
	CHECK(SessionLogIsOurs("PlugY_2026-10-10_20-44-47_287472.log"));
	CHECK(SessionLogIsOurs("plugy_2026-10-10_20-44-47_1.LOG"));
	CHECK(!SessionLogIsOurs("PlugY.log"));
	CHECK(!SessionLogIsOurs("PlugY_2026-10-10_20-44-47_287472.log2"));
	CHECK(!SessionLogIsOurs("PlugY_2026-10-10_20-44-47_287472.log.bak"));
	CHECK(!SessionLogIsOurs("PlugY_2026-10-10_20-44-47_.log"));
	CHECK(!SessionLogIsOurs("PlugY_2026-10-10_20-44-47_12345678901.log"));
	CHECK(!SessionLogIsOurs("PlugY_26-10-10_20-44-47_1.log"));
	CHECK(!SessionLogIsOurs("PlugY_2026-10-10_20-44_1.log"));
	CHECK(!SessionLogIsOurs("PlugY_2026-1a-10_20-44-47_1.log"));
	CHECK(!SessionLogIsOurs("XPlugY_2026-10-10_20-44-47_1.log"));
	CHECK(!SessionLogIsOurs("PlugY_"));
	CHECK(!SessionLogIsOurs(""));
	CHECK(!SessionLogIsOurs(NULL));
}

static void testClock()
{
	printf("SessionLogClock\n");
	char clock[SESSION_LOG_CLOCK_SIZE];
	SYSTEMTIME t = at(2026, 10, 10, 20, 44, 47, 123);
	SessionLogClock(&t, clock);
	CHECK_STR(clock, "20:44:47.123 ");
	t = at(2026, 10, 10, 1, 2, 3, 4);
	SessionLogClock(&t, clock);
	CHECK_STR(clock, "01:02:03.004 ");
}

struct Buffer
{
	char text[512];
	size_t len;
	int calls;
};

static void bufferSink(const char* text, size_t len, void* ctx)
{
	Buffer* b = (Buffer*)ctx;
	b->calls++;
	if (b->len + len >= sizeof(b->text))
		return;
	memcpy(b->text + b->len, text, len);
	b->len += len;
	b->text[b->len] = 0;
}

static void stamped(Buffer* b, const char* text, bool* atLineStart)
{
	SessionLogStamp(text, "S ", atLineStart, bufferSink, b);
}

static void testStamp()
{
	printf("SessionLogStamp\n");
	Buffer b = {};
	bool lineStart = true;
	stamped(&b, "a\nb\n", &lineStart);
	CHECK_STR(b.text, "S a\nS b\n");
	CHECK(lineStart);

	// A message that ends mid-line is continued by the next one without a second stamp.
	b = Buffer();
	lineStart = true;
	stamped(&b, "Install: ", &lineStart);
	CHECK(!lineStart);
	stamped(&b, "OK\n", &lineStart);
	CHECK_STR(b.text, "S Install: OK\n");
	CHECK(lineStart);

	// Blank lines stay blank.
	b = Buffer();
	lineStart = true;
	stamped(&b, "\n\nx\n\n", &lineStart);
	CHECK_STR(b.text, "\n\nS x\n\n");

	// A newline that closes a line left open by the previous message.
	b = Buffer();
	lineStart = false;
	stamped(&b, "\nnext", &lineStart);
	CHECK_STR(b.text, "\nS next");
	CHECK(!lineStart);

	// Tabs and other characters are line content.
	b = Buffer();
	lineStart = true;
	stamped(&b, "\t\tPlugY4Kolbot\n", &lineStart);
	CHECK_STR(b.text, "S \t\tPlugY4Kolbot\n");

	b = Buffer();
	lineStart = true;
	stamped(&b, "", &lineStart);
	CHECK_EQ(b.calls, 0);
	CHECK(lineStart);
	SessionLogStamp(NULL, "S ", &lineStart, bufferSink, &b);
	CHECK_EQ(b.calls, 0);
}

static FILETIME ft(ULONGLONG ticks)
{
	FILETIME f;
	f.dwLowDateTime = (DWORD)ticks;
	f.dwHighDateTime = (DWORD)(ticks >> 32);
	return f;
}

static const ULONGLONG SECOND = 10000000ULL;
static const ULONGLONG DAY = 24ULL * 60 * 60 * SECOND;
static const ULONGLONG NOW = 133000000000000000ULL;

static void testRemovals()
{
	printf("SessionLogSelectRemovals\n");

	// Age: more than keepDays old goes, exactly keepDays old stays, a future stamp stays.
	SessionLogEntry age[5];
	age[0].lastWrite = ft(NOW - 8 * DAY);
	age[1].lastWrite = ft(NOW - 6 * DAY);
	age[2].lastWrite = ft(NOW - 7 * DAY);
	age[3].lastWrite = ft(NOW - 7 * DAY - 1);
	age[4].lastWrite = ft(NOW + DAY);
	CHECK_EQ(SessionLogSelectRemovals(age, 5, ft(NOW), 7, 500), 2);
	CHECK(age[0].remove);
	CHECK(!age[1].remove);
	CHECK(!age[2].remove);
	CHECK(age[3].remove);
	CHECK(!age[4].remove);

	// Count: keepMax - 1 earlier logs stay (the new one makes keepMax), oldest go first.
	SessionLogEntry many[5];
	for (int i = 0; i < 5; i++)
		many[i].lastWrite = ft(NOW - (ULONGLONG)(5 - i) * SECOND);	// many[0] is the oldest
	CHECK_EQ(SessionLogSelectRemovals(many, 5, ft(NOW), 7, 3), 3);
	CHECK(many[0].remove);
	CHECK(many[1].remove);
	CHECK(many[2].remove);
	CHECK(!many[3].remove);
	CHECK(!many[4].remove);

	// Both rules: aged logs go first, then the oldest of the rest.
	SessionLogEntry mixed[5];
	mixed[0].lastWrite = ft(NOW - 10 * DAY);
	mixed[1].lastWrite = ft(NOW - 3 * SECOND);
	mixed[2].lastWrite = ft(NOW - 9 * DAY);
	mixed[3].lastWrite = ft(NOW - 1 * SECOND);
	mixed[4].lastWrite = ft(NOW - 2 * SECOND);
	CHECK_EQ(SessionLogSelectRemovals(mixed, 5, ft(NOW), 7, 3), 3);
	CHECK(mixed[0].remove);
	CHECK(mixed[1].remove);
	CHECK(mixed[2].remove);
	CHECK(!mixed[3].remove);
	CHECK(!mixed[4].remove);

	// Stale flags from the caller are reset.
	SessionLogEntry fresh[2];
	fresh[0].lastWrite = ft(NOW - SECOND);
	fresh[0].remove = true;
	fresh[1].lastWrite = ft(NOW - 2 * SECOND);
	fresh[1].remove = true;
	CHECK_EQ(SessionLogSelectRemovals(fresh, 2, ft(NOW), 7, 500), 0);
	CHECK(!fresh[0].remove);
	CHECK(!fresh[1].remove);

	CHECK_EQ(SessionLogSelectRemovals(NULL, 0, ft(NOW), 7, 500), 0);

	// keepMax 1 keeps no earlier log; keepMax 0 behaves the same.
	SessionLogEntry one[2];
	one[0].lastWrite = ft(NOW - SECOND);
	one[1].lastWrite = ft(NOW - 2 * SECOND);
	CHECK_EQ(SessionLogSelectRemovals(one, 2, ft(NOW), 7, 1), 2);
	CHECK_EQ(SessionLogSelectRemovals(one, 2, ft(NOW), 7, 0), 2);
}

int main()
{
	testName();
	testIsOurs();
	testClock();
	testStamp();
	testRemovals();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

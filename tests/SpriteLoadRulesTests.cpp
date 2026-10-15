/*=================================================================
	Unit tests for src/plugy/spriteLoadRules.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/spriteLoadRules.h"
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

// Five 4-byte tokens, each given as exactly 4 bytes (NULs included).
static void setTokens(BYTE tokens[20], const char* t0, const char* t1, const char* t2, const char* t3, const char* t4)
{
	const char* all[5] = { t0, t1, t2, t3, t4 };
	for (int i = 0; i < 5; i++)
		memcpy(tokens + i * 4, all[i], 4);
}

static void setName(BYTE* hdr, const char* name)
{
	memset(hdr + DCC_HDR_NAME_OFFSET, 0, DCC_HDR_NAME_SIZE);
	memcpy(hdr + DCC_HDR_NAME_OFFSET, name, strlen(name));
}

static void setHeader(BYTE* hdr, DWORD dirs, DWORD frames)
{
	memset(hdr, 0, DCC_HEADER_SIZE);
	*(DWORD*)(hdr + 0x14) = dirs;
	*(DWORD*)(hdr + 0x18) = frames;
}

static void setOff(BYTE* hdr, int i, DWORD value)
{
	((DWORD*)(hdr + 0x1C))[i] = value;
}

/*-------------------------------- T-NAME --------------------------------*/

static void testTokenName()
{
	BYTE tokens[20];
	char name[DCC_NAME_SIZE];

	setTokens(tokens, "TH  ", "HD  ", "th2 ", "A2  ", "hth ");
	CHECK_EQ(dccTokenName(tokens, name), 12);
	CHECK(!strcmp(name, "THHDth2A2hth"));

	setTokens(tokens, "ABCD", "    ", "    ", "    ", "    ");
	CHECK_EQ(dccTokenName(tokens, name), 3);
	CHECK(!strcmp(name, "ABC"));

	setTokens(tokens, "A B ", "    ", "    ", "    ", "    ");
	CHECK_EQ(dccTokenName(tokens, name), 1);
	CHECK(!strcmp(name, "A"));

	// A NUL is copied like any byte, so it ends the name for every later token.
	setTokens(tokens, "A\0\0\0", "HD  ", "th2 ", "A2  ", "hth ");
	CHECK_EQ(dccTokenName(tokens, name), 1);
	CHECK(!strcmp(name, "A"));

	setTokens(tokens, "TH\0 ", "HD  ", "    ", "    ", "    ");
	CHECK_EQ(dccTokenName(tokens, name), 2);
	CHECK(!strcmp(name, "TH"));

	setTokens(tokens, "TH  ", "    ", "HD  ", "    ", "    ");
	CHECK_EQ(dccTokenName(tokens, name), 4);
	CHECK(!strcmp(name, "THHD"));

	setTokens(tokens, "    ", "    ", "    ", "    ", "    ");
	CHECK_EQ(dccTokenName(tokens, name), 0);
	CHECK(!strcmp(name, ""));
}

static void testHeaderName()
{
	BYTE hdr[DCC_HEADER_SIZE];
	BYTE tokens[20];
	setHeader(hdr, 8, 16);
	setTokens(tokens, "TH  ", "HD  ", "th2 ", "A2  ", "hth ");

	setName(hdr, "THHDth2A2hth");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MATCH);
	// Bytewise, as the game's lookup: case counts.
	setName(hdr, "thhdTH2a2HTH");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MISMATCH);
	setName(hdr, "SOSHlrgNUhth");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MISMATCH);

	setName(hdr, "A");
	setTokens(tokens, "A\0\0\0", "HD  ", "th2 ", "A2  ", "hth ");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MATCH);

	setTokens(tokens, "TH  ", "HD  ", "th2 ", "A2  ", "hth ");
	memset(hdr + DCC_HDR_NAME_OFFSET, 'x', DCC_HDR_NAME_SIZE);
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_UNKNOWN);
	setName(hdr, "");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_UNKNOWN);
	setName(hdr, "THHDth2A2hth");
	setTokens(tokens, "    ", "    ", "    ", "    ", "    ");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_UNKNOWN);

	setTokens(tokens, "\x01" "B  ", "HD  ", "    ", "    ", "    ");
	setName(hdr, "\x01" "BHD");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MATCH);
	setName(hdr, "BHD");
	CHECK_EQ(dccHeaderNameMatches(hdr, tokens), DCC_NAME_MISMATCH);
}

/*-------------------------------- T-LOAD --------------------------------*/

// The sprite object's B area as the issuer fills it for count directions from start.
static void setB(BYTE* B, BYTE count, BYTE start, BYTE dirsPlusOne, BYTE frames, int end, DWORD span)
{
	memset(B, 0, 0x90);
	B[0] = 0x06;
	B[1] = count;
	B[2] = start;
	B[3] = dirsPlusOne;
	B[4] = frames;
	*(DWORD*)(B + 0xC + 4 * end) = span;
}

// X: 8 directions of 16 frames, the first four spanning 0x4FEF bytes.
static void setX(BYTE* hdr)
{
	setHeader(hdr, 8, 16);
	setName(hdr, "THHDth2A2hth");
	DWORD off[9] = { 0x2F, 0x102F, 0x202F, 0x302F, 0x2F + 0x4FEF, 0x602F, 0x702F, 0x802F, 0x902F };
	for (int i = 0; i < 9; i++)
		setOff(hdr, i, off[i]);
}

static void testLoadMatches()
{
	BYTE hdr[DCC_HEADER_SIZE];
	BYTE B[0x90];
	setB(B, 4, 0, 9, 0x10, 4, 0x4FEF);

	// Y: 16 directions of 8 frames.
	setHeader(hdr, 16, 8);
	for (int i = 0; i <= 16; i++)
		setOff(hdr, i, 0x4F + 0x100 * i);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_BAD_LAYOUT);

	setX(hdr);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_OK);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEE), DCC_LOAD_BAD_SIZE);
	setOff(hdr, 4, 0x2F + 0x5000);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x5000), DCC_LOAD_BAD_SPAN);

	setX(hdr);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 0, 0x4FEF), DCC_LOAD_BAD_RANGE);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 33, 0x4FEF), DCC_LOAD_BAD_RANGE);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 9, 0x4FEF), DCC_LOAD_BAD_RANGE);
	CHECK_EQ(dccLoadMatches(hdr, B, 0xFFFFFFFF, 4, 0x4FEF), DCC_LOAD_BAD_RANGE);
	setHeader(hdr, 0, 16);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_BAD_RANGE);
	setHeader(hdr, 33, 16);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_BAD_RANGE);

	// The read ends exactly at the last direction.
	setX(hdr);
	setB(B, 8, 0, 9, 0x10, 8, 0x902F - 0x2F);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 8, 0x9000), DCC_LOAD_OK);

	// A later start.
	setB(B, 4, 4, 9, 0x10, 8, 0x902F - (0x2F + 0x4FEF));
	CHECK_EQ(dccLoadMatches(hdr, B, 4, 4, 0x902F - (0x2F + 0x4FEF)), DCC_LOAD_OK);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_BAD_LAYOUT);

	// Descending offsets: a negative span.
	setX(hdr);
	setB(B, 4, 0, 9, 0x10, 4, 0x4FEF);
	setOff(hdr, 4, 0x2E);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0x4FEF), DCC_LOAD_BAD_SPAN);
	setOff(hdr, 4, 0x2F);
	CHECK_EQ(dccLoadMatches(hdr, B, 0, 4, 0), DCC_LOAD_BAD_SPAN);
}

/*-------------------------------- T-ISSUE --------------------------------*/

static void testIssueCopy()
{
	BYTE hdr[DCC_HEADER_SIZE];
	setX(hdr);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_OK);
	CHECK_EQ(dccIssueCopyOk(hdr, 1, 0, 4), DCC_ISSUE_OK);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 0), DCC_ISSUE_BAD_COPY);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 9), DCC_ISSUE_BAD_COPY);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0xFFFFFFFF, 4), DCC_ISSUE_BAD_COPY);

	// Span 0: every offset equal.
	for (int i = 0; i <= 8; i++)
		setOff(hdr, i, 0x2F);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);

	// Equal neighbours with a span are fine.
	setOff(hdr, 2, 0x100);
	setOff(hdr, 3, 0x100);
	setOff(hdr, 4, 0x200);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_OK);

	// One decreasing pair inside a positive span.
	setX(hdr);
	setOff(hdr, 2, 0x10);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);

	setX(hdr);
	*(DWORD*)(hdr + 0x14) = 0;
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);
	*(DWORD*)(hdr + 0x14) = 33;
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);
	*(DWORD*)(hdr + 0x14) = 2;
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);

	// The issuer fails by itself for one direction of a multi-direction type 1 file.
	setX(hdr);
	CHECK_EQ(dccIssueCopyOk(hdr, 1, 0, 1), DCC_ISSUE_VANILLA);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 1), DCC_ISSUE_OK);
	*(DWORD*)(hdr + 0x14) = 1;
	CHECK_EQ(dccIssueCopyOk(hdr, 1, 0, 1), DCC_ISSUE_OK);

	// start = d * count: directions 4..8 are sound, 0..4 are not.
	setX(hdr);
	setOff(hdr, 1, 0x10);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 1, 4), DCC_ISSUE_OK);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 0, 4), DCC_ISSUE_BAD_COPY);
	CHECK_EQ(dccIssueCopyOk(hdr, 0, 2, 4), DCC_ISSUE_BAD_COPY);
}

/*-------------------------------- T-PAIR --------------------------------*/

static SpritePairTable pairTable;
static SpritePairTable pairTable2;

static void testPairs()
{
	memset(&pairTable, 0, sizeof(pairTable));
	CHECK_EQ(spritePairCount(&pairTable, "THHDth2A2hth", "SOSHlrgNUhth"), 1);
	for (int n = 2; n <= 9; n++)
		CHECK_EQ(spritePairCount(&pairTable, "THHDth2A2hth", "SOSHlrgNUhth"), n);
	CHECK_EQ(spritePairCount(&pairTable, "THHDth2A2hth", "ZMTRlitA1hth"), 1);
	CHECK_EQ(spritePairCount(&pairTable, "ZMTRlitA1hth", "SOSHlrgNUhth"), 1);
	CHECK_EQ(pairTable.used, 3);

	char expected[DCC_NAME_SIZE];
	for (int i = 3; i < SPRITE_PAIR_CAPACITY; i++)
	{
		sprintf(expected, "F%d", i);
		CHECK_EQ(spritePairCount(&pairTable, expected, "H"), 1);
	}
	CHECK_EQ(pairTable.used, SPRITE_PAIR_CAPACITY);
	CHECK_EQ(pairTable.overflow, 0);
	CHECK_EQ(spritePairCount(&pairTable, "NEW", "PAIR"), 0);
	CHECK_EQ(pairTable.overflow, 1);
	CHECK_EQ(spritePairCount(&pairTable, "THHDth2A2hth", "SOSHlrgNUhth"), 10);

	// A header name with no NUL in its 0x20 bytes: one 31-char form, stored and matched as is.
	BYTE hdr[DCC_HEADER_SIZE];
	setHeader(hdr, 8, 16);
	memset(hdr + DCC_HDR_NAME_OFFSET, 'n', DCC_HDR_NAME_SIZE);
	char found[DCC_COPY_NAME_SIZE];
	dccCopyName(hdr, found);
	CHECK_EQ(strlen(found), DCC_COPY_NAME_SIZE - 1);
	memset(&pairTable2, 0, sizeof(pairTable2));
	CHECK_EQ(spritePairCount(&pairTable2, "THHDth2A2hth", found), 1);
	CHECK(!strcmp(pairTable2.pairs[0].found, found));
	char again[DCC_COPY_NAME_SIZE];
	dccCopyName(hdr, again);
	CHECK_EQ(spritePairCount(&pairTable2, "THHDth2A2hth", again), 2);

	setName(hdr, "SOSHlrgNUhth");
	dccCopyName(hdr, found);
	CHECK(!strcmp(found, "SOSHlrgNUhth"));
}

/*-------------------------------- T-PRINT --------------------------------*/

static void testPrintableName()
{
	char shown[DCC_COPY_NAME_SIZE];
	dccPrintableName("SOSHlrgNUhth", shown);
	CHECK(!strcmp(shown, "SOSHlrgNUhth"));
	dccPrintableName(" ~", shown);
	CHECK(!strcmp(shown, " ~"));
	dccPrintableName("A\r\nB\x01\x1F" "C", shown);
	CHECK(!strcmp(shown, "A??B??C"));
	dccPrintableName("\x7F\x80\xFF" "D", shown);
	CHECK(!strcmp(shown, "???D"));
	dccPrintableName("", shown);
	CHECK(!strcmp(shown, ""));

	// The logged form of a stale copy: same length as its raw form, which the pair table keeps.
	BYTE hdr[DCC_HEADER_SIZE];
	setHeader(hdr, 8, 16);
	memset(hdr + DCC_HDR_NAME_OFFSET, '\n', DCC_HDR_NAME_SIZE);
	char raw[DCC_COPY_NAME_SIZE];
	dccCopyName(hdr, raw);
	dccPrintableName(raw, shown);
	CHECK_EQ(strlen(shown), DCC_COPY_NAME_SIZE - 1);
	CHECK(!strchr(shown, '\n'));
	CHECK_EQ(raw[0], '\n');
}

/*-------------------------------- T-LIVE --------------------------------*/

static void testLive()
{
	BYTE nameRec[0x30] = {};
	BYTE poolRec[0x14] = {};
	BYTE hdr[DCC_HEADER_SIZE] = {};
	BYTE other[DCC_HEADER_SIZE] = {};
	*(BYTE**)(nameRec + 0x24) = poolRec;
	*(BYTE**)poolRec = hdr;
	CHECK(dccHeaderIsLive(nameRec, hdr));
	CHECK(!dccHeaderIsLive(NULL, hdr));
	*(BYTE**)poolRec = other;
	CHECK(!dccHeaderIsLive(nameRec, hdr));
	*(BYTE**)poolRec = hdr;
	*(BYTE**)(nameRec + 0x24) = NULL;
	CHECK(!dccHeaderIsLive(nameRec, hdr));
}

/*-------------------------------- T-RELINK --------------------------------*/

#define NB_RECORDS 8

static HcRecord recs[NB_RECORDS];
static BYTE owners[NB_RECORDS][0x30];
static int releases;
static HcRecord* released;

static void countRelease(HcRecord* rec, void* ctx)
{
	releases++;
	released = rec;
	CHECK(ctx == (void*)recs);
}

// Record i: live (data, size 0x10 * (i + 1)) or not, its owner pointing back.
static void setRecord(int i, bool live)
{
	recs[i].data = live ? 0x1000 + i : 0;
	recs[i].size = live ? 0x10 * (i + 1) : 0;
	recs[i].owner = live ? (DWORD)owners[i] : 0;
	*(DWORD*)(owners[i] + 0x24) = live ? (DWORD)&recs[i] : 0;
}

// Walks a list from its head; counts each record's appearances, bounded so a loop ends.
static int walk(DWORD head, int seen[NB_RECORDS])
{
	int n = 0;
	for (DWORD p = head; p && n < 4 * NB_RECORDS; p = ((HcRecord*)p)->next, n++)
	{
		int i = (int)(((HcRecord*)p) - recs);
		if (i >= 0 && i < NB_RECORDS)
			seen[i]++;
	}
	return n;
}

// Both lists together hold every record exactly once, live ones in the LRU.
static void checkLists(const HcLists* lists, DWORD expectedCount, DWORD expectedUsed)
{
	int seenLru[NB_RECORDS] = {};
	int seenFree[NB_RECORDS] = {};
	int lru = walk(lists->head, seenLru);
	int freed = walk(lists->freeHead, seenFree);
	CHECK_EQ(lru, expectedCount);
	CHECK_EQ(lists->count, expectedCount);
	CHECK_EQ(lists->used, expectedUsed);
	CHECK_EQ(lru + freed, NB_RECORDS);
	for (int i = 0; i < NB_RECORDS; i++)
	{
		CHECK_EQ(seenLru[i] + seenFree[i], 1);
		CHECK_EQ(seenLru[i], recs[i].data ? 1 : 0);
	}
	if (lists->tail)
		CHECK_EQ(((HcRecord*)lists->tail)->next, 0);
	if (lists->freeTail)
		CHECK_EQ(((HcRecord*)lists->freeTail)->next, 0);
	if (lists->head)
		CHECK_EQ(((HcRecord*)lists->head)->prev, 0);
}

static void testRelink()
{
	HcLists lists;

	// Healthy: records 0-4 live and linked, 5-7 free.
	memset(recs, 0, sizeof(recs));
	for (int i = 0; i < NB_RECORDS; i++)
		setRecord(i, i < 5);
	for (int i = 0; i < 4; i++)
	{
		recs[i].next = (DWORD)&recs[i + 1];
		recs[i + 1].prev = (DWORD)&recs[i];
	}
	releases = 0;
	hcRelink(recs, NB_RECORDS, &lists, countRelease, recs);
	checkLists(&lists, 5, 0x10 + 0x20 + 0x30 + 0x40 + 0x50);
	CHECK_EQ(releases, 0);
	for (int i = 0; i < 5; i++)
		CHECK_EQ(recs[i].owner, (DWORD)owners[i]);
	CHECK_EQ(lists.head, (DWORD)&recs[0]);
	CHECK_EQ(lists.tail, (DWORD)&recs[4]);

	// Orphans: live records linked nowhere, plus a self-loop record.
	memset(recs, 0, sizeof(recs));
	for (int i = 0; i < NB_RECORDS; i++)
		setRecord(i, i % 2 == 0);
	recs[2].next = recs[2].prev = (DWORD)&recs[2];
	recs[5].next = recs[5].prev = (DWORD)&recs[5];
	releases = 0;
	hcRelink(recs, NB_RECORDS, &lists, countRelease, recs);
	checkLists(&lists, 4, 0x10 + 0x30 + 0x50 + 0x70);
	CHECK_EQ(releases, 0);

	// A live record whose owner moved on loses it; a freed record whose owner still points back is released once.
	memset(recs, 0, sizeof(recs));
	for (int i = 0; i < NB_RECORDS; i++)
		setRecord(i, i < 6);
	*(DWORD*)(owners[3] + 0x24) = (DWORD)&recs[0];
	recs[5].data = 0;
	releases = 0;
	released = NULL;
	hcRelink(recs, NB_RECORDS, &lists, countRelease, recs);
	CHECK_EQ(recs[3].owner, 0);
	CHECK_EQ(recs[0].owner, (DWORD)owners[0]);
	CHECK_EQ(releases, 1);
	CHECK(released == &recs[5]);
	CHECK_EQ(recs[5].owner, 0);
	CHECK_EQ(recs[5].size, 0);
	checkLists(&lists, 5, 0x10 + 0x20 + 0x30 + 0x40 + 0x50);

	// A freed record whose owner points elsewhere is not released.
	memset(recs, 0, sizeof(recs));
	for (int i = 0; i < NB_RECORDS; i++)
		setRecord(i, true);
	recs[7].data = 0;
	*(DWORD*)(owners[7] + 0x24) = 0;
	releases = 0;
	hcRelink(recs, NB_RECORDS, &lists, countRelease, recs);
	CHECK_EQ(releases, 0);
	CHECK_EQ(recs[7].owner, 0);
	checkLists(&lists, 7, 0x10 + 0x20 + 0x30 + 0x40 + 0x50 + 0x60 + 0x70);

	// Nothing live: an empty LRU and every record free.
	memset(recs, 0, sizeof(recs));
	hcRelink(recs, NB_RECORDS, &lists, countRelease, recs);
	checkLists(&lists, 0, 0);
	CHECK_EQ(lists.head, 0);
	CHECK_EQ(lists.tail, 0);
}

/*-------------------------------- T-LOG --------------------------------*/

static void testLogThis()
{
	bool firstAll = true;
	for (LONG n = 1; n <= 32; n++)
		firstAll = firstAll && spriteLogThis(n);
	CHECK(firstAll);
	CHECK(!spriteLogThis(33));
	CHECK(!spriteLogThis(255));
	CHECK(spriteLogThis(256));
	CHECK(!spriteLogThis(257));
	CHECK(spriteLogThis(512));
}

int main()
{
	testTokenName();
	testHeaderName();
	testLoadMatches();
	testIssueCopy();
	testPairs();
	testPrintableName();
	testLive();
	testRelink();
	testLogThis();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

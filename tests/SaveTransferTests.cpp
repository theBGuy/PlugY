/*=================================================================
	Unit tests for src/plugy/saveTransfer.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/saveTransfer.h"
#include <stdio.h>
#include <string.h>
#include <vector>

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

static const DWORD CAP = SAVE_TRANSFER_MAX_SIZE;

// One received stream, driven through checkSaveChunk alone: the receivers' sticky refusal and their discard/free are
// not modelled here.
struct Stream
{
	DWORD size;
	DWORD cur;
	std::vector<BYTE> data;
	bool refused;
};

static SaveChunkResult feed(Stream* s, BYTE packSize, DWORD finalSize, const BYTE* payload)
{
	SaveChunkResult r = checkSaveChunk(s->size, s->cur, packSize, finalSize, CAP);
	if (r == SAVE_CHUNK_REJECT)
	{
		s->refused = true;
		return r;
	}
	if (r == SAVE_CHUNK_START)
	{
		s->size = finalSize;
		s->data.assign(finalSize, 0);
	}
	// An accepted chunk that would overrun the buffer is a failed check here, not a crash of the test.
	DWORD len = packSize ? packSize - 1 : 0;
	if (s->cur > s->data.size() || len > s->data.size() - s->cur)
	{
		check(false, "accepted chunk fits the buffer", __LINE__);
		return r;
	}
	if (len)
		memcpy(&s->data[s->cur], payload, len);
	s->cur += len;
	return r;
}

// Sends a whole file the way the senders chunk it: 0xFE payload bytes per packet, the last one shorter.
static void sendAll(Stream* s, const std::vector<BYTE>& file, int* starts, int* appends, int* rejects)
{
	DWORD sent = 0;
	while (sent < file.size())
	{
		DWORD remaining = (DWORD)file.size() - sent;
		BYTE packSize = remaining > 0xFE ? 0xFF : (BYTE)(remaining + 1);
		SaveChunkResult r = feed(s, packSize, (DWORD)file.size(), &file[sent]);
		if (r == SAVE_CHUNK_START) (*starts)++;
		else if (r == SAVE_CHUNK_APPEND) (*appends)++;
		else (*rejects)++;
		sent += packSize - 1;
	}
}

static std::vector<BYTE> pattern(DWORD size, BYTE seed)
{
	std::vector<BYTE> v(size);
	for (DWORD i = 0; i < size; i++)
		v[i] = (BYTE)(seed + i * 7);
	return v;
}

static void testLegitSequence()
{
	printf("legit two-stream sequence\n");
	// The extended file first, then the shared one, which starts again from size 0.
	std::vector<BYTE> extended = pattern(2 * 0xFE + 16, 1);
	std::vector<BYTE> shared = pattern(0xFE + 5, 99);
	Stream ext = {};
	Stream shr = {};
	int starts = 0, appends = 0, rejects = 0;
	sendAll(&ext, extended, &starts, &appends, &rejects);
	CHECK_EQ(starts, 1);
	CHECK_EQ(appends, 2);
	CHECK_EQ(rejects, 0);
	CHECK_EQ(ext.size, extended.size());
	CHECK_EQ(ext.cur, ext.size);
	CHECK(ext.data == extended);

	starts = appends = rejects = 0;
	CHECK_EQ(shr.size, 0);
	sendAll(&shr, shared, &starts, &appends, &rejects);
	CHECK_EQ(starts, 1);
	CHECK_EQ(appends, 1);
	CHECK_EQ(rejects, 0);
	CHECK_EQ(shr.cur, shr.size);
	CHECK(shr.data == shared);
	CHECK(!ext.refused && !shr.refused);

	// A file that fits in one short chunk, and one of exactly one full chunk.
	std::vector<BYTE> tiny = pattern(3, 5);
	Stream t = {};
	starts = appends = rejects = 0;
	sendAll(&t, tiny, &starts, &appends, &rejects);
	CHECK_EQ(starts, 1);
	CHECK_EQ(appends, 0);
	CHECK_EQ(t.cur, 3);
	CHECK(t.data == tiny);
	std::vector<BYTE> full = pattern(0xFE, 7);
	Stream f = {};
	starts = appends = rejects = 0;
	sendAll(&f, full, &starts, &appends, &rejects);
	CHECK_EQ(starts, 1);
	CHECK_EQ(appends, 0);
	CHECK_EQ(f.cur, 0xFE);
	CHECK(f.data == full);
}

static void testHeaderFields()
{
	printf("packSize and finalSize\n");
	// packSize 0 has no payload length at all, on a new and on a started stream.
	CHECK_EQ(checkSaveChunk(0, 0, 0, 100, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(100, 10, 0, 100, CAP), SAVE_CHUNK_REJECT);
	// finalSize 0, on a new and on a started stream.
	CHECK_EQ(checkSaveChunk(0, 0, 1, 0, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(0, 0, 0xFF, 0, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(100, 10, 5, 0, CAP), SAVE_CHUNK_REJECT);
	// The cap: exactly the cap is a valid file, one byte more is refused; the largest 31-bit value too.
	CHECK_EQ(checkSaveChunk(0, 0, 0xFF, CAP, CAP), SAVE_CHUNK_START);
	CHECK_EQ(checkSaveChunk(0, 0, 0xFF, CAP + 1, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(0, 0, 0xFF, 0x7FFFFFFF, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(CAP, 0xFE, 0xFF, CAP, CAP), SAVE_CHUNK_APPEND);
	CHECK_EQ(checkSaveChunk(CAP + 1, 0, 0xFF, CAP + 1, CAP), SAVE_CHUNK_REJECT);
	CHECK(SAVE_TRANSFER_MAX_SIZE >= 16 * 1024 * 1024);
	CHECK(SAVE_TRANSFER_MAX_SIZE <= 0x7FFFFFFF);
}

static void testFirstChunk()
{
	printf("first chunk\n");
	// The first payload may be the whole file or less, never more.
	CHECK_EQ(checkSaveChunk(0, 0, 11, 10, CAP), SAVE_CHUNK_START);
	CHECK_EQ(checkSaveChunk(0, 0, 10, 10, CAP), SAVE_CHUNK_START);
	CHECK_EQ(checkSaveChunk(0, 0, 12, 10, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(0, 0, 0xFF, 1, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(0, 0, 2, 1, CAP), SAVE_CHUNK_START);
	// An empty first payload starts the stream.
	CHECK_EQ(checkSaveChunk(0, 0, 1, 10, CAP), SAVE_CHUNK_START);

	Stream s = {};
	BYTE big[0xFE] = {};
	CHECK_EQ(feed(&s, 0xFF, 0x10, big), SAVE_CHUNK_REJECT);
	CHECK(s.refused);
	CHECK_EQ(s.size, 0);
	CHECK(s.data.empty());
}

static void testStartedStream()
{
	printf("started stream\n");
	// finalSize changed mid-stream, up or down.
	CHECK_EQ(checkSaveChunk(1000, 254, 0xFF, 2000, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 254, 0xFF, 999, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 254, 0xFF, 1000, CAP), SAVE_CHUNK_APPEND);

	// Space left: a payload of exactly size - cur fits, one byte more does not.
	CHECK_EQ(checkSaveChunk(1000, 900, 101, 1000, CAP), SAVE_CHUNK_APPEND);
	CHECK_EQ(checkSaveChunk(1000, 900, 102, 1000, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 999, 2, 1000, CAP), SAVE_CHUNK_APPEND);
	CHECK_EQ(checkSaveChunk(1000, 999, 3, 1000, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 900, 0xFF, 1000, CAP), SAVE_CHUNK_REJECT);

	// A chunk after the stream is complete.
	CHECK_EQ(checkSaveChunk(1000, 1000, 2, 1000, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 1000, 0xFF, 1000, CAP), SAVE_CHUNK_REJECT);

	// cur beyond size never happens through these checks, but is refused all the same.
	CHECK_EQ(checkSaveChunk(1000, 1001, 1, 1000, CAP), SAVE_CHUNK_REJECT);
	CHECK_EQ(checkSaveChunk(1000, 0xFFFFFFFF, 2, 1000, CAP), SAVE_CHUNK_REJECT);

	// packSize 1 carries no payload: accepted on a started stream, nothing is copied, even when complete.
	CHECK_EQ(checkSaveChunk(1000, 500, 1, 1000, CAP), SAVE_CHUNK_APPEND);
	CHECK_EQ(checkSaveChunk(1000, 1000, 1, 1000, CAP), SAVE_CHUNK_APPEND);
	Stream s = {};
	BYTE first[4] = { 1, 2, 3, 4 };
	CHECK_EQ(feed(&s, 5, 10, first), SAVE_CHUNK_START);
	CHECK_EQ(feed(&s, 1, 10, NULL), SAVE_CHUNK_APPEND);
	CHECK_EQ(s.cur, 4);
	CHECK(!s.refused);
	CHECK_EQ(s.data[4], 0);

	// A refused chunk leaves cur where it was, and the check itself keeps no refusal state.
	Stream o = {};
	BYTE chunk[0xFE] = {};
	CHECK_EQ(feed(&o, 0xFF, 300, chunk), SAVE_CHUNK_START);
	CHECK_EQ(feed(&o, 0xFF, 300, chunk), SAVE_CHUNK_REJECT);
	CHECK(o.refused);
	CHECK_EQ(o.cur, 0xFE);
	CHECK_EQ(feed(&o, 300 - 0xFE + 1, 300, chunk), SAVE_CHUNK_APPEND);
	CHECK_EQ(o.cur, 300);
	CHECK_EQ(feed(&o, 2, 300, chunk), SAVE_CHUNK_REJECT);
	CHECK_EQ(o.cur, 300);
}

int main()
{
	testLegitSequence();
	testHeaderFields();
	testFirstChunk();
	testStartedStream();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

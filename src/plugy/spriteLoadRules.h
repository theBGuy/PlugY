/*=================================================================
	Rules of the 1.14d sprite load checks: the DCC file name the game builds from a sprite's tokens, the checks of a
	header copy against the load it serves, the reject cap per (file, header) pair and the repair of the header
	cache's lists. No D2 or PlugY dependencies, so tests/SpriteLoadRulesTests.cpp covers them.
=================================================================*/
#pragma once

#include <windows.h>
#include <string.h>

#define DCC_HEADER_SIZE			0xC0
#define DCC_NAME_SIZE			16		// 5 tokens x 3 chars + NUL
#define DCC_COPY_NAME_SIZE		32		// a header name as logged and stored: at most 31 chars + NUL
#define SPRITE_REJECT_CAP		8
#define SPRITE_PAIR_CAPACITY	256

// DCC header fields: +0x14 directions, +0x18 frames per direction, +0x1C the 33 direction offsets, +0xA0 the name.
#define DCC_HDR_DIRS(h)			(*(const DWORD*)((h) + 0x14))
#define DCC_HDR_FRAMES(h)		(*(const DWORD*)((h) + 0x18))
#define DCC_HDR_OFF(h, i)		(((const DWORD*)((h) + 0x1C))[i])
#define DCC_HDR_NAME_OFFSET		0xA0
#define DCC_HDR_NAME_SIZE		0x20
#define DCC_MAX_DIRS			32

enum DccNameVerdict { DCC_NAME_MATCH, DCC_NAME_MISMATCH, DCC_NAME_UNKNOWN };
enum DccLoadVerdict { DCC_LOAD_OK, DCC_LOAD_BAD_RANGE, DCC_LOAD_BAD_LAYOUT, DCC_LOAD_BAD_SPAN, DCC_LOAD_BAD_SIZE };
enum DccIssueVerdict { DCC_ISSUE_OK, DCC_ISSUE_VANILLA, DCC_ISSUE_BAD_COPY };

// Port of the game's name builder (0x5FE354): per token the first three bytes in order, the token ending at its first
// space; a NUL is copied like any byte, so the name ends at the first NUL of any token. The lookup and the header's
// own name compare bytewise, so no case folding. Returns strlen(out).
inline int dccTokenName(const BYTE tokens[20], char out[DCC_NAME_SIZE])
{
	int n = 0;
	for (int t = 0; t < 5; t++)
		for (int b = 0; b < 3; b++)
		{
			BYTE c = tokens[t * 4 + b];
			if (c == 0x20)
				break;
			out[n++] = (char)c;
		}
	out[n] = '\0';
	return (int)strlen(out);
}

// UNKNOWN when either name is empty or the header's has no NUL in its 0x20 bytes: nothing to compare then.
inline DccNameVerdict dccHeaderNameMatches(const BYTE* hdr, const BYTE tokens[20])
{
	const char* name = (const char*)hdr + DCC_HDR_NAME_OFFSET;
	if (!memchr(name, 0, DCC_HDR_NAME_SIZE) || !name[0])
		return DCC_NAME_UNKNOWN;
	char expected[DCC_NAME_SIZE];
	if (!dccTokenName(tokens, expected))
		return DCC_NAME_UNKNOWN;
	return strcmp(name, expected) == 0 ? DCC_NAME_MATCH : DCC_NAME_MISMATCH;
}

// The issuer's halts on the header (0x5FFA20: span 0 halts 0x3A9, a direction past 32 halts 0x3C1, an offset below the
// start offset halts 0x3C3) as rejects, run on the copy before the request goes out, plus range checks it does not make.
// VANILLA is the case where the issuer fails by itself (type 1, one direction asked of a multi-direction file).
inline DccIssueVerdict dccIssueCopyOk(const BYTE* hdr, DWORD type, DWORD d, DWORD count)
{
	DWORD dirs = DCC_HDR_DIRS(hdr);
	if (type == 1 && count == 1 && dirs != 1)
		return DCC_ISSUE_VANILLA;
	if (count == 0 || count > DCC_MAX_DIRS || d > DCC_MAX_DIRS)
		return DCC_ISSUE_BAD_COPY;
	DWORD start = d * count;
	DWORD end = start + count;
	if (end > DCC_MAX_DIRS || dirs < 1 || dirs > DCC_MAX_DIRS || end > dirs)
		return DCC_ISSUE_BAD_COPY;
	DWORD base = DCC_HDR_OFF(hdr, start);
	if (DCC_HDR_OFF(hdr, end) == base)
		return DCC_ISSUE_BAD_COPY;
	for (DWORD i = start + 1; i <= end; i++)
		if ((LONG)(DCC_HDR_OFF(hdr, i) - base) < 0)
			return DCC_ISSUE_BAD_COPY;
	return DCC_ISSUE_OK;
}

// The completed read against the sprite object it fills (B = S+0x20: +1 count, +2 start, +3 dirs+1, +4 frames,
// +0xC+4i the offsets relative to start) and the size read. Reads hdr[0..0x9F] and B[0..0x8F] only.
inline DccLoadVerdict dccLoadMatches(const BYTE* hdr, const BYTE* B, DWORD start, DWORD count, DWORD size)
{
	DWORD dirs = DCC_HDR_DIRS(hdr);
	if (count == 0 || count > DCC_MAX_DIRS || start > DCC_MAX_DIRS)
		return DCC_LOAD_BAD_RANGE;
	DWORD end = start + count;
	if (end > DCC_MAX_DIRS || dirs == 0 || dirs > DCC_MAX_DIRS || end > dirs)
		return DCC_LOAD_BAD_RANGE;
	if (B[1] != (BYTE)count || B[2] != (BYTE)start || B[3] != (BYTE)(dirs + 1) || B[4] != hdr[0x18])
		return DCC_LOAD_BAD_LAYOUT;
	DWORD span = DCC_HDR_OFF(hdr, end) - DCC_HDR_OFF(hdr, start);
	if ((LONG)span <= 0 || span != *(const DWORD*)(B + 0xC + 4 * end))
		return DCC_LOAD_BAD_SPAN;
	if (span != size)
		return DCC_LOAD_BAD_SIZE;
	return DCC_LOAD_OK;
}

// One truncated form of a header's name for lookup and storage; logs print it through dccPrintableName.
inline void dccCopyName(const BYTE* hdr, char out[DCC_COPY_NAME_SIZE])
{
	const char* name = (const char*)hdr + DCC_HDR_NAME_OFFSET;
	int n = 0;
	while (n < DCC_COPY_NAME_SIZE - 1 && name[n])
	{
		out[n] = name[n];
		n++;
	}
	out[n] = '\0';
}

// For logs only: a header's name is printed from its raw bytes (in fix-off test runs, from a cache slot another file
// may hold by then), and a control byte would split the session log's line. Every byte outside printable ASCII
// becomes '?'; comparisons and the pair table keep the dccCopyName form.
inline void dccPrintableName(const char* name, char out[DCC_COPY_NAME_SIZE])
{
	int n = 0;
	for (; n < DCC_COPY_NAME_SIZE - 1 && name[n]; n++)
	{
		BYTE c = (BYTE)name[n];
		out[n] = c < 0x20 || c > 0x7E ? '?' : (char)c;
	}
	out[n] = '\0';
}

struct SpritePair { char expected[DCC_NAME_SIZE]; char found[DCC_COPY_NAME_SIZE]; DWORD count; DWORD counted; };
struct SpritePairTable { SpritePair pairs[SPRITE_PAIR_CAPACITY]; DWORD used; DWORD clock; DWORD recycled; };

// Returns the pair's new reject count; a new pair starts at 1 and sets *isNew. It takes a slot whose streak was
// reset, else a free one, else the slot of the pair counted longest ago: a streak left behind when its sprite went
// off screen must not hold its slot for good, while a load rejected on every frame stays among the most recently
// counted and reaches the cap.
inline DWORD spritePairCount(SpritePairTable* t, const char* expected, const char* found, bool* isNew = NULL)
{
	t->clock++;
	if (isNew)
		*isNew = false;
	SpritePair* reset = NULL;
	SpritePair* oldest = NULL;
	for (DWORD i = 0; i < t->used; i++)
	{
		SpritePair* q = &t->pairs[i];
		if (!strcmp(q->expected, expected) && !strcmp(q->found, found))
		{
			q->counted = t->clock;
			return ++q->count;
		}
		if (!reset && !q->count)
			reset = q;
		if (!oldest || t->clock - q->counted > t->clock - oldest->counted)
			oldest = q;
	}
	SpritePair* p = reset;
	if (!p && t->used < SPRITE_PAIR_CAPACITY)
		p = &t->pairs[t->used++];
	if (!p)
	{
		p = oldest;
		t->recycled++;
	}
	strncpy(p->expected, expected, DCC_NAME_SIZE - 1);
	p->expected[DCC_NAME_SIZE - 1] = '\0';
	strncpy(p->found, found, DCC_COPY_NAME_SIZE - 1);
	p->found[DCC_COPY_NAME_SIZE - 1] = '\0';
	p->count = 1;
	p->counted = t->clock;
	if (isNew)
		*isNew = true;
	return 1;
}

// A file whose load passed the issue checks starts all its pairs over, so the cap counts rejects in a row: a race
// rejects a load now and then over a session, a rule that is wrong for a file rejects every one of its loads.
inline void spritePairReset(SpritePairTable* t, const char* expected)
{
	for (DWORD i = 0; i < t->used; i++)
		if (!strcmp(t->pairs[i].expected, expected))
			t->pairs[i].count = 0;
}

// A load whose pair reached this count goes through: past the cap of rejects in a row.
inline bool spritePastCap(DWORD n) { return n > SPRITE_REJECT_CAP; }

// Every event up to the 32nd, then one in 256: a soak keeps its first cases and a running total.
inline bool spriteLogThis(LONG n) { return n <= 32 || n % 256 == 0; }

// The file's name record still holds a pool record (+0x24) whose data (+0) is this header: the links the header getter
// follows from its lookup (0x5FE9C4-0x5FE9D4), with the null check and the comparison the getter does not make.
inline bool dccHeaderIsLive(const BYTE* nameRec, const BYTE* hdr)
{
	const BYTE* poolRec = nameRec ? *(const BYTE* const*)(nameRec + 0x24) : 0;
	return poolRec != 0 && *(const BYTE* const*)poolRec == hdr;
}

// Header pool record (0x14 bytes, 32-bit fields): the LRU and the free list share next/prev.
struct HcRecord { DWORD data, size, owner, next, prev; };
struct HcLists { DWORD head, tail, freeHead, freeTail, count, used; };
typedef void (*HcOwnerRelease)(HcRecord* rec, void* ctx);

// True when a record of the pool holds this buffer. The header getter returns only such buffers (a record's data on a
// hit, a newly allocated record's on a miss), so one that no record holds has left the cache: evicted, its buffer
// freed, or dropped by a teardown.
inline bool hcHoldsData(const HcRecord* records, DWORD max, const BYTE* data)
{
	if (!records || !data)
		return false;
	for (DWORD i = 0; i < max; i++)
		if (records[i].data == (DWORD)data)
			return true;
	return false;
}

inline void hcAppend(DWORD* head, DWORD* tail, HcRecord* rec)
{
	rec->prev = *tail;
	rec->next = 0;
	if (*tail)
		((HcRecord*)*tail)->next = (DWORD)rec;
	else
		*head = (DWORD)rec;
	*tail = (DWORD)rec;
}

// Rebuilds both lists from the record array alone, since the old links are what is broken: a record with data goes
// to the LRU (in array order), keeping its owner only if the owner still points back; one without data goes to the
// free list, its owner released first when it still points back, as the pool's own eviction does.
inline void hcRelink(HcRecord* records, DWORD max, HcLists* out, HcOwnerRelease release, void* ctx)
{
	out->head = out->tail = out->freeHead = out->freeTail = out->count = out->used = 0;
	for (DWORD i = 0; i < max; i++)
	{
		HcRecord* rec = &records[i];
		bool ownerBack = rec->owner != 0 && *(const DWORD*)(rec->owner + 0x24) == (DWORD)rec;
		if (rec->data)
		{
			if (!ownerBack)
				rec->owner = 0;
			hcAppend(&out->head, &out->tail, rec);
			out->count++;
			out->used += rec->size;
		}
		else
		{
			if (ownerBack && release)
				release(rec, ctx);
			rec->size = 0;
			rec->owner = 0;
			hcAppend(&out->freeHead, &out->freeTail, rec);
		}
	}
}

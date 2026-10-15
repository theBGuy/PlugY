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

// The issuer's own range and offset checks (0x5FFA20), run on the copy before the request goes out. VANILLA is the
// case where the issuer fails by itself (type 1, one direction asked of a multi-direction file).
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
	// Equal neighbours are allowed, as the issuer's sign test is.
	for (DWORD i = start; i < end; i++)
		if (DCC_HDR_OFF(hdr, i + 1) < DCC_HDR_OFF(hdr, i))
			return DCC_ISSUE_BAD_COPY;
	if (DCC_HDR_OFF(hdr, end) - DCC_HDR_OFF(hdr, start) == 0)
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

// For logs only: a stale copy can hold freed bytes, and a control byte would split the session log's line. Every
// byte outside printable ASCII becomes '?'; comparisons and the pair table keep the dccCopyName form.
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

struct SpritePair { char expected[DCC_NAME_SIZE]; char found[DCC_COPY_NAME_SIZE]; DWORD count; };
struct SpritePairTable { SpritePair pairs[SPRITE_PAIR_CAPACITY]; DWORD used; DWORD overflow; };

// Returns the pair's new reject count (a new pair starts at 1), or 0 when the table is full: a full table must not
// let a rule bug reject the same load forever.
inline DWORD spritePairCount(SpritePairTable* t, const char* expected, const char* found)
{
	for (DWORD i = 0; i < t->used; i++)
	{
		SpritePair* p = &t->pairs[i];
		if (!strcmp(p->expected, expected) && !strcmp(p->found, found))
			return ++p->count;
	}
	if (t->used >= SPRITE_PAIR_CAPACITY)
	{
		t->overflow++;
		return 0;
	}
	SpritePair* p = &t->pairs[t->used++];
	strncpy(p->expected, expected, DCC_NAME_SIZE - 1);
	p->expected[DCC_NAME_SIZE - 1] = '\0';
	strncpy(p->found, found, DCC_COPY_NAME_SIZE - 1);
	p->found[DCC_COPY_NAME_SIZE - 1] = '\0';
	p->count = 1;
	return 1;
}

// Every event up to the 32nd, then one in 256: a soak keeps its first cases and a running total.
inline bool spriteLogThis(LONG n) { return n <= 32 || n % 256 == 0; }

// The header getter's own liveness test (0x5FE9C4-0x5FE9D4): the file's name record holds a pool record (+0x24)
// whose data (+0) is still this header.
inline bool dccHeaderIsLive(const BYTE* nameRec, const BYTE* hdr)
{
	const BYTE* poolRec = nameRec ? *(const BYTE* const*)(nameRec + 0x24) : 0;
	return poolRec != 0 && *(const BYTE* const*)poolRec == hdr;
}

// Header pool record (0x14 bytes, 32-bit fields): the LRU and the free list share next/prev.
struct HcRecord { DWORD data, size, owner, next, prev; };
struct HcLists { DWORD head, tail, freeHead, freeTail, count, used; };
typedef void (*HcOwnerRelease)(HcRecord* rec, void* ctx);

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

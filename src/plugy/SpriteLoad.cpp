/*=================================================================
	Sprite loading: an asynchronous DCC sprite load decodes with its own copy of its file's header, and the game's
	DCC header cache is used under one lock. 1.14d only.

	Vanilla hands the loader thread a raw pointer to the file's 0xC0-byte header, which lives in an unlocked LRU
	cache: when the slot is evicted and reused while the read is in flight, the decoder decodes one file with another
	file's header and the game halts ("Sprite Decompression Error", Halt 1632). The cache may also be reached from
	more than one thread, and an LRU emptied by such a race would make the cache's allocator spin forever.
	The patches copy the header into each sprite object when its load is issued, after checking under the cache lock
	that the cache still holds it and that it is still that file's; a stale or wrong header drops the load through the
	game's own "cannot issue" path, so it retries on the next frame. A header the cache no longer holds is never sent;
	a wrong one goes through after 8 rejects in a row, so that a check that is wrong for a file cannot keep its sprite
	blank. The read is checked against the copy before it is decoded, and a load that did not decode ends in the
	game's own "no graphics" state instead of a halt. A synchronous load, which waits for its own read, holds the
	cache lock while it waits. An emptied LRU is rebuilt from the cache's records.

=================================================================*/

#include "spriteLoad.h"
#include "spriteLoadRules.h"
#include "common.h"
#include <stdio.h>
#include <intrin.h>			// _InterlockedAnd8()

// 1.14d Game.exe, as offsets from offset_D2Game.
#define SL_ISSUER				0x1FFA20	// eax=desc, ecx=d, edx=count, esi=S, push file, d, hdr; ret 0Ch; -1 = cannot issue
#define SL_ISSUE_CALL			0x20001A	// call 005FFA20, the only call to the issuer (in 0x5FFFA0)
#define SL_LOAD_DONE			0x1FF6E0	// stdcall(buf, size, cb), ret 0Ch: the loader's completion callback
#define SL_LOAD_DONE_MOV		0x1FFB8E	// mov [ebp-30h],5FF6E0h: the callback put in the request descriptor
#define SL_FINALIZER			0x1FEC90	// edi=S, push desc, hdr; ret 8: waits for the read and builds the frames
#define SL_FINALIZE_CALL		0x2003A2	// call 005FEC90, the only asynchronous finalize (in 0x6001F0)
#define SL_FINALIZE_EXIT		0x2003C9	// 0x6001F0's "no sprite" epilogue: pop edi, pop esi, xor eax,eax, pop ebx
#define SL_ALLOC_SIZE_ASYNC		0x1FFFA8	// mov edx,0B8h: sprite object size, asynchronous load
#define SL_MEMSET_ASYNC			0x1FFFC4	// push 0B8h
#define SL_ALLOC_SIZE_SYNC		0x1FFEA4	// mov edx,0B8h: synchronous load
#define SL_MEMSET_SYNC			0x1FFEC1	// push 0B8h
#define SL_REQUEST_WAIT			0x00A9E0	// fastcall(request): waits for the read; idempotent
#define SL_NAME_LOOKUP			0x20AF90	// fastcall(table, name): the file's name record, or 0
#define SL_NAME_TABLE			0x48CB20
#define SL_HEADER_POOL			0x48DB20
#define SL_HEADER_GET			0x1FE990	// eax=desc, edi=name buffer, push arg; ret 4: the file's cached header
#define SL_HEADER_GET_CALL1		0x20025A	// in 0x6001F0
#define SL_HEADER_GET_CALL2		0x200552	// in 0x6004E0
#define SL_POOL_FLUSH			0x2093E0	// fastcall(pool): evicts every record
#define SL_FLUSH_CALL1			0x1FFD9D	// header pool flush in the cache re-init 0x5FFD40
#define SL_FLUSH_CALL2			0x1FFE69	// header pool flush in the cache teardown 0x5FFE30
#define SL_SYNC_LOAD			0x1FFE90	// esi=desc, push hdr and 3 more; ret 10h: reads with the raw header and waits for it
#define SL_SYNC_LOAD_CALL1		0x2001DD	// in 0x6000B0
#define SL_SYNC_LOAD_CALL2		0x200488	// in 0x6001F0
#define SL_POOL_EVICT			0x2092D0	// fastcall(pool, record): evicts the record, or the LRU head when 0
#define SL_EVICT_CALL			0x209465	// in the allocator 0x609420's retry loop
#ifdef PLUGY_TEST_HOOKS
#define SL_HEADER_CAP_PUSH1		0x0573FE	// push 7D000h: the header pool's byte budget at each pool init
#define SL_HEADER_CAP_PUSH2		0x0F8F3A
#define SL_HEADER_CAP_PUSH3		0x0F9086
#define SL_HEADER_CAP_PUSH4		0x0F9112
#define SL_HEADER_CAP			0x7D000
#define SL_STORM_READ			0x017040	// the loader's archive read: ecx=file, eax=offset, push buf, size; ret 8
#define SL_STORM_READ_CALL		0x01A6A9
#endif

// Sprite object S (B = S+0x20): +0xC the 5 file name tokens, B+0 flags (bit0 no graphics, bit1 asynchronous, bit2
// pending), +0x28 finalized, +0xB0 the read request. The tail past vanilla's 0xB8 bytes is PlugY's, zeroed by the
// game's own memset of the bumped size. The sprite cache charges its 0xD0 bytes to its budget (0x5FF3F0), little next
// to the decoded frames charged to the same record.
#define VANILLA_SPRITE_OBJECT_SIZE	0xB8
#define SPRITE_OBJECT_SIZE		0x188
#define S_TOKENS				0x0C
#define S_FLAGS					0x20
#define S_REQUEST				0xB0
#define S_COPY					0xB8	// the header copy the load decodes with
#define S_STATE					0x178	// LONG: SL_STATE_*
#define S_COOKIE				0x17C	// SL_COOKIE_*, written at issue; anything else is an object PlugY did not issue
#define S_RAN					0x180	// LONG: 1 once the completion callback ran
C_ASSERT(S_COPY + DCC_HEADER_SIZE == S_STATE);
C_ASSERT(S_RAN + 8 == SPRITE_OBJECT_SIZE);

#define SL_STATE_SKIPPED		1
#define SL_STATE_DECODE_FAILED	2
#define SL_COOKIE_CHECKED		0x31444C53	// 'SLD1': issued with a checked copy
#define SL_COOKIE_PASSTHROUGH	0x50444C53	// 'SLDP': issued with its copy after the reject cap

#define SL_FLAG_NO_GRAPHICS		1
#define SL_FLAG_PENDING			4

// Header pool fields; a record is an HcRecord.
#define POOL_CAP				0x04
#define POOL_USED				0x08
#define POOL_COUNT				0x0C
#define POOL_RECORDS			0x10
#define POOL_MAX				0x14
#define POOL_HEAD				0x18
#define POOL_TAIL				0x1C
#define POOL_FREE_HEAD			0x20
#define POOL_FREE_TAIL			0x24
#define POOL_EVICT_CALLBACK		0x28
#define POOL_FIELD(pool, field)	(*(volatile DWORD*)((pool) + (field)))

#define SL_THREAD_SLOTS			16
#define SL_CONTENTIONS_LOGGED	20
#define SL_STACK_FRAMES			10

typedef DWORD (STDCALL *t_loadDone)(BYTE* buf, DWORD size, DWORD* cb);
typedef DWORD (FASTCALL *t_requestWait)(void* request);
typedef BYTE* (FASTCALL *t_nameLookup)(void* table, const char* name);
typedef void (FASTCALL *t_poolFlush)(BYTE* pool);
typedef void (FASTCALL *t_poolEvict)(BYTE* pool, BYTE* rec);
typedef void (FASTCALL *t_poolRelease)(HcRecord* rec);

#define SL_CALL(type, offset) ((type)(offset_D2Game + (offset)))
#define SL_HEADER_POOL_PTR ((BYTE*)(offset_D2Game + SL_HEADER_POOL))

#ifdef PLUGY_TEST_HOOKS
int testSpriteFix = 1;
int testSpriteGuard = 1;
int testHeaderCacheLock = 1;
DWORD testSpriteLoadDelayMs = 0;
DWORD testHeaderCacheBytes = 0;
DWORD testSpriteInjectEvery = 0;
DWORD testSpriteIssueInjectEvery = 0;
DWORD testSpriteDecodeFailEvery = 0;
DWORD testSpriteSkipCallbackEvery = 0;
#define SL_FIX_ON				(testSpriteFix != 0)
#define SL_GUARD_ON				(testSpriteGuard != 0)
#define SL_LOCK_ON				(testHeaderCacheLock != 0)
#else
#define SL_FIX_ON				true
#define SL_GUARD_ON				true
#define SL_LOCK_ON				true
#endif

// Jump and call targets of the stubs, set at install. File-scope statics only: the DLL builds with
// /Zc:threadSafeInit-, so a function-local static with a dynamic initializer is unsafe across threads.
static DWORD sl_issuer;
static DWORD sl_finalizer;
static DWORD sl_finalizeExit;
static DWORD sl_headerGet;
static DWORD sl_syncLoad;
#ifdef PLUGY_TEST_HOOKS
static DWORD sl_stormRead;
#endif

static bool sharedInitDone = false;
static bool sharedInitOk = false;
static bool headerCacheLockInstalled = false;
static DWORD pinError = 0;
static DWORD mainThreadId = 0;

// hdrLock is held for every call a sprite load makes into the header cache: both getter calls, the issue check, each
// synchronous load (its read is in flight until it returns), the allocator's evictions with their repair and the two
// pool flushes. Vanilla still uses the getter's header unlocked for a short stretch after it returns; the getter has
// just moved that header to the LRU's tail (0x609380), and an eviction takes the head. The cache re-init (0x5FFD40)
// and teardown (0x5FFE30) clear the name table and the pool around their flush without the lock, as in vanilla.
// Lock order is hdrLock, then ringLock or the log lock, never the reverse; pairLock is taken alone.
static CRITICAL_SECTION hdrLock;
static CRITICAL_SECTION pairLock;
static CRITICAL_SECTION ringLock;
static volatile DWORD hdrOwner = 0;
static LONG hdrDepth = 0;					// changed only by the thread holding hdrLock

static volatile LONG threadIds[SL_THREAD_SLOTS] = {};
static SpritePairTable pairs = {};

static volatile LONG cntIssues, cntCopies, cntRejects, cntStale, cntMismatch, cntUnknown, cntBadCopy;
static volatile LONG cntPassthrough, cntGuard, cntNoGraphics, cntDecodeFail, cntNoCallback;
static volatile LONG cntRepairs, cntRepairsSkipped, cntContentions, cntThreads, cntThreadsOverflow;
#ifdef PLUGY_TEST_HOOKS
static volatile LONG cntInjectedRejects, cntInjected, cntInjectSkipped, cntIssueInjected, cntDelayed, cntSyncLoads;
#endif

static bool initShared()
{
	if (sharedInitDone)
		return sharedInitOk;
	sharedInitDone = true;
	// Game.exe frees PlugY.dll before it exits while the game still calls the flush wrapper at shutdown.
	HMODULE module;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&Install_SpriteLoad, &module))
	{
		pinError = GetLastError();
		return false;
	}
	InitializeCriticalSection(&hdrLock);
	InitializeCriticalSection(&pairLock);
	InitializeCriticalSection(&ringLock);
	mainThreadId = GetCurrentThreadId();
	sharedInitOk = true;
	return true;
}

/*================================= HEADER CACHE LOCK =================================*/

// "module+offset" of each frame, or "?+address" outside any module.
static void formatStack(void** frames, USHORT count, char* text, size_t size)
{
	size_t len = 0;
	text[0] = '\0';
	for (USHORT i = 0; i < count; i++)
	{
		const char* base = "?";
		DWORD offset = (DWORD)frames[i];
		char path[MAX_PATH];
		HMODULE module = NULL;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)frames[i], &module) && GetModuleFileNameA(module, path, MAX_PATH))
		{
			path[MAX_PATH - 1] = '\0';
			const char* slash = strrchr(path, '\\');
			base = slash ? slash + 1 : path;
			offset -= (DWORD)module;
		}
		int n = _snprintf(text + len, size - len, "%s%s+%X", len ? " " : "", base, offset);
		if (n < 0 || (size_t)n >= size - len)
		{
			text[size - 1] = '\0';
			return;
		}
		len += n;
	}
}

// Each thread is logged once, with the stack that brought it to the cache; a full table only counts.
static void noteThread(DWORD self)
{
	for (int i = 0; i < SL_THREAD_SLOTS; i++)
	{
		DWORD id = (DWORD)threadIds[i];
		if (id == self)
			return;
		if (id)
			continue;
		LONG previous = InterlockedCompareExchange(&threadIds[i], (LONG)self, 0);
		if ((DWORD)previous == self)
			return;
		if (previous)
			continue;
		InterlockedIncrement(&cntThreads);
		void* frames[SL_STACK_FRAMES];
		USHORT count = RtlCaptureStackBackTrace(1, SL_STACK_FRAMES, frames, NULL);
		char stack[SL_STACK_FRAMES * 48];
		formatStack(frames, count, stack, sizeof(stack));
		log_msg("HeaderCache: thread %u entered the header cache (main thread %u), stack %s\n", self, mainThreadId, stack);
		return;
	}
	InterlockedIncrement(&cntThreadsOverflow);
}

#ifdef PLUGY_TEST_HOOKS
static volatile LONG budgetLogged = 0;
static volatile LONG healthTick = 0;

// The pool's budget is set at each pool init; this covers an init that ran before the push patches.
static void testApplyBudget()
{
	if (!testHeaderCacheBytes)
		return;
	BYTE* pool = SL_HEADER_POOL_PTR;
	DWORD was = POOL_FIELD(pool, POOL_CAP);
	if (was == testHeaderCacheBytes)
		return;
	POOL_FIELD(pool, POOL_CAP) = testHeaderCacheBytes;
	if (!InterlockedExchange(&budgetLogged, 1))
		log_msg("SpriteLoad TEST: header cache budget %u bytes (was %u)\n", testHeaderCacheBytes, was);
}

// Records reachable from the LRU head, each pointer checked to be a record of the array: racy without the lock,
// so it is bounded and never follows a pointer outside the array.
static DWORD countLinked(BYTE* pool, DWORD count)
{
	DWORD records = POOL_FIELD(pool, POOL_RECORDS);
	DWORD end = records + POOL_FIELD(pool, POOL_MAX) * sizeof(HcRecord);
	DWORD linked = 0;
	DWORD p = POOL_FIELD(pool, POOL_HEAD);
	for (DWORD step = 0; p && step <= count; step++)
	{
		if (!records || p < records || p >= end || (p - records) % sizeof(HcRecord))
			break;
		linked++;
		p = ((volatile HcRecord*)p)->next;
	}
	return linked;
}

static void testHealth()
{
	DWORD now = GetTickCount();
	LONG last = healthTick;
	if (now - (DWORD)last < 60000)
		return;
	if (InterlockedCompareExchange(&healthTick, (LONG)now, last) != last)
		return;
	BYTE* pool = SL_HEADER_POOL_PTR;
	DWORD count = POOL_FIELD(pool, POOL_COUNT);
	DWORD linked = countLinked(pool, count);
	log_msg("SpriteLoad TEST health: issues %u copies %u rejects %u stale %u mismatch %u unknown %u badcopy %u"
		" injectedrejects %u passthrough %u pairrecycled %u guard %u nographics %u decodefail %u nocallback %u injected %u"
		" injectskipped %u issueinjected %u delayed %u syncloads %u repairs %u contentions %u threads %u threadsoverflow %u"
		" headers count %u linked %u used %u cap %u\n",
		cntIssues, cntCopies, cntRejects, cntStale, cntMismatch, cntUnknown, cntBadCopy,
		cntInjectedRejects, cntPassthrough, pairs.recycled, cntGuard, cntNoGraphics, cntDecodeFail, cntNoCallback, cntInjected,
		cntInjectSkipped, cntIssueInjected, cntDelayed, cntSyncLoads, cntRepairs, cntContentions, cntThreads, cntThreadsOverflow,
		count, linked, POOL_FIELD(pool, POOL_USED), POOL_FIELD(pool, POOL_CAP));
}
#endif

// No arguments and only EAX/ECX/EDX clobbered, so the naked stubs call them around game code. Enter locks exactly
// when SL_LOCK_ON, a value fixed before any install, so Leave unlocks exactly what Enter locked.
void STDCALL headerCacheEnter()
{
	DWORD self = GetCurrentThreadId();
	noteThread(self);
	if (SL_LOCK_ON)
	{
		if (!TryEnterCriticalSection(&hdrLock))
		{
			LONG n = InterlockedIncrement(&cntContentions);
			if (n <= SL_CONTENTIONS_LOGGED)
				log_msg("HeaderCache: thread %u waited for thread %u (contention %u)\n", self, hdrOwner, n);
			EnterCriticalSection(&hdrLock);
		}
		hdrDepth++;
		hdrOwner = self;
	}
#ifdef PLUGY_TEST_HOOKS
	testApplyBudget();
#endif
}

void STDCALL headerCacheLeave()
{
#ifdef PLUGY_TEST_HOOKS
	testHealth();
#endif
	if (!SL_LOCK_ON)
		return;
	if (--hdrDepth == 0)
		hdrOwner = 0;
	LeaveCriticalSection(&hdrLock);
}

FCT_ASM ( caller_headerGetLocked )
	PUSH EAX
	PUSH ECX
	PUSH EDX
	CALL headerCacheEnter
	POP EDX
	POP ECX
	POP EAX
	PUSH DWORD PTR SS:[ESP+0x4]
	CALL sl_headerGet
	PUSH EAX
	PUSH ECX
	PUSH EDX
	CALL headerCacheLeave
	POP EDX
	POP ECX
	POP EAX
	RETN 4
}}

// Replaces the two header pool flushes (cache re-init and teardown); callers keep only ESI/EDI across the call.
static void FASTCALL headerFlushLocked(BYTE* pool)
{
	headerCacheEnter();
	SL_CALL(t_poolFlush, SL_POOL_FLUSH)(pool);
	headerCacheLeave();
}

void STDCALL syncLoadEnter()
{
#ifdef PLUGY_TEST_HOOKS
	InterlockedIncrement(&cntSyncLoads);
#endif
	headerCacheEnter();
}

// The synchronous load (0x5FFE90) sends its read with the raw cached header and then waits for it (0x5FFF7C) unless
// that header is flagged no graphics by then: the lock keeps another thread from evicting the header while it waits.
// Its four arguments are re-pushed; ESI passes through.
FCT_ASM ( caller_syncLoadLocked )
	PUSH EAX
	PUSH ECX
	PUSH EDX
	CALL syncLoadEnter
	POP EDX
	POP ECX
	POP EAX
	PUSH DWORD PTR SS:[ESP+0x10]
	PUSH DWORD PTR SS:[ESP+0x10]
	PUSH DWORD PTR SS:[ESP+0x10]
	PUSH DWORD PTR SS:[ESP+0x10]
	CALL sl_syncLoad
	PUSH EAX
	PUSH ECX
	PUSH EDX
	CALL headerCacheLeave
	POP EDX
	POP ECX
	POP EAX
	RETN 0x10
}}

// The pool's eviction callback, as its own eviction calls it: it clears the name record's link and frees it.
static void releaseOwner(HcRecord* rec, void* ctx)
{
	t_poolRelease release = (t_poolRelease)POOL_FIELD((BYTE*)ctx, POOL_EVICT_CALLBACK);
	if (release)
		release(rec);
}

static void repairHeaderLru(BYTE* pool, const char* why)
{
	HcRecord* records = (HcRecord*)POOL_FIELD(pool, POOL_RECORDS);
	if (!records)
	{
		// Bounded: the allocator calls again on every turn of its loop.
		LONG n = InterlockedIncrement(&cntRepairsSkipped);
		if (spriteLogThis(n))
			log_msg("HeaderCache: LRU not repaired (%s): the pool has no record array, %u total\n", why, n);
		return;
	}
	DWORD oldCount = POOL_FIELD(pool, POOL_COUNT);
	HcLists lists;
	hcRelink(records, POOL_FIELD(pool, POOL_MAX), &lists, releaseOwner, pool);
	POOL_FIELD(pool, POOL_HEAD) = lists.head;
	POOL_FIELD(pool, POOL_TAIL) = lists.tail;
	POOL_FIELD(pool, POOL_FREE_HEAD) = lists.freeHead;
	POOL_FIELD(pool, POOL_FREE_TAIL) = lists.freeTail;
	POOL_FIELD(pool, POOL_COUNT) = lists.count;
	POOL_FIELD(pool, POOL_USED) = lists.used;
	LONG n = InterlockedIncrement(&cntRepairs);
	if (spriteLogThis(n))
		log_msg("HeaderCache: LRU repaired (%s): %u live records relinked, count %u -> %u, used %u, repair %u\n",
			why, lists.count, oldCount, lists.count, lists.used, n);
}

// Replaces the eviction call in the allocator's retry loop, which never ends once the LRU head is gone (the
// eviction then frees nothing) or the count is 0 (it returns at once). The eviction runs under the lock with its
// repair; the header pool's allocator is only reached from the locked getter anyway. Other pools pass straight through.
static void FASTCALL headerEvictOrRepair(BYTE* pool, BYTE* rec)
{
	if (pool != SL_HEADER_POOL_PTR || rec)
	{
		SL_CALL(t_poolEvict, SL_POOL_EVICT)(pool, rec);
		return;
	}
	headerCacheEnter();
	DWORD head = POOL_FIELD(pool, POOL_HEAD);
	const char* why = !head ? "empty-head" : !POOL_FIELD(pool, POOL_COUNT) ? "zero-count"
		: !((HcRecord*)head)->data ? "freed-head" : NULL;
	if (why)
		repairHeaderLru(pool, why);
	SL_CALL(t_poolEvict, SL_POOL_EVICT)(pool, rec);
	headerCacheLeave();
}

/*================================= TEST HEADER RING =================================*/

#ifdef PLUGY_TEST_HOOKS
#define SL_RING_SIZE 16
// Copies of headers that matched their file, used to inject a foreign header into a load.
static BYTE ring[SL_RING_SIZE][DCC_HEADER_SIZE];
static DWORD ringUsed = 0;
static DWORD ringNext = 0;

static bool everyKth(volatile LONG* tick, DWORD k)
{
	return k && (DWORD)InterlockedIncrement(tick) % k == 0;
}

static void ringPush(const BYTE* copy)
{
	EnterCriticalSection(&ringLock);
	memcpy(ring[ringNext], copy, DCC_HEADER_SIZE);
	ringNext = (ringNext + 1) % SL_RING_SIZE;
	if (ringUsed < SL_RING_SIZE)
		ringUsed++;
	LeaveCriticalSection(&ringLock);
}

// A ring header with another name; at completion also one the structural checks catch: another span over the
// directions read, or fewer directions than the read ends at.
static bool ringInject(BYTE* copy, bool atCompletion, DWORD start, DWORD count)
{
	char name[DCC_COPY_NAME_SIZE];
	dccCopyName(copy, name);
	DWORD end = start + count;
	if (atCompletion && (start > DCC_MAX_DIRS || count > DCC_MAX_DIRS || end > DCC_MAX_DIRS))
		return false;
	bool found = false;
	EnterCriticalSection(&ringLock);
	for (DWORD i = 0; i < ringUsed && !found; i++)
	{
		const BYTE* other = ring[i];
		char otherName[DCC_COPY_NAME_SIZE];
		dccCopyName(other, otherName);
		if (!strcmp(otherName, name))
			continue;
		if (atCompletion && DCC_HDR_DIRS(other) >= end
			&& DCC_HDR_OFF(other, end) - DCC_HDR_OFF(other, start) == DCC_HDR_OFF(copy, end) - DCC_HDR_OFF(copy, start))
			continue;
		memcpy(copy, other, DCC_HEADER_SIZE);
		found = true;
	}
	LeaveCriticalSection(&ringLock);
	return found;
}

static volatile LONG issueInjectTick = 0;
static volatile LONG injectTick = 0;
static volatile LONG decodeFailTick = 0;
static volatile LONG skipCallbackTick = 0;
#endif

/*================================= ISSUE =================================*/

enum SpriteIssueVerdict { SIV_OK, SIV_MISMATCH, SIV_UNKNOWN, SIV_BADCOPY, SIV_INJECTED };
static const char* const issueVerdictNames[] = { "ok", "mismatch", "unknown", "badcopy", "injected" };

// Called in place of the issuer (0x5FFA20) with its arguments; returns the header the load goes out with, or NULL to
// fail the issue as the issuer itself does, so the caller evicts the sprite and the next frame retries it.
BYTE* STDCALL spriteIssue(BYTE* S, BYTE* hdr, BYTE* desc, DWORD d, DWORD count)
{
	if (!SL_FIX_ON)
		return hdr;
	// The issuer halts on every other type before any read is issued.
	DWORD type = *(DWORD*)(desc + 8);
	if (type > 1)
		return hdr;
	InterlockedIncrement(&cntIssues);

	BYTE* copy = S + S_COPY;
	char expected[DCC_NAME_SIZE];
	dccTokenName(S + S_TOKENS, expected);
	bool injected = false;
	headerCacheEnter();
	BYTE* nameRec = SL_CALL(t_nameLookup, SL_NAME_LOOKUP)((void*)(offset_D2Game + SL_NAME_TABLE), expected);
	// Only a header the cache still holds is read: one evicted since the getter returned it may already be freed
	// (0x6092D0). When the file's own record does not hold it, any record may: the name check then judges it, so a
	// lookup that misses for some file cannot make all its loads stale.
	BYTE* pool = SL_HEADER_POOL_PTR;
	bool live = dccHeaderIsLive(nameRec, hdr)
		|| hcHoldsData((const HcRecord*)POOL_FIELD(pool, POOL_RECORDS), POOL_FIELD(pool, POOL_MAX), hdr);
	if (live)
		memcpy(copy, hdr, DCC_HEADER_SIZE);
#ifdef PLUGY_TEST_HOOKS
	if (live && everyKth(&issueInjectTick, testSpriteIssueInjectEvery) && ringInject(copy, false, 0, 0))
	{
		injected = true;
		InterlockedIncrement(&cntIssueInjected);
	}
#endif
	headerCacheLeave();
	if (!live)
	{
		// Without guard checks (test builds) it goes out as vanilla sends it.
		if (!SL_GUARD_ON)
			return hdr;
		// Never let through: the header is gone, and the getter returns one the cache holds on the next frame.
		LONG total = InterlockedIncrement(&cntRejects);
		LONG own = InterlockedIncrement(&cntStale);
		if (spriteLogThis(total))
			log_msg("SpriteLoad: issue rejected (stale): file %s, its header no longer cached, retried on the next frame,"
				" %u total, %u stale\n", expected, total, own);
		return NULL;
	}
	char copyName[DCC_COPY_NAME_SIZE];
	dccCopyName(copy, copyName);
	char shownName[DCC_COPY_NAME_SIZE];
	dccPrintableName(copyName, shownName);
#ifdef PLUGY_TEST_HOOKS
	if (injected)
		log_msg("SpriteLoad TEST: injected header %s into file %s (issue)\n", shownName, expected);
#endif

	SpriteIssueVerdict verdict = SIV_OK;
	DccIssueVerdict shape = DCC_ISSUE_OK;
	if (SL_GUARD_ON)
	{
		DccNameVerdict name;
		if (injected)
			verdict = SIV_INJECTED;
		else if ((name = dccHeaderNameMatches(copy, S + S_TOKENS)) != DCC_NAME_MATCH)
			verdict = name == DCC_NAME_MISMATCH ? SIV_MISMATCH : SIV_UNKNOWN;
		else if ((shape = dccIssueCopyOk(copy, type, d, count)) == DCC_ISSUE_BAD_COPY)
			verdict = SIV_BADCOPY;
	}

	if (verdict == SIV_OK)
	{
		// A VANILLA shape goes on to the issuer, which refuses it by itself: no streak ends there.
		if (shape == DCC_ISSUE_OK)
		{
			EnterCriticalSection(&pairLock);
			spritePairReset(&pairs, expected);
			LeaveCriticalSection(&pairLock);
		}
		*(volatile DWORD*)(S + S_COOKIE) = SL_COOKIE_CHECKED;
#ifdef PLUGY_TEST_HOOKS
		if (!injected && dccHeaderNameMatches(copy, S + S_TOKENS) == DCC_NAME_MATCH)
			ringPush(copy);
#endif
		InterlockedIncrement(&cntCopies);
		return copy;
	}

	const char* word = issueVerdictNames[verdict];
#ifdef PLUGY_TEST_HOOKS
	if (verdict == SIV_INJECTED)
	{
		// Never counted in the pair table, so injected pairs never reach the cap.
		LONG total = InterlockedIncrement(&cntRejects);
		LONG own = InterlockedIncrement(&cntInjectedRejects);
		log_msg("SpriteLoad: issue rejected (%s): file %s, header %s, reject %u of %u for this pair, %u total, %u %s\n",
			word, expected, shownName, 0, SPRITE_REJECT_CAP, total, own, word);
		return NULL;
	}
#endif

	EnterCriticalSection(&pairLock);
	DWORD recycledBefore = pairs.recycled;
	bool isNew;
	DWORD n = spritePairCount(&pairs, expected, copyName, &isNew);
	DWORD recycled = pairs.recycled != recycledBefore ? pairs.recycled : 0;
	LeaveCriticalSection(&pairLock);
	if (recycled && spriteLogThis((LONG)recycled))
		log_msg("SpriteLoad: reject table full: file %s took the slot of the pair counted longest ago, %u total\n",
			expected, recycled);
	if (!spritePastCap(n))
	{
		LONG total = InterlockedIncrement(&cntRejects);
		volatile LONG* counter = verdict == SIV_MISMATCH ? &cntMismatch : verdict == SIV_UNKNOWN ? &cntUnknown : &cntBadCopy;
		LONG own = InterlockedIncrement(counter);
		if (isNew || spriteLogThis(total))
			log_msg("SpriteLoad: issue rejected (%s): file %s, header %s, reject %u of %u for this pair, %u total, %u %s\n",
				word, expected, shownName, n, SPRITE_REJECT_CAP, total, own, word);
		return NULL;
	}
	// A rule that is wrong for a file must not keep its sprite blank for good. The load goes out with its copy, whose
	// name the completion no longer checks.
	LONG total = InterlockedIncrement(&cntPassthrough);
	if (n == SPRITE_REJECT_CAP + 1 || spriteLogThis(total))
		log_msg("SpriteLoad: issue passed through after %u rejects in a row: file %s, header %s, %u total\n",
			SPRITE_REJECT_CAP, expected, shownName, total);
	*(volatile DWORD*)(S + S_COOKIE) = SL_COOKIE_PASSTHROUGH;
	return copy;
}

// The issuer's arguments are re-pushed for spriteIssue (count, d, desc, hdr, S); a non-NULL result replaces the hdr
// argument, NULL returns the issuer's own failure (-1, ret 0Ch).
FCT_ASM ( caller_spriteIssue )
	PUSH EAX
	PUSH ECX
	PUSH EDX
	PUSH EDX
	PUSH DWORD PTR SS:[ESP+0x18]
	PUSH EAX
	PUSH DWORD PTR SS:[ESP+0x1C]
	PUSH ESI
	CALL spriteIssue
	TEST EAX,EAX
	JE rejectIssue
	MOV DWORD PTR SS:[ESP+0x10],EAX
	POP EDX
	POP ECX
	POP EAX
	JMP sl_issuer
rejectIssue:
	POP EDX
	POP ECX
	POP EAX
	OR EAX,-1
	RETN 0x0C
}}

/*================================= COMPLETION =================================*/

// A header's name as logged; the raw dccCopyName form stays the one compared and stored.
static void printableHeaderName(const BYTE* hdr, char out[DCC_COPY_NAME_SIZE])
{
	char raw[DCC_COPY_NAME_SIZE];
	dccCopyName(hdr, raw);
	dccPrintableName(raw, out);
}

static void spanOf(const BYTE* hdr, DWORD start, DWORD count, DWORD* span)
{
	DWORD end = start + count;
	*span = start <= DCC_MAX_DIRS && count <= DCC_MAX_DIRS && end <= DCC_MAX_DIRS
		? DCC_HDR_OFF(hdr, end) - DCC_HDR_OFF(hdr, start) : 0;
}

// The loader thread's completion callback (reached by "call eax", stdcall, ret 0Ch), with Storm's lock released. It
// never takes the cache lock, frees or touches the header cache: a synchronous load holds that lock while it waits
// for its own read, which this thread may serve after this one. The only locks PlugY takes here are the log's (and the
// test ring's in test builds). A sprite PlugY did not issue goes to vanilla unchanged.
static DWORD STDCALL spriteLoadDone(BYTE* buf, DWORD size, DWORD* cb)
{
	t_loadDone vanilla = SL_CALL(t_loadDone, SL_LOAD_DONE);
	BYTE* B = (BYTE*)cb[2];
	BYTE* S = B - S_FLAGS;
	BYTE* copy = S + S_COPY;
	DWORD cookie = *(volatile DWORD*)(S + S_COOKIE);
	if ((cookie != SL_COOKIE_CHECKED && cookie != SL_COOKIE_PASSTHROUGH) || cb[1] != (DWORD)copy)
	{
#ifdef PLUGY_TEST_HOOKS
		if (!testSpriteFix)
		{
			// Read once, so the logged name is the one compared.
			BYTE seen[DCC_HEADER_SIZE];
			memcpy(seen, (BYTE*)cb[1], DCC_HEADER_SIZE);
			DccNameVerdict v = dccHeaderNameMatches(seen, S + S_TOKENS);
			if (v != DCC_NAME_MATCH)
			{
				char file[DCC_NAME_SIZE];
				char header[DCC_COPY_NAME_SIZE];
				dccTokenName(S + S_TOKENS, file);
				printableHeaderName(seen, header);
				log_msg("SpriteLoad TEST: fix off, header %s for file %s (%s)\n", header, file,
					v == DCC_NAME_MISMATCH ? "mismatch" : "unknown");
			}
		}
#endif
		return vanilla(buf, size, cb);
	}

	char file[DCC_NAME_SIZE];
	dccTokenName(S + S_TOKENS, file);
#ifdef PLUGY_TEST_HOOKS
	if (everyKth(&skipCallbackTick, testSpriteSkipCallbackEvery))
	{
		log_msg("SpriteLoad TEST: callback skipped for file %s\n", file);
		return 1;
	}
#endif
	InterlockedExchange((volatile LONG*)(S + S_RAN), 1);
#ifdef PLUGY_TEST_HOOKS
	if (everyKth(&decodeFailTick, testSpriteDecodeFailEvery))
	{
		InterlockedExchange((volatile LONG*)(S + S_STATE), SL_STATE_DECODE_FAILED);
		_InterlockedAnd8((volatile char*)B, (char)~SL_FLAG_PENDING);
		log_msg("SpriteLoad TEST: decode failure forced for file %s\n", file);
		return 1;
	}
	if (everyKth(&injectTick, testSpriteInjectEvery))
	{
		if (ringInject(copy, true, cb[3], cb[4]))
		{
			InterlockedIncrement(&cntInjected);
			char header[DCC_COPY_NAME_SIZE];
			printableHeaderName(copy, header);
			log_msg("SpriteLoad TEST: injected header %s into file %s (completion)\n", header, file);
		}
		else
			InterlockedIncrement(&cntInjectSkipped);
	}
#endif

	if (SL_GUARD_ON)
	{
		const char* reason = NULL;
		switch (dccLoadMatches(copy, B, cb[3], cb[4], size))
		{
		case DCC_LOAD_BAD_RANGE:	reason = "range"; break;
		case DCC_LOAD_BAD_LAYOUT:	reason = "layout"; break;
		case DCC_LOAD_BAD_SPAN:		reason = "span"; break;
		case DCC_LOAD_BAD_SIZE:		reason = "size"; break;
		default:
			// A pass-through after the reject cap skips the name check, or the sprite would stay blank for good.
			if (cookie == SL_COOKIE_CHECKED)
			{
				DccNameVerdict v = dccHeaderNameMatches(copy, S + S_TOKENS);
				if (v != DCC_NAME_MATCH)
					reason = v == DCC_NAME_MISMATCH ? "name-mismatch" : "name-unknown";
			}
		}
		if (reason)
		{
			// Nothing was decoded or added to the sprite cache; the finalize hook turns this into "no graphics".
			InterlockedExchange((volatile LONG*)(S + S_STATE), SL_STATE_SKIPPED);
			_InterlockedAnd8((volatile char*)B, (char)~SL_FLAG_PENDING);
			LONG total = InterlockedIncrement(&cntGuard);
			if (spriteLogThis(total))
			{
				char header[DCC_COPY_NAME_SIZE];
				DWORD span;
				printableHeaderName(copy, header);
				spanOf(copy, cb[3], cb[4], &span);
				log_msg("SpriteLoad: decode skipped (%s): file %s, header %s, dirs %u frames %u start %u count %u span %u size %u, %u total\n",
					reason, file, header, DCC_HDR_DIRS(copy), DCC_HDR_FRAMES(copy), cb[3], cb[4], span, size, total);
			}
			return 1;
		}
	}

	DWORD result = vanilla(buf, size, cb);
	// The decoder returned nothing: vanilla flagged only the copy, and the finalize hook ends the load.
	if (!cb[5])
		InterlockedExchange((volatile LONG*)(S + S_STATE), SL_STATE_DECODE_FAILED);
	return result;
}

/*================================= FINALIZE =================================*/

// 0: not a sprite PlugY issued, vanilla as is. 1: no graphics (the stub leaves 0x6001F0 as when no sprite loads).
// 2: vanilla, with the copy as its header argument, so its decode-failure test reads the copy rather than a cache
// slot another file may hold by now.
DWORD STDCALL spriteFinalize(BYTE* S)
{
	if (!SL_FIX_ON)
		return 0;
	DWORD cookie = *(volatile DWORD*)(S + S_COOKIE);
	if (cookie != SL_COOKIE_CHECKED && cookie != SL_COOKIE_PASSTHROUGH)
		return 0;
	SL_CALL(t_requestWait, SL_REQUEST_WAIT)(*(void**)(S + S_REQUEST));
	LONG state = *(volatile LONG*)(S + S_STATE);
	LONG ran = *(volatile LONG*)(S + S_RAN);
	// The issuer sets up a completion callback only for a header with bit1; without one vanilla decodes here.
	bool expected = (S[S_COPY] & 2) != 0;
	if (!state && (ran || !expected))
		return 2;

	S[S_FLAGS] |= SL_FLAG_NO_GRAPHICS;
	const char* reason;
	if (state == SL_STATE_SKIPPED)
		reason = "skipped";
	else if (state == SL_STATE_DECODE_FAILED)
	{
		reason = "decode failed";
		InterlockedIncrement(&cntDecodeFail);
	}
	else
	{
		reason = "no callback";
		InterlockedIncrement(&cntNoCallback);
	}
	LONG total = InterlockedIncrement(&cntNoGraphics);
	if (spriteLogThis(total))
	{
		char file[DCC_NAME_SIZE];
		dccTokenName(S + S_TOKENS, file);
		log_msg("SpriteLoad: no graphics for file %s (%s), %u total\n", file, reason, total);
	}
	return 1;
}

// At the finalize call: [esp] return 0x6003A7, [esp+4] hdr, [esp+8] desc, EDI = S. The no-graphics exit drops the
// return address and both arguments and leaves through 0x6001F0's own "no sprite" epilogue.
FCT_ASM ( caller_spriteFinalize )
	PUSH EDI
	CALL spriteFinalize
	CMP EAX,1
	JE noGraphics
	TEST EAX,EAX
	JE vanillaFinalize
	LEA EAX,[EDI+S_COPY]
	MOV DWORD PTR SS:[ESP+0x4],EAX
vanillaFinalize:
	JMP sl_finalizer
noGraphics:
	ADD ESP,0x0C
	JMP sl_finalizeExit
}}

/*================================= TEST READ DELAY =================================*/

#ifdef PLUGY_TEST_HOOKS
// STDCALL: the DLL's default is cdecl, which would leave the argument for POPAD to restore shifted registers from.
void STDCALL spriteReadDelay(BYTE* req)
{
	// The request's descriptor is set on its final chunk only.
	DWORD* desc = *(DWORD**)(req + 0x40);
	if (desc && desc[0] == (DWORD)spriteLoadDone)
	{
		Sleep(testSpriteLoadDelayMs);
		InterlockedIncrement(&cntDelayed);
	}
}

FCT_ASM ( caller_spriteReadDelay )
	PUSHAD
	PUSH ESI
	CALL spriteReadDelay
	POPAD
	JMP sl_stormRead
}}
#endif

/*================================= INSTALL =================================*/

static bool hasOperand(DWORD offset, BYTE opcode, DWORD operand)
{
	BYTE* p = (BYTE*)(offset_D2Game + offset);
	return p[0] == opcode && *(DWORD*)(p + 1) == operand;
}

static bool callsTo(DWORD offset, DWORD target)
{
	DWORD site = offset_D2Game + offset;
	return *(BYTE*)site == 0xE8 && site + 5 + *(DWORD*)(site + 1) == offset_D2Game + target;
}

// The first site of the SpriteLoad patches that differs from 1.14d, as an address, or 0.
static DWORD changedSpriteLoadSite()
{
	static const DWORD sizeSites[] = { SL_ALLOC_SIZE_ASYNC, SL_MEMSET_ASYNC, SL_ALLOC_SIZE_SYNC, SL_MEMSET_SYNC };
	static const BYTE sizeOpcodes[] = { 0xBA, 0x68, 0xBA, 0x68 };
	for (int i = 0; i < 4; i++)
		if (!hasOperand(sizeSites[i], sizeOpcodes[i], VANILLA_SPRITE_OBJECT_SIZE))
			return offset_D2Game + sizeSites[i];
	BYTE* mov = (BYTE*)(offset_D2Game + SL_LOAD_DONE_MOV);
	if (mov[0] != 0xC7 || mov[1] != 0x45 || mov[2] != 0xD0 || *(DWORD*)(mov + 3) != offset_D2Game + SL_LOAD_DONE)
		return offset_D2Game + SL_LOAD_DONE_MOV;
	if (!callsTo(SL_FINALIZE_CALL, SL_FINALIZER))
		return offset_D2Game + SL_FINALIZE_CALL;
	static const BYTE exitBytes[] = { 0x5F, 0x5E, 0x33, 0xC0, 0x5B };
	if (memcmp((BYTE*)(offset_D2Game + SL_FINALIZE_EXIT), exitBytes, sizeof(exitBytes)))
		return offset_D2Game + SL_FINALIZE_EXIT;
	if (!callsTo(SL_ISSUE_CALL, SL_ISSUER))
		return offset_D2Game + SL_ISSUE_CALL;
	return 0;
}

#ifdef PLUGY_TEST_HOOKS
static void installTestHooks()
{
	if (testHeaderCacheBytes)
	{
		static const DWORD capSites[] = { SL_HEADER_CAP_PUSH1, SL_HEADER_CAP_PUSH2, SL_HEADER_CAP_PUSH3, SL_HEADER_CAP_PUSH4 };
		DWORD changed = 0;
		for (int i = 0; i < 4 && !changed; i++)
			if (!hasOperand(capSites[i], 0x68, SL_HEADER_CAP))
				changed = offset_D2Game + capSites[i];
		if (changed)
			log_msg("SpriteLoad TEST: header cache budget not patched: the code was already changed (site %08X)\n", changed);
		else
			for (int i = 0; i < 4; i++)
			{
				mem_seek(offset_D2Game + capSites[i] + 1);
				MEMT_DWORD(SL_HEADER_CAP, testHeaderCacheBytes);
				//004573FE  |. 68 00D00700    PUSH 7D000
			}
	}
	if (testSpriteLoadDelayMs)
	{
		if (!callsTo(SL_STORM_READ_CALL, SL_STORM_READ))
			log_msg("SpriteLoad TEST: sprite read delay not patched: the code was already changed (site %08X)\n",
				offset_D2Game + SL_STORM_READ_CALL);
		else
		{
			sl_stormRead = offset_D2Game + SL_STORM_READ;
			mem_seek(offset_D2Game + SL_STORM_READ_CALL + 1);
			MEMC_REF4(offset_D2Game + SL_STORM_READ, caller_spriteReadDelay);
			//0041A6A9  |. E8 92C9FFFF    CALL Game.00417040
			log_msg("SpriteLoad TEST: delaying each sprite read by %u ms\n", testSpriteLoadDelayMs);
		}
	}
}
#endif

static DWORD changedHeaderCacheSite();

void Install_SpriteLoad()
{
	static int isInstalled = false;
	if (isInstalled) return;
	isInstalled = true;
	if (version_D2Game != V114d)
		return;

	log_msg("Patch D2Game to keep each sprite load on its own header. (SpriteLoad)\n");
	DWORD changed = changedSpriteLoadSite();
	if (changed)
	{
		log_msg("SpriteLoad not installed: the code was already changed (site %08X)\n\n", changed);
		return;
	}
	// The issue check reads the cache under hdrLock, which only protects it once HeaderCacheLock is in. Before it
	// installs its sites are still vanilla, and after it they are its own, so either install may come first.
	changed = headerCacheLockInstalled ? 0 : changedHeaderCacheSite();
	if (changed)
	{
		log_msg("SpriteLoad not installed: the header cache code it needs locked was already changed (site %08X)\n\n",
			changed);
		return;
	}
	if (!initShared())
	{
		log_msg("SpriteLoad not installed: PlugY.dll could not be pinned (error %u)\n\n", pinError);
		return;
	}
	sl_issuer = offset_D2Game + SL_ISSUER;
	sl_finalizer = offset_D2Game + SL_FINALIZER;
	sl_finalizeExit = offset_D2Game + SL_FINALIZE_EXIT;

	// Sizes first and the issue last: every sprite object issued with a copy has the tail the other hooks read.
	mem_seek(offset_D2Game + SL_ALLOC_SIZE_ASYNC + 1);
	MEMT_DWORD(VANILLA_SPRITE_OBJECT_SIZE, SPRITE_OBJECT_SIZE);
	//005FFFA8  |. BA B8000000    MOV EDX,0B8
	mem_seek(offset_D2Game + SL_MEMSET_ASYNC + 1);
	MEMT_DWORD(VANILLA_SPRITE_OBJECT_SIZE, SPRITE_OBJECT_SIZE);
	//005FFFC4  |. 68 B8000000    PUSH 0B8
	mem_seek(offset_D2Game + SL_ALLOC_SIZE_SYNC + 1);
	MEMT_DWORD(VANILLA_SPRITE_OBJECT_SIZE, SPRITE_OBJECT_SIZE);
	//005FFEA4  |. BA B8000000    MOV EDX,0B8
	mem_seek(offset_D2Game + SL_MEMSET_SYNC + 1);
	MEMT_DWORD(VANILLA_SPRITE_OBJECT_SIZE, SPRITE_OBJECT_SIZE);
	//005FFEC1  |. 68 B8000000    PUSH 0B8
	mem_seek(offset_D2Game + SL_LOAD_DONE_MOV + 3);
	MEMT_DWORD(offset_D2Game + SL_LOAD_DONE, spriteLoadDone);
	//005FFB8E  |. C745 D0 E0F65F00  MOV DWORD PTR SS:[EBP-30],Game.005FF6E0
	mem_seek(offset_D2Game + SL_FINALIZE_CALL + 1);
	MEMC_REF4(offset_D2Game + SL_FINALIZER, caller_spriteFinalize);
	//006003A2  |. E8 E9E8FFFF    CALL Game.005FEC90
	mem_seek(offset_D2Game + SL_ISSUE_CALL + 1);
	MEMC_REF4(offset_D2Game + SL_ISSUER, caller_spriteIssue);
	//0060001A  |. E8 01FAFFFF    CALL Game.005FFA20
	log_msg("SpriteLoad installed\n");

#ifdef PLUGY_TEST_HOOKS
	installTestHooks();
#endif
	log_msg("\n");
}

// The first site of the HeaderCacheLock patches that differs from 1.14d, as an address, or 0.
static DWORD changedHeaderCacheSite()
{
	if (!callsTo(SL_HEADER_GET_CALL1, SL_HEADER_GET))
		return offset_D2Game + SL_HEADER_GET_CALL1;
	if (!callsTo(SL_HEADER_GET_CALL2, SL_HEADER_GET))
		return offset_D2Game + SL_HEADER_GET_CALL2;
	if (!callsTo(SL_FLUSH_CALL1, SL_POOL_FLUSH))
		return offset_D2Game + SL_FLUSH_CALL1;
	if (!callsTo(SL_FLUSH_CALL2, SL_POOL_FLUSH))
		return offset_D2Game + SL_FLUSH_CALL2;
	if (!callsTo(SL_EVICT_CALL, SL_POOL_EVICT))
		return offset_D2Game + SL_EVICT_CALL;
	if (!callsTo(SL_SYNC_LOAD_CALL1, SL_SYNC_LOAD))
		return offset_D2Game + SL_SYNC_LOAD_CALL1;
	if (!callsTo(SL_SYNC_LOAD_CALL2, SL_SYNC_LOAD))
		return offset_D2Game + SL_SYNC_LOAD_CALL2;
	return 0;
}

void Install_HeaderCacheLock()
{
	static int isInstalled = false;
	if (isInstalled) return;
	isInstalled = true;
	if (version_D2Game != V114d)
		return;

	log_msg("Patch D2Game to lock the sprite header cache. (HeaderCacheLock)\n");
	DWORD changed = changedHeaderCacheSite();
	if (changed)
	{
		log_msg("HeaderCacheLock not installed: the code was already changed (site %08X)\n\n", changed);
		return;
	}
	if (!initShared())
	{
		log_msg("HeaderCacheLock not installed: PlugY.dll could not be pinned (error %u)\n\n", pinError);
		return;
	}
	sl_headerGet = offset_D2Game + SL_HEADER_GET;
	sl_syncLoad = offset_D2Game + SL_SYNC_LOAD;

	mem_seek(offset_D2Game + SL_EVICT_CALL + 1);
	MEMC_REF4(offset_D2Game + SL_POOL_EVICT, headerEvictOrRepair);
	//00609465  |. E8 66FEFFFF    CALL Game.006092D0
	mem_seek(offset_D2Game + SL_FLUSH_CALL1 + 1);
	MEMC_REF4(offset_D2Game + SL_POOL_FLUSH, headerFlushLocked);
	//005FFD9D  |. E8 3E960000    CALL Game.006093E0
	mem_seek(offset_D2Game + SL_FLUSH_CALL2 + 1);
	MEMC_REF4(offset_D2Game + SL_POOL_FLUSH, headerFlushLocked);
	//005FFE69  |. E8 72950000    CALL Game.006093E0
	mem_seek(offset_D2Game + SL_HEADER_GET_CALL1 + 1);
	MEMC_REF4(offset_D2Game + SL_HEADER_GET, caller_headerGetLocked);
	//0060025A  |. E8 31E7FFFF    CALL Game.005FE990
	mem_seek(offset_D2Game + SL_HEADER_GET_CALL2 + 1);
	MEMC_REF4(offset_D2Game + SL_HEADER_GET, caller_headerGetLocked);
	//00600552  |. E8 39E4FFFF    CALL Game.005FE990
	mem_seek(offset_D2Game + SL_SYNC_LOAD_CALL1 + 1);
	MEMC_REF4(offset_D2Game + SL_SYNC_LOAD, caller_syncLoadLocked);
	//006001DD  |. E8 AEFCFFFF    CALL Game.005FFE90
	mem_seek(offset_D2Game + SL_SYNC_LOAD_CALL2 + 1);
	MEMC_REF4(offset_D2Game + SL_SYNC_LOAD, caller_syncLoadLocked);
	//00600488  |. E8 03FAFFFF    CALL Game.005FFE90
	headerCacheLockInstalled = true;
	log_msg("HeaderCacheLock installed\n");
	log_msg("\n");
}

/*================================= END OF FILE =================================*/

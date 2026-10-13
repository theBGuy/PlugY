/*=================================================================
	Unit tests for src/plugy/updateNoticeRules.h.
	Built and run by scripts/build.ps1 -Test; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/updateNoticeRules.h"
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

#define FRAME_MS 40	// one frame at 25 fps

// Frames from 'from' up to but not including 'to', every FRAME_MS; how many of them returned true.
static int run(UpdateGameNoticeTimer* t, unsigned long from, unsigned long to, bool ready)
{
	int due = 0;
	for (unsigned long now = from; (long)(now - to) < 0; now += FRAME_MS)
		if (updateGameNoticeStep(t, now, ready))
			due++;
	return due;
}

static void testDueAfterTheDelay()
{
	UpdateGameNoticeTimer t = {};
	CHECK(!updateGameNoticeStep(&t, 5000, true));		// the first ready frame arms
	CHECK(run(&t, 5040, 7000, true) == 0);
	CHECK(updateGameNoticeStep(&t, 7000, true));		// due exactly at the delay
	CHECK(t.showAt == 7000);
}

static void testNotReadyNeverDue()
{
	UpdateGameNoticeTimer t = {};
	CHECK(run(&t, 5000, 20000, false) == 0);
	CHECK(!t.waiting);
}

static void testNotReadyRestartsTheDelay()
{
	UpdateGameNoticeTimer t = {};
	CHECK(run(&t, 5000, 6000, true) == 0);
	CHECK(!updateGameNoticeStep(&t, 6000, false));		// no newer release or no player for one frame
	CHECK(!updateGameNoticeStep(&t, 6040, true));		// arms again
	CHECK(t.showAt == 8040);
	CHECK(run(&t, 6080, 8040, true) == 0);
	CHECK(updateGameNoticeStep(&t, 8040, true));
}

static void testNewGameRestartsTheDelay()
{
	// Game 1 left after 1 s; no frames during the next load (1.2 s); game 2 must wait its own 2 s.
	UpdateGameNoticeTimer t = {};
	CHECK(run(&t, 5000, 6000, true) == 0);
	CHECK(!updateGameNoticeStep(&t, 7200, true));
	CHECK(t.showAt == 9200);
	CHECK(run(&t, 7240, 9200, true) == 0);
	CHECK(updateGameNoticeStep(&t, 9200, true));
}

static void testGapAtTheLimitIsTheSameGame()
{
	UpdateGameNoticeTimer t = {};
	CHECK(!updateGameNoticeStep(&t, 5000, true));
	CHECK(!updateGameNoticeStep(&t, 6000, true));		// 1000 ms: a slow frame, not a new game
	CHECK(t.showAt == 7000);
	CHECK(updateGameNoticeStep(&t, 7000, true));
	UpdateGameNoticeTimer u = {};
	CHECK(!updateGameNoticeStep(&u, 5000, true));
	CHECK(!updateGameNoticeStep(&u, 6001, true));		// 1001 ms: a new game
	CHECK(u.showAt == 8001);
}

static void testTickCountWrap()
{
	// Armed 1 s before GetTickCount wraps: due 1 s after the wrap, never at the wrap itself.
	UpdateGameNoticeTimer t = {};
	unsigned long start = 0xFFFFFFFFul - 999;			// 1000 ms before 2^32
	CHECK(!updateGameNoticeStep(&t, start, true));
	CHECK(t.showAt == 1000);
	CHECK(run(&t, start + FRAME_MS, 1000, true) == 0);	// crosses 0 without a re-arm or an early print
	CHECK(t.showAt == 1000);
	CHECK(updateGameNoticeStep(&t, 1000, true));
}

static void testFirstFrameAfterBootArms()
{
	// GetTickCount below the gap (seconds after boot): the first frame still arms rather than printing.
	UpdateGameNoticeTimer t = {};
	CHECK(!updateGameNoticeStep(&t, 300, true));
	CHECK(t.waiting && t.showAt == 2300);
	CHECK(run(&t, 340, 2300, true) == 0);
	CHECK(updateGameNoticeStep(&t, 2300, true));
}

int main()
{
	testDueAfterTheDelay();
	testNotReadyNeverDue();
	testNotReadyRestartsTheDelay();
	testNewGameRestartsTheDelay();
	testGapAtTheLimitIsTheSameGame();
	testTickCountWrap();
	testFirstFrameAfterBootArms();
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

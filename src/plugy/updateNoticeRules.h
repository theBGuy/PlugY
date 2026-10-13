/*=================================================================
	When the in-game update line prints: a while after the client player appears, the delay starting again for
	each new game. No D2 or PlugY dependencies, so tests/UpdateNoticeRulesTests.cpp covers it.
=================================================================*/
#pragma once

// The load screen still covers the first frames after the client player appears.
#define UPDATE_GAME_NOTICE_DELAY_MS	2000
// The draw step stops between games, so a longer gap between two calls means a new game is loading.
#define UPDATE_GAME_FRAME_GAP_MS	1000

// Zero-initialized before the first frame.
struct UpdateGameNoticeTimer
{
	bool waiting;
	unsigned long showAt;
	unsigned long lastFrame;
};

/* One in-game frame at tick count now (GetTickCount, which wraps at 2^32); ready means a newer release is known
   and the client player exists. True on each frame once the line is due: the caller prints it and stops calling. */
inline bool updateGameNoticeStep(UpdateGameNoticeTimer* t, unsigned long now, bool ready)
{
	bool newGame = now - t->lastFrame > UPDATE_GAME_FRAME_GAP_MS;
	t->lastFrame = now;
	if (!ready)
	{
		t->waiting = false;
		return false;
	}
	if (!t->waiting || newGame)
	{
		t->waiting = true;
		t->showAt = now + UPDATE_GAME_NOTICE_DELAY_MS;
		return false;
	}
	return (long)(now - t->showAt) >= 0;
}

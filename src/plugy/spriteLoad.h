/*=================================================================
	Sprite loading: an asynchronous DCC sprite load decodes with its own copy of its file's header, checked when the
	load is issued, and sprite loads call into the game's DCC header cache under one lock. 1.14d only.

=================================================================*/
#pragma once

#include "common.h"

void Install_SpriteLoad();
void Install_HeaderCacheLock();

#ifdef PLUGY_TEST_HOOKS
// [TEST] knobs of the live test harness, read once by loadParameters before any install and never written again.
extern int testSpriteFix, testSpriteGuard, testHeaderCacheLock;
extern DWORD testSpriteLoadDelayMs, testHeaderCacheBytes, testSpriteInjectEvery, testSpriteIssueInjectEvery,
             testSpriteDecodeFailEvery, testSpriteSkipCallbackEvery;
#endif

/*================================= END OF FILE =================================*/

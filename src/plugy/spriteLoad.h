/*=================================================================
	Sprite loading: each asynchronous DCC sprite load decodes with its own copy of the file's header, checked
	against the file it was issued for, and the game's DCC header cache is used under one lock. 1.14d only.

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

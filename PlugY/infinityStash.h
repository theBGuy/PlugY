/*=================================================================
	File created by Yohann NICOLAS.

	infinity Stash gestion

=================================================================*/
#pragma once

#include "common.h"

extern DWORD maxSelfPages;
extern DWORD maxSharedPages;
extern DWORD nbPagesPerIndex;
extern DWORD nbPagesPerIndex2;
extern bool active_multiPageStash;
extern int active_SharedStashInMultiPlayer;
extern bool active_sharedStash;
extern bool active_sharedGold;
extern bool separateHardSoftStash;
extern char* sharedStashFilename;

extern bool displaySharedSetItemNameInGreen;
extern int posXPreviousBtn;
extern int posYPreviousBtn;
extern int posWPreviousBtn;
extern int posHPreviousBtn;
extern int posXNextBtn;
extern int posYNextBtn;
extern int posWNextBtn;
extern int posHNextBtn;
extern int posXSharedBtn;
extern int posYSharedBtn;
extern int posWSharedBtn;
extern int posHSharedBtn;
extern int posXPreviousIndexBtn;
extern int posYPreviousIndexBtn;
extern int posWPreviousIndexBtn;
extern int posHPreviousIndexBtn;
extern int posXNextIndexBtn;
extern int posYNextIndexBtn;
extern int posWNextIndexBtn;
extern int posHNextIndexBtn;
extern int posXPutGoldBtn;
extern int posYPutGoldBtn;
extern int posWPutGoldBtn;
extern int posHPutGoldBtn;
extern int posXTakeGoldBtn;
extern int posYTakeGoldBtn;
extern int posWTakeGoldBtn;
extern int posHTakeGoldBtn;
extern int posXStashNameField;
extern int posYStashNameField;
extern int posWStashNameField;
extern int posHStashNameField;
extern int posXStashGoldField;
extern int posYStashGoldField;
extern int posWStashGoldField;
extern int posHStashGoldField;

extern bool autoRenameStashPage;
extern DWORD PersonalNormalPageColor;
extern DWORD PersonalIndexPageColor;
extern DWORD PersonalMainIndexPageColor;
extern DWORD SharedNormalPageColor;
extern DWORD SharedIndexPageColor;
extern DWORD SharedMainIndexPageColor;

bool hasCurrentStash(Unit* ptChar);
void toggleToSharedStash(Unit* ptChar);
void toggleToSelfStash(Unit* ptChar);
void setCurrentStashIndex(Unit* ptChar, int index);
void selectPreviousStash(Unit* ptChar);
void selectNextStash(Unit* ptChar);
void selectPreviousIndexStash(Unit* ptChar);
void selectNextIndexStash(Unit* ptChar);
void selectPrevious2Stash(Unit* ptChar);
void selectNext2Stash(Unit* ptChar);
void selectPreviousIndex2Stash(Unit* ptChar);
void selectNextIndex2Stash(Unit* ptChar);
void swapStash(Unit* ptChar, DWORD page, bool toggle);
bool canCreateStashPage(bool isShared, DWORD page);	// false: a missing page with this id may not be created
void renameCurrentStash(Unit* ptChar, char* name);
void insertStash(Unit* ptChar);
bool canDeleteStash(Unit* ptChar);	// the page shown is empty, so deleteStash would run
// Server: the caller reports the delete's relocations (flushPendingMoves) once the client has run the same delete.
bool deleteStash(Unit* ptChar, bool isClient);

void updateSelectedStashClient(Unit* ptChar);
void selectStash(Unit* ptChar, Stash* newStash, bool forceUpdate = false);
void setSelectedStashClient(DWORD stashId, DWORD stashFlags, DWORD flags, bool bOnlyItems);

Stash* addStash(Unit* ptChar, bool isShared, bool autoSetIndex, Stash* ptStash = NULL);
Stash* getStash(Unit* ptChar, DWORD isShared, DWORD id);	// NULL when the list has no page with this id
DWORD loadStashList(Unit* ptChar, BYTE* data, DWORD maxSize, DWORD* curSize, bool isShared);
DWORD FASTCALL placeLoadedItem(Game* ptGame, Unit* ptChar, Unit* ptItem, Unit* ptParent);
void showMovedStashItemsNotice(Unit* ptChar);
void saveStashList(Unit* ptChar, Stash* ptStash, BYTE** data, DWORD* maxSize, DWORD* curSize);
bool getSharedStashCanonicalHash(Unit* ptChar, DWORD* hash);	// false when the hash can't be computed (e.g. an item fails to serialize)

// Where findStashItem found an item. CURRENT: owned page-4 item of the inventory chain (the page shown).
// LIST: a hidden page's ptListItem list. PHANTOM: in the inventory chain but not an owned page-4 item (an
// inventory/belt/equipped item, or an unowned unit behind the chain's tail): never move it.
enum { STASHLOC_CURRENT = 0, STASHLOC_LIST = 1, STASHLOC_PHANTOM = 3 };
struct StashLoc
{
	Stash*	page;	// CURRENT: the current page; LIST: its page; PHANTOM: NULL
	Unit*	prev;	// LIST: predecessor through ItemData ptNextItem, NULL when first
	BYTE	kind;
};

// 1.14d only. They work on ptChar's own units, so the server and client players use the same code.
Unit* findStashItem(Unit* ptChar, DWORD gid, StashLoc* out);	// gid = Unit nItemNum; NULL when not found
bool detachItem(Unit* ptChar, Unit* item, StashLoc* loc);		// true: the item is unlinked (next/prev NULL); false: it stays where it was
bool attachItem(Unit* ptChar, Unit* item, Stash* target, BYTE x, BYTE y);	// current page: false when the grid refuses it
bool stashFindSpot(Unit* ptChar, Stash* page, Unit* item, BYTE* x, BYTE* y, bool explicitXY);
void flushPendingMoves(Unit* ptChar);	// one UC_ITEM_MOVED per relocation a page switch recorded
void relocateStashItem(Unit* ptChar, Unit* item, Stash* source);	// server: a free spot of the page shown, else a "Moved items" page; never drops it
// PinnedItems as ItemsBIN codes (+0x80, 4 chars, space padded); 1.14d only, empty when nothing is pinned.
#define MAX_PIN_CODES 5
extern DWORD pinCodes[MAX_PIN_CODES];
extern DWORD nbPinCodes;
// Server player: the PinnedItems codes, once its client confirmed the protocol (it then holds the same codes). Client
// player: only the codes of the server's UC_PROTO.
bool isPinnedItem(Unit* ptChar, Unit* item, DWORD bIsClient);
bool serverPinsSwaps(Unit* ptChar);	// this player's swaps keep pinned items on their page
// A page swap (the page shown keeps its id, its items and target's trade places); keepPinned: pinned items stay on
// their page.
int swapStashItems(Unit* ptChar, Stash* target, DWORD bIsClient, bool keepPinned);
// UC_SELECT_STASH stash flags of a swap (with bit3): the server kept pinned items, so the client keeps them too.
#define STASH_SWAP_PINNED 0x10

void getCurrentStashName(WCHAR* buffer, DWORD maxSize, Unit* ptChar);
WCHAR* getDefaultStashName(Unit* ptChar);

void Install_MultiPageStash();

/*================================= END OF FILE =================================*/
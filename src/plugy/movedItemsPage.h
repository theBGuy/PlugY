/*=================================================================
	Name and label of the stash pages that take the items which no
	longer fit their page. No D2 or PlugY dependencies, so
	tests/MovedItemsPageTests.cpp covers them.
=================================================================*/
#pragma once

#include <windows.h>
#include <string.h>

// Saved, matched and reported to scripts under this English name in every game language, so a page made under one
// language is still found under another. Only the stash name field, its Shift+click prefill and the notice use the
// translated label.
#define MOVED_ITEMS_PAGE_NAME "Moved items"
#define MOVED_ITEMS_PAGE_NAME_W L"" MOVED_ITEMS_PAGE_NAME
#define MOVED_ITEMS_PAGE_LABEL_KEY L"STR_MOVED_ITEMS_PAGE"
// The stash shows at most this many characters of a page name.
#define STASH_NAME_MAX_SHOWN 20

inline bool isMovedItemsPageName(const char* name)
{
	return name && !strcmp(name, MOVED_ITEMS_PAGE_NAME);
}

// loaded: what LocalizedStrings.ini gave; a file without the entry (an older one, or one a mod packs in its MPQ)
// gives the entry's own name.
inline LPCWSTR movedItemsPageLabel(LPCWSTR loaded)
{
	if (!loaded || !loaded[0] || !wcscmp(loaded, MOVED_ITEMS_PAGE_LABEL_KEY))
		return MOVED_ITEMS_PAGE_NAME_W;
	return loaded;
}

// What Shift+click on a "Moved items" page fills in after /rp, given the name the stash shows. Pressing Enter on it
// unchanged must never rename the page, and only ASCII reaches /rp as the same text whatever code page the game
// narrows chat input to, so any other label gives the saved name.
inline LPCWSTR movedItemsPagePrefill(LPCWSTR shown)
{
	for (LPCWSTR c = shown; *c; c++)
		if (*c > 0x7F)
			return MOVED_ITEMS_PAGE_NAME_W;
	return shown;
}

// Whether a /rp name (renamePage has trimmed its spaces) is the ASCII label the stash shows, as Shift+click fills
// it in.
inline bool isShownMovedItemsLabel(const char* typed, LPCWSTR label)
{
	if (!typed || !label)
		return false;
	WCHAR shown[STASH_NAME_MAX_SHOWN + 1];
	wcsncpy(shown, label, STASH_NAME_MAX_SHOWN);
	shown[STASH_NAME_MAX_SHOWN] = 0;
	LPCWSTR start = shown;
	while (*start == L' ')
		start++;
	size_t len = wcslen(start);
	while (len && start[len - 1] == L' ')
		len--;
	if (strlen(typed) != len)
		return false;
	for (size_t i = 0; i < len; i++)
		if (start[i] > 0x7F || (WCHAR)(BYTE)typed[i] != start[i])
			return false;
	return true;
}

/*================================= END OF FILE =================================*/

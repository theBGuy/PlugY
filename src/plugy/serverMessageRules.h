/*=================================================================
	Rules for the client->server custom messages (packet 0x3A), shared by the client that sends them and the
	server that checks them, so both judge alike. No D2 or PlugY dependencies, so tests/ServerMessageRulesTests.cpp
	covers them.
=================================================================*/
#pragma once

#include "d2/updatingConst.h"
#include <string.h>

// Lower types belong to the game's own add-stat-point request, which shares the packet (it acts on stat ids 0-3).
inline bool isVanillaServerMessage(int type)
{
	return type < US_UNASSIGN_STR_POINT;
}

// The game's own mode (ClientGameMode): 0 in single player; measured on 1.14d, a TCP/IP host reads 8 and a joiner 9. A
// multiplayer host indexes each client by the name it joined with, so renames run in single player only.
#define CLIENT_GAME_MODE_SINGLE_PLAYER	0

inline bool isSinglePlayerGameMode(unsigned long mode)
{
	return mode == CLIENT_GAME_MODE_SINGLE_PLAYER;
}

#define RENAME_NAME_MIN	2
#define RENAME_NAME_MAX	15	// the name fields hold 16 bytes

inline bool isRenameNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// A name /renamechar accepts: letters and underscores only, the rule the client already applied.
inline bool isRenameCharName(const char* name)
{
	if (!name)
		return false;
	size_t len = strlen(name);
	if (len < RENAME_NAME_MIN || len > RENAME_NAME_MAX)
		return false;
	for (size_t i = 0; i < len; i++)
		if (!isRenameNameChar(name[i]))
			return false;
	return true;
}

/*=================================================================
	Rules for the client->server custom messages (packet 0x3A), shared by the client that sends them and the
	server that checks them, so both judge alike. No D2 or PlugY dependencies, so tests/ServerMessageRulesTests.cpp
	covers them.
=================================================================*/
#pragma once

#include "d2/updatingConst.h"
#include <string.h>

// Lower types are the game's own stat point requests (stat id 0-3), which share the packet.
inline bool isVanillaServerMessage(int type)
{
	return type < US_UNASSIGN_STR_POINT;
}

// The game's own mode (ClientGameMode): 0 starts the single player server, 1 a TCP/IP host (read in 1.14d's game
// setup). A multiplayer host indexes each client by the name it joined with, so renames run in single player only.
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

// A name /renamechar accepts: it also names the save files, so letters and underscores only.
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

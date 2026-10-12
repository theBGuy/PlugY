/*=================================================================
	File created by Yohann NICOLAS.

	Load Player Custom Data.

=================================================================*/
#pragma once

#include "common.h"

// The character this client last joined a multiplayer game with ("" before the first join).
extern char joinedCharName[17];

void Install_LoadPlayerData();

// Frees what a host received from clientID's save upload, once its connection is gone (a later client may reuse
// the id, and must not append to the stale entry). Server thread only, like ReceiveSaveFiles.
void discardClientSaveFiles(DWORD clientID);

/*================================= END OF FILE =================================*/

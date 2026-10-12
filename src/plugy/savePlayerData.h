/*=================================================================
	File created by Yohann NICOLAS.

	Save Player Custom Data.

=================================================================*/
#pragma once

#include "common.h"

extern bool active_AutoBackup;
extern int maxBackupPerCharacter;

void backupSaveFiles(char* name, int isHardCoreGame);
void Install_SavePlayerData();

// Frees the save files a host still holds for clientID, once its connection is gone and nothing more can be sent to
// it. Server thread only, like SendSaveFilesToSave.
void discardClientSaveTransfer(DWORD clientID);

/*================================= END OF FILE =================================*/
/*=================================================================
	File created by Yohann NICOLAS.

	Save Player Custom Data.

=================================================================*/
#pragma once

#include "common.h"

extern bool active_AutoBackup;
extern int maxBackupPerCharacter;
extern bool plugyForcedSave;	// true while PlugY's /save runs D2SaveGame

void backupSaveFiles(char* name, int isHardCoreGame);
void Install_SavePlayerData();

/*================================= END OF FILE =================================*/
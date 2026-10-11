/*=================================================================
	File created by Yohann NICOLAS.

	Loading parameters from ini file.

=================================================================*/
#pragma once

#include "../common/PlugYVersion.h"

enum TargetMod
{
	MOD_NO = 0,
};

// Global variable (user parameters)
extern char* modDataDirectory;
extern bool active_plugin;
extern bool active_DisableBattleNet;
extern bool active_CheckMemory;
extern char* dllFilenames;
extern bool active_D2Mod;
extern TargetMod selectModParam;
extern bool active_CheckForUpdates;
extern int updateChannel;	// eUpdateChannel (src/common/UpdateCheck.h)

void loadParameters();

/*================================= END OF FILE =================================*/
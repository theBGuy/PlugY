/*=================================================================
	File created by Yohann NICOLAS.

	Commands directly in game.

=================================================================*/
#pragma once

#include "common.h"

extern bool active_Commands;
extern bool active_savegame;	// /save can run: set by Install_Commands

void savePlayers(Unit* ptChar);
void putGold(Unit* ptChar, DWORD amount);
void takeGold(Unit* ptChar, DWORD amount);
void updateSharedGold(DWORD goldAmount);

void Install_Commands();

/*================================= END OF FILE =================================*/
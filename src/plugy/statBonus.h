/*=================================================================
	Stat bonus: the game server adds the [STAT BONUS] magic find and gold find to every character it loads into a
	game, on top of its gear, and tells that character's client so PlugY's stats page shows it. 1.14d only.

=================================================================*/
#pragma once

#include "common.h"

extern bool active_StatBonus;
extern int statBonusMagicFind;
extern int statBonusGoldFind;

void applyStatBonusClient(DataPacket* packet);
void Install_StatBonus();

/*================================= END OF FILE =================================*/

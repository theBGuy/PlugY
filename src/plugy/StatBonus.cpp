/*=================================================================
	Stat bonus: the game server adds the [STAT BONUS] magic find and gold find to every character it loads into a
	game. 1.14d only.

	Both are base stats the .d2s never saves (no CSvBits), so each load starts without them, and the game never
	sends them to the client (not Saved), so the server mirrors each value with UC_ADD_STAT.

=================================================================*/

#include "statBonus.h"
#include "statBonusRules.h"
#include "updateClient.h"
#include "common.h"

bool active_StatBonus = false;
int statBonusMagicFind = 0;
int statBonusGoldFind = 0;

C_ASSERT(STAT_BONUS_GOLD_FIND == STATS_ITEM_GOLDBONUS);
C_ASSERT(STAT_BONUS_MAGIC_FIND == STATS_ITEM_MAGICBONUS);

// An absolute set, so a value the base list already holds (a mod that saves the stat) never stacks.
static void setBaseStat(Unit* ptChar, DWORD statId, int value)
{
	int base = D2GetPlayerBaseStat(ptChar, statId, 0);
	if (base != value)
		D2AddPlayerStat(ptChar, statId, value - base, 0);
}

static void applyStatBonus(Unit* ptChar, DWORD statId, int value)
{
	if (!value)
		return;
	setBaseStat(ptChar, statId, value);
	updateClient(ptChar, UC_ADD_STAT, statId, value, 0);
}

// Replaces CLIENTS_AddPlayerToGame's call to D2InvModeLast (same register contract). The save is loaded and the
// join state sent by then, the client's player unit included, so the mirror reaches a unit that exists.
void FASTCALL statBonusOnPlayerLoaded(Game* ptGame, Unit* ptChar)
{
	D2InvModeLast(ptGame, ptChar);
	if (!ptChar || ptChar->nUnitType != UNIT_PLAYER)
		return;
	applyStatBonus(ptChar, STATS_ITEM_MAGICBONUS, statBonusMagicFind);
	applyStatBonus(ptChar, STATS_ITEM_GOLDBONUS, statBonusGoldFind);
	log_msg("Stat bonus for %s : magic find %d (total %d), gold find %d (total %d)\n", PCPlayerData ? PCPlayerData->name : "?",
		statBonusMagicFind, D2GetPlayerStat(ptChar, STATS_ITEM_MAGICBONUS, 0), statBonusGoldFind, D2GetPlayerStat(ptChar, STATS_ITEM_GOLDBONUS, 0));
}

// Applies whatever the server sends, not this client's own INI: in TCP/IP the host's server decides the bonus.
void applyStatBonusClient(DataPacket* packet)
{
	Unit* ptChar = D2GetClientPlayer();
	DWORD statId = packet->mParam1;
	if (!ptChar || ptChar->nUnitId != packet->mPlayerID || !isStatBonusStat(statId))
	{
		log_msg("[CLIENT] Stat bonus %u for player %u ignored\n", statId, packet->mPlayerID);
		return;
	}
	setBaseStat(ptChar, statId, clampStatBonus((int)packet->mParam2));
}

void Install_StatBonus()
{
	static int isInstalled = false;
	if (isInstalled) return;

	// The client mirror needs the 0x9D handler, which otherwise only the multi-page stash installs.
	Install_UpdateClient();

	log_msg("Patch D2Game for the stat bonus on each character load. (StatBonus)\n");

	mem_seek R8(D2Game, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 139A0C);
	MEMC_REF4( D2InvModeLast, statBonusOnPlayerLoaded);
	//00539A0B  |. E8 F0E00500    CALL Game.00597B00

	log_msg("\n");

	isInstalled = true;
}

/*================================= END OF FILE =================================*/

/*=================================================================
	Value rules of the [STAT BONUS] section, shared by the server that applies a bonus and the client that mirrors
	it, so both clamp alike. No D2 or PlugY dependencies, so tests/StatBonusTests.cpp covers them.
=================================================================*/
#pragma once

#include <stdlib.h>

// ItemStatCost ids; StatBonus.cpp asserts they match d2constants.h.
#define STAT_BONUS_GOLD_FIND	79	// item_goldbonus
#define STAT_BONUS_MAGIC_FIND	80	// item_magicbonus

// The drop code multiplies magic find by up to 600 and (gold find + 100) by the gold pile, both in 32 bits, so this
// cap keeps far from an overflow with the game's own treasure classes.
#define STAT_BONUS_MAX			10000

inline int clampStatBonus(int value)
{
	return value < 0 ? 0 : value > STAT_BONUS_MAX ? STAT_BONUS_MAX : value;
}

// An INI value; atoi saturates out-of-range text and reads garbage as 0, so the clamp bounds every input.
inline int parseStatBonus(const char* text)
{
	return text ? clampStatBonus(atoi(text)) : 0;
}

inline bool isStatBonusStat(unsigned int statId)
{
	return statId == STAT_BONUS_GOLD_FIND || statId == STAT_BONUS_MAGIC_FIND;
}

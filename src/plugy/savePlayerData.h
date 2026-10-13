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

/*---------------- Crash-safe single-player saves, 1.14d ----------------*/
#include "../common/CharacterCommit.h"

extern bool active_CrashSafeSaves;
void Install_CharacterCommit();

// The commit result of the last single-player save; seq is the journal sequence of that save.
CommitResult getLastSaveResult(DWORD* seq = NULL);
// Saves the game now (D2SaveGame) and returns that save's result: ABORTED when no character was saved.
CommitResult forceCharacterSave(Game* ptGame, DWORD* seq = NULL);

// One recovery run (CommitDecideRecovery and its actions) for a character's journal.
struct CharacterRecovery
{
	CommitVerdict verdict;
	CommitQuarantineCause cause;
	CommitParseStatus parse;
	bool committed;			// the commit test held when recovery read the journal
	bool rewroteD2s;		// an action replaced the .d2s
	bool actionFailed;		// an action failed: everything after it, the journal included, stayed on disk
	DWORD readError;		// READ_ERROR: the open or read error of file
	char file[MAX_PATH];	// the file a refusal names: quarantined, unreadable, or one an action could not change
};
// loadedHere: the current game plays the character (false at a load and at the first save of a new character).
// Returns true when the save or load may go on: a proceeding verdict whose every action succeeded.
bool recoverCharacterFiles(const char* name, bool loadedHere, CharacterRecovery* out);
// The message box of a quarantine or a failed recovery action, naming the file the player must look at.
void showRecoveryRefusal(const char* name, const CharacterRecovery* recovery, bool atLoad);

// The session lock <name>.pylive, held open without sharing while this process plays the character. One at a time:
// acquiring another name releases the previous one. waited: another process held it and released it meanwhile.
bool acquireSessionLock(const char* name, DWORD waitMs, bool* waited, DWORD* error);
void releaseSessionLockUnlessNamed(const char* name);
// The lock is released with the server-side PlayerData that loaded the character, never with a client-side or
// TCP/IP joiner's PlayerData of the same name.
void setSessionLockOwner(PlayerData* playerData);
void releaseSessionLockOf(PlayerData* playerData);
// After a load passed its checks: the game being loaded plays the character.
void markCharacterLoaded(const char* name, Game* ptGame);

// /renamechar: journals and locks are found by name, so a rename waits until no journal of either name is open.
bool characterRenameAllowed(const char* oldName, const char* newName);
void renameCharacterCommitFiles(const char* oldName, const char* newName);

/*================================= END OF FILE =================================*/
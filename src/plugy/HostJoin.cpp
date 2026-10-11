/*=================================================================
	A TCP/IP host forgets a joiner whose connection closes before its character is in the game. 1.14d only.

	Vanilla never removes such a client: when the net layer releases a connection it only queues a 0x70 "client
	gone" packet to the server, whose handler flags the record (+0x504) for a sweep that runs on realm servers only.
	The record keeps the character's name in the host's name table, so the join validator silently refuses every
	later join under that name until the host leaves the game.
	The patches are active wherever the game runs its own server, single player included; they only change
	anything for a client whose connection closed before its character loaded.

=================================================================*/

#include "hostJoin.h"
#include "loadPlayerData.h"
#include "releasedConnections.h"
#include "common.h"

// 1.14d Game.exe server, as offsets from offset_D2Game.
#define HJ_RELEASE_CALLBACK	0x12B750	// fastcall(net, clientID, file, line), ret 8: queues the 0x70
#define HJ_RELEASE_PUSH		0x12B7AF	// imm32 of "push 52B750h" in the server's net setup
#define HJ_SERVER_NET		0x482D08	// the server's net object, the callback's net argument
#define HJ_FIND_CLIENT		0x137760	// fastcall(clientID): the record with lock 0x8846A8 held, or 0 unlocked
#define HJ_FIND_CALL		0x13F36C	// rel32 of "call 00537760" in the 0x70 case of the dispatcher 0x53F100
#define HJ_UNLOCK_CLIENTS	0x1377A0	// leaves lock 0x8846A8
#define HJ_LOCK_GAME		0x12E860	// fastcall(game key): the game with its lock held, or 0
#define HJ_UNLOCK_GAME		0x12DA90	// fastcall(game)
#define HJ_FIND_IN_GAME		0x137810	// fastcall(game, clientID): the record in the game's client list, or 0
#define HJ_REMOVE_CLIENT	0x139DA0	// fastcall(game, clientID, flag), ret 4: unlinks the record from every table and frees it
#define HJ_REALM_DB			0x483D50	// non-zero on realm servers only

// NetClient +0x04 is the state: 1 is joined with no character loaded yet (the 0x6B load leaves it in one locked pass).
// A game's lock key is the DWORD at game+0.
#define HJ_STATE_JOINED		1
#define HJ_CLIENT_STATE(c)	(*(DWORD*)(c)->uk1)
#define HJ_GAME_KEY(g)		(*(DWORD*)(g))

typedef DWORD (FASTCALL *t_releaseCallback)(void* net, DWORD clientID, DWORD file, DWORD line);
typedef NetClient* (FASTCALL *t_findClient)(DWORD clientID);
typedef void (FASTCALL *t_unlockClients)();
typedef Game* (FASTCALL *t_lockGame)(DWORD key);
typedef void (FASTCALL *t_unlockGame)(Game* game);
typedef NetClient* (FASTCALL *t_findInGame)(Game* game, DWORD clientID);
typedef void (FASTCALL *t_removeClient)(Game* game, DWORD clientID, DWORD flag);

#define HJ_CALL(type, offset) ((type)(offset_D2Game + (offset)))

// Connections the net layer released, recorded on its own thread just before it queues their 0x70. The 0x70 handler
// acts only on an entry of the current server's net object, so neither a 0x70 a connected client sends itself nor an
// entry left over from an earlier server (its 0x70 dropped at shutdown) changes anything.
static CRITICAL_SECTION releasedLock;
static ReleasedConnections released = {};

static void recordRelease(void* net, DWORD clientID)
{
	EnterCriticalSection(&releasedLock);
	addReleased(&released, net, clientID);
	LeaveCriticalSection(&releasedLock);
}

static bool takeCurrentServerRelease(DWORD clientID)
{
	void* net = *(void**)(offset_D2Game + HJ_SERVER_NET);
	EnterCriticalSection(&releasedLock);
	bool found = takeReleased(&released, net, clientID);
	LeaveCriticalSection(&releasedLock);
	return found;
}

static DWORD FASTCALL releasedConnection(void* net, DWORD clientID, DWORD file, DWORD line)
{
	recordRelease(net, clientID);
	return HJ_CALL(t_releaseCallback, HJ_RELEASE_CALLBACK)(net, clientID, file, line);
}

// Replaces the record lookup of the 0x70 case and returns what it returns: after a removal the lookup misses, so the
// case skips its flag write and unlock. Runs on the server thread, as do the join and load handlers.
static NetClient* FASTCALL findClientRemovingDeadJoiner(DWORD clientID)
{
	t_findClient findClient = HJ_CALL(t_findClient, HJ_FIND_CLIENT);
	if (!takeCurrentServerRelease(clientID))
		return findClient(clientID);
	// The connection is gone, so whatever PlugY received from it will never be loaded.
	discardClientSaveFiles(clientID);
	if (*(DWORD*)(offset_D2Game + HJ_REALM_DB))
		return findClient(clientID);

	NetClient* client = findClient(clientID);
	if (!client)
		return NULL;
	// Read under lock 0x8846A8: vanilla unlinks a record from the clientID table before it frees it or its game.
	DWORD state = HJ_CLIENT_STATE(client);
	Game* game = client->ptGame;
	DWORD key = game ? HJ_GAME_KEY(game) : 0;
	char name[sizeof(client->name) + 1];
	strncpy(name, client->name, sizeof(client->name));
	name[sizeof(client->name)] = '\0';
	HJ_CALL(t_unlockClients, HJ_UNLOCK_CLIENTS)();
	if (state != HJ_STATE_JOINED || !game)
		return findClient(clientID);

	// Vanilla's removal when a character fails to load (0x5301F6-0x5301FC), under the game lock it holds there. It
	// finds that game through the connection, which is gone here, hence the key read above.
	Game* locked = HJ_CALL(t_lockGame, HJ_LOCK_GAME)(key);
	if (!locked)
		return findClient(clientID);
	bool removed = false;
	if (locked == game && HJ_CALL(t_findInGame, HJ_FIND_IN_GAME)(game, clientID) == client
		&& HJ_CLIENT_STATE(client) == HJ_STATE_JOINED)
	{
		HJ_CALL(t_removeClient, HJ_REMOVE_CLIENT)(game, clientID, 1);
		removed = true;
	}
	HJ_CALL(t_unlockGame, HJ_UNLOCK_GAME)(locked);
	if (removed)
		log_msg("TCP/IP host: removed client %u (%s), whose connection closed before its character was in the game\n", clientID, name);
	return findClient(clientID);
}

void Install_HostJoin()
{
	static int isInstalled = false;
	if (isInstalled) return;
	isInstalled = true;
	if (version_D2Game != V114d)
		return;

	log_msg("Patch D2Game to forget joiners whose connection closes while joining. (HostJoin)\n");
	DWORD findCall = offset_D2Game + HJ_FIND_CALL;
	DWORD findTarget = findCall + 4 + *(DWORD*)findCall;
	DWORD pushed = *(DWORD*)(offset_D2Game + HJ_RELEASE_PUSH);
	// Both patches or neither: without the other, each one is inert.
	if (findTarget != offset_D2Game + HJ_FIND_CLIENT || pushed != offset_D2Game + HJ_RELEASE_CALLBACK)
	{
		log_msg("HostJoin not installed: the code was already changed (call target %08X, callback %08X)\n\n", findTarget, pushed);
		return;
	}
	InitializeCriticalSection(&releasedLock);
	mem_seek(offset_D2Game + HJ_RELEASE_PUSH);
	MEMT_DWORD(offset_D2Game + HJ_RELEASE_CALLBACK, releasedConnection);
	//0052B7AE  |. 68 50B75200    PUSH Game.0052B750
	mem_seek(findCall);
	MEMC_REF4(offset_D2Game + HJ_FIND_CLIENT, findClientRemovingDeadJoiner);
	//0053F36B  |. E8 F083FFFF    CALL Game.00537760
	log_msg("\n");
}

/*================================= END OF FILE =================================*/

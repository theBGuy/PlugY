/*=================================================================
	The connections a server's net layer released, which HostJoin.cpp checks before it removes a joiner. No D2 or
	PlugY dependencies, so tests/ReleasedConnectionsTests.cpp covers them; the caller does the locking.
=================================================================*/
#pragma once

#include <windows.h>
#include <string.h>

#define RELEASED_CONNECTIONS_MAX 64

struct ReleasedConnection
{
	void* net;
	DWORD clientID;
};

struct ReleasedConnections
{
	ReleasedConnection entries[RELEASED_CONNECTIONS_MAX];
	int count;
};

// Each entry is taken by the 0x70 its release queues right after it, so a full list means some 0x70s were lost:
// the oldest entry goes.
inline void addReleased(ReleasedConnections* list, void* net, DWORD clientID)
{
	if (list->count == RELEASED_CONNECTIONS_MAX)
	{
		memmove(list->entries, list->entries + 1, sizeof(list->entries) - sizeof(list->entries[0]));
		list->count--;
	}
	list->entries[list->count].net = net;
	list->entries[list->count].clientID = clientID;
	list->count++;
}

// Takes one entry of clientID released by currentNet. Entries of any other net object belong to an earlier server,
// whose 0x70s will never come, and are dropped; the current server's other entries keep their order.
inline bool takeReleased(ReleasedConnections* list, void* currentNet, DWORD clientID)
{
	bool found = false;
	int kept = 0;
	for (int i = 0; i < list->count; i++)
	{
		if (list->entries[i].net != currentNet)
			continue;
		if (!found && list->entries[i].clientID == clientID)
		{
			found = true;
			continue;
		}
		list->entries[kept++] = list->entries[i];
	}
	list->count = kept;
	return found;
}

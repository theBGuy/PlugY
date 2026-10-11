/*=================================================================
	Size checks for the PlugY save files (.d2x/.sss) streamed in
	chunks between the host and the joiners of a TCP/IP game. No D2
	or PlugY dependencies, so tests/SaveTransferTests.cpp covers them.
=================================================================*/
#pragma once

#include <windows.h>

// Largest real save file measured on 2026-10-11 was 2984 bytes (a .d2x); 16 MiB leaves wide headroom.
#define SAVE_TRANSFER_MAX_SIZE 0x1000000

enum SaveChunkResult { SAVE_CHUNK_REJECT = 0, SAVE_CHUNK_START = 1, SAVE_CHUNK_APPEND = 2 };

// size/cur: the stream's state so far (size == 0: not started). packSize, finalSize: from the received chunk, whose
// payload is packSize - 1 bytes. START: allocate finalSize bytes, then store the payload at cur. APPEND: store it at cur.
inline SaveChunkResult checkSaveChunk(DWORD size, DWORD cur, BYTE packSize, DWORD finalSize, DWORD maxSize)
{
	if (packSize == 0)
		return SAVE_CHUNK_REJECT;
	if (finalSize == 0 || finalSize > maxSize)
		return SAVE_CHUNK_REJECT;
	DWORD payload = (DWORD)packSize - 1;
	if (size == 0)
		return payload <= finalSize ? SAVE_CHUNK_START : SAVE_CHUNK_REJECT;
	if (finalSize != size)
		return SAVE_CHUNK_REJECT;
	if (cur > size)
		return SAVE_CHUNK_REJECT;
	if (payload > size - cur)
		return SAVE_CHUNK_REJECT;
	return SAVE_CHUNK_APPEND;
}

/*=================================================================
	Update download: release assets to disk, SHA-256 checks.

	Updater only: this module links bcrypt.lib, which PlugY.dll must
	never import, so the DLL does not compile it.
=================================================================*/
#pragma once

#include "UpdateCheck.h"

/* Downloads url (https only, redirects followed, see UpdateHttpGetEx) into a
   temp file beside path, renamed by its handle over path only once the whole
   body arrived, so path ends up either complete or untouched. Nothing follows
   a reparse point: a link or folder already at path fails with
   UPDATE_FILE_ERROR. A body over maxBytes fails with UPDATE_TOO_LARGE. flags
   are UpdateHttpGetEx's; the plain call passes 0. Returns an eUpdateResult;
   info may be NULL. */
int UpdateDownloadToFile(const char* url, const char* path, DWORD maxBytes, UpdateHttpInfo* info);
int UpdateDownloadToFileEx(const char* url, const char* path, DWORD maxBytes, DWORD flags, UpdateHttpInfo* info);

/* SHA-256 of a file as 64 lowercase hex digits. False (hex "") when it cannot
   be read or hashed, or path is itself a reparse point. */
bool UpdateSha256File(const char* path, char hex[UPDATE_SHA256_HEX]);

/* Looks fileName up in SHA256SUMS text (len bytes, need not be NUL-terminated):
   lines "<64 hex>  <name>" or "<64 hex> *<name>", LF or CRLF. The name must
   match exactly. hex comes back lowercase; false (hex "") when not listed. */
bool UpdateFindSha256Sum(const char* sums, DWORD len, const char* fileName, char hex[UPDATE_SHA256_HEX]);

///////////////////////// END OF FILE ///////////////////////

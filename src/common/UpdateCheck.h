/*=================================================================
	Update check: asks GitHub for the newest PlugY4Kolbot release.

	Shared by PlugY.dll (main-menu hint) and the updater. Everything
	here is reentrant (no globals), throws nothing and allocates only
	bounded memory, so it can run on a background thread inside
	Game.exe. Network access is WinHTTP only; PlugY.dll delay-loads
	winhttp.dll so a disabled check never loads it.
=================================================================*/
#pragma once

#include <windows.h>
#include "PlugYVersion.h"

#define PLUGY_UPDATE_REPO "theBGuy/PlugY4Kolbot"
#define PLUGY_UPDATE_USER_AGENT "PlugY4Kolbot/" PLUGY_VERSION

// The int results of the calls below. Values are fixed: new ones are only ever appended.
enum eUpdateResult
{
	UPDATE_OK = 0,
	UPDATE_NO_RELEASE,		// the response was valid but no release qualifies
	UPDATE_BAD_ARGUMENT,	// includes any URL that is not https, has userinfo, or has spaces or control characters
	UPDATE_NETWORK_ERROR,	// WinHTTP failure: DNS, connect, TLS/certificate/revocation, timeout
	UPDATE_HTTP_ERROR,		// the final response status was not 200
	UPDATE_TOO_LARGE,		// the body exceeded the size cap
	UPDATE_BAD_RESPONSE,	// malformed, truncated or wrongly typed data, or a redirect off https
	UPDATE_FILE_ERROR,
	UPDATE_NO_MEMORY,
	UPDATE_CACHE_UNAVAILABLE,	// no safe cache folder/file, so GitHub was not asked
	UPDATE_BUSY					// another process held the fetch lock; nothing was checked
};

const char* UpdateResultName(int result);

#define UPDATE_TAG_MAX 64		// any valid tag fits; longer ones are not semver
#define UPDATE_URL_MAX 512
#define UPDATE_NAME_MAX 128
#define UPDATE_SHA256_HEX 65	// 64 lowercase hex digits + NUL
#define UPDATE_NOTES_MAX 4096
#define UPDATE_MAX_ASSETS 16
#define UPDATE_API_MAX_BYTES (2 * 1024 * 1024)
#define UPDATE_HTTP_DEADLINE_MS (3 * 60 * 1000)
#define UPDATE_API_DEADLINE_MS (60 * 1000)	// the releases query; downloads keep UPDATE_HTTP_DEADLINE_MS

// UpdateHttpGetEx flags. Unknown bits fail with UPDATE_BAD_ARGUMENT.
#define UPDATE_HTTP_CHECK_REVOCATION 0x1	// a certificate revocation failure, even "unknown", is a network error


/*------------------------------- Semver --------------------------------
	The grammar of scripts/version-lib.mjs SEMVER_RE: X.Y.Z without leading
	zeros, optionally -alpha.N, -beta.N or -rc.N. Each number is limited
	to 9 digits so it fits a DWORD; longer ones do not parse.
-----------------------------------------------------------------------*/
enum eUpdatePrerelease
{
	UPDATE_PRE_NONE = 0,
	UPDATE_PRE_ALPHA,
	UPDATE_PRE_BETA,
	UPDATE_PRE_RC
};

typedef struct
{
	DWORD major;
	DWORD minor;
	DWORD patch;
	DWORD channel;	// eUpdatePrerelease
	DWORD number;	// the pre-release N, 0 for a release
} UpdateVersion;

// A bare version: "14.3.0", "14.4.0-rc.2". A leading 'v' is refused.
bool UpdateParseVersion(const char* text, UpdateVersion* out);
// A release tag: the same grammar with an optional leading lowercase 'v'.
bool UpdateParseTag(const char* tag, UpdateVersion* out);
// SemVer 2.0.0 precedence: -1, 0 or 1. A pre-release sorts below its release, alpha < beta < rc.
int UpdateCompareVersions(const UpdateVersion* a, const UpdateVersion* b);


/*------------------------------ Releases -------------------------------*/
typedef struct
{
	char name[UPDATE_NAME_MAX];
	char url[UPDATE_URL_MAX];			// browser_download_url, always https
	char sha256[UPDATE_SHA256_HEX];		// from the asset's "sha256:<hex>" digest, "" when absent
} UpdateAsset;

typedef struct
{
	char tag[UPDATE_TAG_MAX];			// tag_name as published, e.g. "v14.4.0"
	UpdateVersion version;				// zero when the tag is not semver (UpdateFindReleaseByTag only)
	bool prerelease;					// GitHub's flag OR a -alpha/-beta/-rc tag
	char pageUrl[UPDATE_URL_MAX];		// html_url; "" when it was not https or did not fit
	char notes[UPDATE_NOTES_MAX];		// body as UTF-8, cut at a character boundary
	bool notesTruncated;
	DWORD nbAssets;
	// Some assets were not stored: over UPDATE_MAX_ASSETS, or a name/url that did not fit or was not https.
	bool assetsOmitted;
	UpdateAsset assets[UPDATE_MAX_ASSETS];
} UpdateRelease;

/* Pure parse + select over a GitHub "list releases" response (json need not be
   NUL-terminated; nothing past len is read). Picks the highest semver tag among
   non-draft releases, not the first listed; non-semver tags are ignored and
   pre-releases count only when includePrerelease. A release missing its draft
   or prerelease field is ignored. Returns UPDATE_OK, UPDATE_NO_RELEASE or
   UPDATE_BAD_RESPONSE; *out is zeroed unless UPDATE_OK. */
int UpdateSelectRelease(const char* json, DWORD len, bool includePrerelease, UpdateRelease* out);
// The same parse, returning the first non-draft release whose tag_name equals tag exactly (semver or not).
int UpdateFindReleaseByTag(const char* json, DWORD len, const char* tag, UpdateRelease* out);


/*------------------------------ Transport ------------------------------*/
typedef struct
{
	DWORD httpStatus;	// final status after redirects, 0 when none was received
	DWORD winError;		// the failing call's GetLastError (WinHTTP or file), 0 when none
} UpdateHttpInfo;

// Receives the body in order; any result other than UPDATE_OK aborts the transfer with that result.
typedef int (*UpdateSinkFn)(void* context, const BYTE* data, DWORD size);

/* HTTPS GET with default certificate validation. Redirects (at most 10) are
   followed only to https URLs; any other target fails with UPDATE_BAD_RESPONSE.
   Each stage gets at most 5 s (resolve, connect, send) or 10 s (response,
   each read), never more than what is left of UPDATE_HTTP_DEADLINE_MS, which
   is checked between every stage. Fails with UPDATE_TOO_LARGE as soon as the
   body passes maxBytes, and with UPDATE_HTTP_ERROR on any final status other
   than 200 (the body is then not read). extraHeaders is NULL or ASCII
   "Name: value" lines joined by CRLF. Sends User-Agent PLUGY_UPDATE_USER_AGENT.
   info may be NULL. Needs about 16 KB of the calling thread's stack.
   UpdateHttpGet is UpdateHttpGetEx with flags 0. */
int UpdateHttpGet(const char* url, const char* extraHeaders, DWORD maxBytes,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info);

/* Pure helpers of UpdateHttpGetEx, exposed for tests. Stage timeouts: resolve,
   connect and send all run inside one WinHttpSendRequest call, so together
   they never exceed leftMs (each at most 5 s); responseMs is one wait for
   headers or one read (at most 10 s, never over leftMs). Every value is at
   least 1 ms, because 0 means "no timeout" to WinHTTP. */
typedef struct
{
	DWORD resolveMs;
	DWORD connectMs;
	DWORD sendMs;
	DWORD responseMs;
} UpdateStageTimeouts;
void UpdateHttpStageTimeouts(DWORD leftMs, UpdateStageTimeouts* out);
/* The URL a redirect from currentUrl (https, no userinfo) to location leads to.
   Only an absolute https URL without userinfo or an absolute path on the same
   host is accepted; anything else, including any space, control character,
   backslash or non-ASCII byte, gives UPDATE_BAD_RESPONSE, as does a result
   over outSize. UPDATE_BAD_ARGUMENT for a bad currentUrl or buffer. */
int UpdateResolveRedirect(const char* currentUrl, const char* location, char* out, DWORD outSize);
int UpdateHttpGetEx(const char* url, const char* extraHeaders, DWORD maxBytes, DWORD flags,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info);

/* GET https://api.github.com/repos/<repo>/releases?per_page=50 (repo is
   "owner/name": letters, digits, '-', '_', '.'), within UPDATE_API_DEADLINE_MS.
   On UPDATE_OK *json holds *len bytes plus a NUL; release it with UpdateFree.
   Bodies over UPDATE_API_MAX_BYTES fail with UPDATE_TOO_LARGE. The plain call
   passes flags 0. */
int UpdateFetchReleases(const char* repo, char** json, DWORD* len, UpdateHttpInfo* info);
int UpdateFetchReleasesEx(const char* repo, DWORD flags, char** json, DWORD* len, UpdateHttpInfo* info);
void UpdateFree(void* p);

// UpdateFetchReleases + UpdateSelectRelease. The plain call passes flags 0.
int UpdateFetchLatest(const char* repo, bool includePrerelease, UpdateRelease* out, UpdateHttpInfo* info);
int UpdateFetchLatestEx(const char* repo, bool includePrerelease, DWORD flags, UpdateRelease* out, UpdateHttpInfo* info);


/*-------------------------------- Cache --------------------------------
	One file per Windows user and channel, %LOCALAPPDATA%\PlugY4Kolbot\
	update-check-<stable|prerelease>.txt, shared by every game instance
	and install of that user so a farm of bots asks GitHub at most once
	per interval and channel. Lines are key=value: checkedAt (unix
	seconds, UTC), channel (stable|prerelease), status (ok|error),
	latestTag, pageUrl.

	Game.exe may run elevated while any process of the same user can
	write that folder, so no cache operation follows a reparse point:
	the folder must be a real directory, held open (which pins it and
	its ancestors) while its files are touched, and a cache, lock or
	temp file that is a reparse point or a directory is refused.
-----------------------------------------------------------------------*/
enum eUpdateChannel
{
	UPDATE_CHANNEL_STABLE = 0,
	UPDATE_CHANNEL_PRERELEASE
};

#define UPDATE_RECHECK_OK_SECONDS (24 * 3600)
#define UPDATE_RECHECK_ERROR_SECONDS 3600

typedef struct
{
	ULONGLONG checkedAt;
	int channel;						// eUpdateChannel
	bool ok;							// false: the last check failed
	char latestTag[UPDATE_TAG_MAX];		// "" when the last good check found no release
	char pageUrl[UPDATE_URL_MAX];
} UpdateCache;

ULONGLONG UpdateUnixTimeNow();
/* Builds the channel's cache path and creates the folder. False when
   %LOCALAPPDATA% is unset, relative or too long, or the folder is not a real
   directory (a file, a junction or a symlink). */
bool UpdateCacheChannelPath(int channel, wchar_t* path, DWORD maxChars);
// UpdateCacheChannelPath(UPDATE_CHANNEL_STABLE, ...).
bool UpdateCacheDefaultPath(wchar_t* path, DWORD maxChars);
// False when the file is missing, unsafe, unreadable or garbled in any way: callers treat that as stale.
bool UpdateCacheRead(const wchar_t* path, UpdateCache* out);
/* Writes a temp file beside path and renames that handle over path, so readers
   never see a partial file. Refuses a path that is a reparse point or a
   directory. Also deletes this module's own temp files for path
   (<path>.<pid>.<tick>.tmp) left over for more than an hour. */
bool UpdateCacheWrite(const wchar_t* path, const UpdateCache* cache);
/* Pure policy. cache is NULL when none could be read. Check when there is no
   cache, the channel changed, checkedAt is in the future (clock moved back),
   UPDATE_RECHECK_OK_SECONDS passed since an ok check, or
   UPDATE_RECHECK_ERROR_SECONDS since a failed one. */
bool UpdateShouldCheck(const UpdateCache* cache, int channel, ULONGLONG now);


/*--------------------------- One-call check ----------------------------*/
typedef struct
{
	bool newer;							// the fields below name a release newer than currentVersion
	char tag[UPDATE_TAG_MAX];
	char version[UPDATE_TAG_MAX];		// tag without its leading 'v', for display
	char pageUrl[UPDATE_URL_MAX];
	bool fetched;						// GitHub was asked during this call
	/* The request's eUpdateResult when fetched; otherwise UPDATE_OK (answered
	   from a fresh cache), UPDATE_BUSY (another process was checking; answered
	   from the cache as it was) or UPDATE_CACHE_UNAVAILABLE (newer is false). */
	int result;
	UpdateHttpInfo info;
} UpdateHint;

/* For PlugY.dll: is a release newer than currentVersion (PLUGY_VERSION) known
   on this channel? Answers from the channel's cache while UpdateShouldCheck
   says it is fresh, otherwise asks GitHub (PLUGY_UPDATE_REPO) and writes the
   cache back. Without a usable cache it never asks: the promise is at most one
   request per interval. A failed request keeps the last known release, so the
   hint survives an outage. Only one process per user and channel fetches at a
   time; the others wait up to 20 s for it, then answer from the cache with
   UPDATE_BUSY. A fetch can take up to UPDATE_API_DEADLINE_MS, so this must
   never run on the game thread; give that thread at least 64 KB of stack.
   Returns out->newer. */
bool UpdateCheckForNewer(int channel, const char* currentVersion, UpdateHint* out);
// The same with an explicit repo and cache path, for tests. A NULL cachePath gives UPDATE_CACHE_UNAVAILABLE.
bool UpdateCheckForNewerEx(const char* repo, const wchar_t* cachePath, int channel,
	const char* currentVersion, UpdateHint* out);

///////////////////////// END OF FILE ///////////////////////

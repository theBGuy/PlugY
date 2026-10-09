/*=================================================================
	Update check: GitHub release query, JSON parsing, semver,
	release selection and the per-user cache. See UpdateCheck.h.

	Everything parsed here comes from the network and is read inside
	Game.exe: every read is bounded by an explicit end pointer, every
	string lands in a fixed buffer, and anything unexpected fails
	closed.
=================================================================*/

#include "UpdateCheck.h"
#include <winhttp.h>
#include <delayimp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")

// Older SDKs lack these; the values are fixed by the WinHTTP ABI.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

#define UPDATE_WIDEN2(x) L##x
#define UPDATE_WIDEN(x) UPDATE_WIDEN2(x)

#define JSON_MAX_DEPTH 32
#define JSON_KEY_MAX 32
#define HTTP_URL_MAX 8192
#define HTTP_HEADERS_MAX 1024
#define HTTP_MAX_REDIRECTS 10
#define CACHE_FILE_MAX 2048
#define FETCH_LOCK_WAIT_MS 20000


const char* UpdateResultName(int result)
{
	switch (result)
	{
	case UPDATE_OK: return "ok";
	case UPDATE_NO_RELEASE: return "no release";
	case UPDATE_BAD_ARGUMENT: return "bad argument";
	case UPDATE_NETWORK_ERROR: return "network error";
	case UPDATE_HTTP_ERROR: return "HTTP error";
	case UPDATE_TOO_LARGE: return "response too large";
	case UPDATE_BAD_RESPONSE: return "bad response";
	case UPDATE_FILE_ERROR: return "file error";
	case UPDATE_NO_MEMORY: return "out of memory";
	case UPDATE_CACHE_UNAVAILABLE: return "cache unavailable";
	case UPDATE_BUSY: return "busy, another game is checking";
	}
	return "unknown";
}


/*=============================== Semver ===============================*/

// 0 or [1-9][0-9]{0,8}: no leading zeros, and at most 9 digits so the value fits a DWORD.
static bool parseVersionNumber(const char** p, DWORD* out)
{
	const char* s = *p;
	if (*s < '0' || *s > '9')
		return false;
	if (*s == '0')
	{
		if (s[1] >= '0' && s[1] <= '9')
			return false;
		*out = 0;
		*p = s + 1;
		return true;
	}
	DWORD value = 0;
	int digits = 0;
	while (*s >= '0' && *s <= '9')
	{
		if (++digits > 9)
			return false;
		value = value * 10 + (*s - '0');
		s++;
	}
	*out = value;
	*p = s;
	return true;
}

bool UpdateParseVersion(const char* text, UpdateVersion* out)
{
	if (!text || !out)
		return false;
	UpdateVersion v;
	memset(&v, 0, sizeof(v));
	const char* p = text;
	if (!parseVersionNumber(&p, &v.major) || *p != '.')
		return false;
	p++;
	if (!parseVersionNumber(&p, &v.minor) || *p != '.')
		return false;
	p++;
	if (!parseVersionNumber(&p, &v.patch))
		return false;
	if (*p == '-')
	{
		p++;
		if (!strncmp(p, "alpha.", 6)) { v.channel = UPDATE_PRE_ALPHA; p += 6; }
		else if (!strncmp(p, "beta.", 5)) { v.channel = UPDATE_PRE_BETA; p += 5; }
		else if (!strncmp(p, "rc.", 3)) { v.channel = UPDATE_PRE_RC; p += 3; }
		else return false;
		if (!parseVersionNumber(&p, &v.number))
			return false;
	}
	if (*p)
		return false;
	*out = v;
	return true;
}

bool UpdateParseTag(const char* tag, UpdateVersion* out)
{
	if (!tag)
		return false;
	return UpdateParseVersion(tag[0] == 'v' ? tag + 1 : tag, out);
}

// A release outranks every pre-release of the same X.Y.Z.
static DWORD channelRank(DWORD channel)
{
	return channel == UPDATE_PRE_NONE ? UPDATE_PRE_RC + 1 : channel;
}

static int compareDword(DWORD a, DWORD b)
{
	return a < b ? -1 : a > b ? 1 : 0;
}

int UpdateCompareVersions(const UpdateVersion* a, const UpdateVersion* b)
{
	int c = compareDword(a->major, b->major);
	if (!c) c = compareDword(a->minor, b->minor);
	if (!c) c = compareDword(a->patch, b->patch);
	if (!c) c = compareDword(channelRank(a->channel), channelRank(b->channel));
	if (!c) c = compareDword(a->number, b->number);
	return c;
}


/*============================ JSON reader =============================*/

typedef struct
{
	const char* p;
	const char* end;	// never dereferenced: every read checks p < end first
	int depth;
} JsonCursor;

// A string being decoded into a fixed buffer. buf NULL: validate and discard.
typedef struct
{
	char* buf;
	DWORD cap;
	DWORD len;
	bool truncated;
} JsonText;

static void textInit(JsonText* t, char* buf, DWORD cap)
{
	t->buf = buf;
	t->cap = buf ? cap : 0;
	t->len = 0;
	t->truncated = false;
	if (buf)
		buf[0] = 0;
}

// Writes whole characters only; once one does not fit, nothing later is kept either.
static void textPut(JsonText* t, const char* bytes, DWORD n)
{
	if (!t->buf || t->truncated)
		return;
	if (n > t->cap - 1 - t->len)
	{
		t->truncated = true;
		return;
	}
	memcpy(t->buf + t->len, bytes, n);
	t->len += n;
}

static void textPutCodePoint(JsonText* t, DWORD cp)
{
	char b[4];
	DWORD n;
	if (cp < 0x80) { b[0] = (char)cp; n = 1; }
	else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
	else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
	else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
	textPut(t, b, n);
}

// Raw UTF-8 is copied byte by byte, so a cut can split a character; its partial bytes are dropped.
static void textFinish(JsonText* t)
{
	if (!t->buf)
		return;
	if (t->truncated)
	{
		DWORD i = t->len;
		DWORD continuation = 0;
		while (i > 0 && continuation < 3 && ((BYTE)t->buf[i - 1] & 0xC0) == 0x80)
		{
			i--;
			continuation++;
		}
		if (i > 0)
		{
			BYTE lead = (BYTE)t->buf[i - 1];
			DWORD need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
			if (need > 1 && continuation + 1 < need)
				t->len = i - 1;
		}
	}
	t->buf[t->len] = 0;
}

static void jsonSkipSpace(JsonCursor* c)
{
	while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
		c->p++;
}

// Consumes ch, after optional whitespace.
static bool jsonEat(JsonCursor* c, char ch)
{
	jsonSkipSpace(c);
	if (c->p < c->end && *c->p == ch)
	{
		c->p++;
		return true;
	}
	return false;
}

static int hexDigit(char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	return -1;
}

static bool jsonHex4(JsonCursor* c, DWORD* out)
{
	if (c->end - c->p < 4)
		return false;
	DWORD value = 0;
	for (int i = 0; i < 4; i++)
	{
		int h = hexDigit(c->p[i]);
		if (h < 0)
			return false;
		value = (value << 4) | h;
	}
	c->p += 4;
	*out = value;
	return true;
}

/* The cursor is on the opening quote. Malformed: unterminated, a raw control
   character, an unknown escape, an unpaired surrogate, or \u0000 (it would cut
   the C string short and let "v1.0.0\u0000junk" read as a valid tag). */
static bool jsonString(JsonCursor* c, JsonText* t)
{
	if (c->p >= c->end || *c->p != '"')
		return false;
	c->p++;
	while (c->p < c->end)
	{
		char ch = *c->p++;
		if (ch == '"')
		{
			textFinish(t);
			return true;
		}
		if ((BYTE)ch < 0x20)
			return false;
		if (ch != '\\')
		{
			textPut(t, &ch, 1);
			continue;
		}
		if (c->p >= c->end)
			return false;
		char e = *c->p++;
		char out;
		switch (e)
		{
		case '"': case '\\': case '/': out = e; break;
		case 'b': out = '\b'; break;
		case 'f': out = '\f'; break;
		case 'n': out = '\n'; break;
		case 'r': out = '\r'; break;
		case 't': out = '\t'; break;
		case 'u':
		{
			DWORD cp;
			if (!jsonHex4(c, &cp) || cp == 0 || (cp >= 0xDC00 && cp <= 0xDFFF))
				return false;
			if (cp >= 0xD800 && cp <= 0xDBFF)
			{
				DWORD low;
				if (c->end - c->p < 2 || c->p[0] != '\\' || c->p[1] != 'u')
					return false;
				c->p += 2;
				if (!jsonHex4(c, &low) || low < 0xDC00 || low > 0xDFFF)
					return false;
				cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
			}
			textPutCodePoint(t, cp);
			continue;
		}
		default:
			return false;
		}
		textPut(t, &out, 1);
	}
	return false;
}

static bool isDigit(char ch)
{
	return ch >= '0' && ch <= '9';
}

static bool jsonSkipNumber(JsonCursor* c)
{
	const char* p = c->p;
	if (p < c->end && *p == '-')
		p++;
	if (p >= c->end)
		return false;
	if (*p == '0')
		p++;
	else if (*p >= '1' && *p <= '9')
		while (p < c->end && isDigit(*p))
			p++;
	else
		return false;
	if (p < c->end && *p == '.')
	{
		p++;
		if (p >= c->end || !isDigit(*p))
			return false;
		while (p < c->end && isDigit(*p))
			p++;
	}
	if (p < c->end && (*p == 'e' || *p == 'E'))
	{
		p++;
		if (p < c->end && (*p == '+' || *p == '-'))
			p++;
		if (p >= c->end || !isDigit(*p))
			return false;
		while (p < c->end && isDigit(*p))
			p++;
	}
	c->p = p;
	return true;
}

static bool jsonLiteral(JsonCursor* c, const char* word)
{
	size_t n = strlen(word);
	if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n))
		return false;
	c->p += n;
	return true;
}

// Opens '[' or '{'. The depth cap also bounds jsonSkipValue's recursion.
static bool jsonOpen(JsonCursor* c, char ch)
{
	if (!jsonEat(c, ch))
		return false;
	return ++c->depth <= JSON_MAX_DEPTH;
}

// Before each element: 1 = an element follows, 0 = the array closed, -1 = malformed.
static int jsonArrayNext(JsonCursor* c, bool* first)
{
	if (*first)
	{
		*first = false;
		if (!jsonEat(c, ']'))
			return 1;
	}
	else if (jsonEat(c, ','))
		return 1;
	else if (!jsonEat(c, ']'))
		return -1;
	c->depth--;
	return 0;
}

/* Before each member: 1 = its key is in key (or discarded when key is NULL) and
   the ':' consumed, 0 = the object closed, -1 = malformed. A key too long for
   the buffer comes back empty, so it matches no field. */
static int jsonObjectNext(JsonCursor* c, bool* first, char* key, DWORD keyCap)
{
	if (*first)
	{
		*first = false;
		if (jsonEat(c, '}'))
		{
			c->depth--;
			return 0;
		}
	}
	else if (!jsonEat(c, ','))
	{
		if (!jsonEat(c, '}'))
			return -1;
		c->depth--;
		return 0;
	}
	JsonText t;
	textInit(&t, key, keyCap);
	jsonSkipSpace(c);
	if (!jsonString(c, &t) || !jsonEat(c, ':'))
		return -1;
	if (key && t.truncated)
		key[0] = 0;
	return 1;
}

static bool jsonSkipValue(JsonCursor* c)
{
	jsonSkipSpace(c);
	if (c->p >= c->end)
		return false;
	switch (*c->p)
	{
	case '"':
	{
		JsonText t;
		textInit(&t, NULL, 0);
		return jsonString(c, &t);
	}
	case '{':
	{
		if (!jsonOpen(c, '{'))
			return false;
		bool first = true;
		for (;;)
		{
			int r = jsonObjectNext(c, &first, NULL, 0);
			if (r <= 0)
				return r == 0;
			if (!jsonSkipValue(c))
				return false;
		}
	}
	case '[':
	{
		if (!jsonOpen(c, '['))
			return false;
		bool first = true;
		for (;;)
		{
			int r = jsonArrayNext(c, &first);
			if (r <= 0)
				return r == 0;
			if (!jsonSkipValue(c))
				return false;
		}
	}
	case 't': return jsonLiteral(c, "true");
	case 'f': return jsonLiteral(c, "false");
	case 'n': return jsonLiteral(c, "null");
	}
	return jsonSkipNumber(c);
}

static bool jsonReadString(JsonCursor* c, JsonText* t)
{
	jsonSkipSpace(c);
	return jsonString(c, t);
}

// A string, or null, which leaves t empty.
static bool jsonReadStringOrNull(JsonCursor* c, JsonText* t, bool* isNull)
{
	jsonSkipSpace(c);
	*isNull = c->p < c->end && *c->p == 'n';
	if (*isNull)
		return jsonLiteral(c, "null");
	return jsonString(c, t);
}

static bool jsonReadBool(JsonCursor* c, bool* out)
{
	jsonSkipSpace(c);
	if (c->p < c->end && *c->p == 't')
	{
		*out = true;
		return jsonLiteral(c, "true");
	}
	*out = false;
	return jsonLiteral(c, "false");
}


/*============================== Releases ==============================*/

typedef struct
{
	char tag[UPDATE_TAG_MAX];
	bool tagUsable;		// present and not truncated
	int draft;			// -1 when absent
	int prerelease;		// -1 when absent
} ReleaseHead;

static bool isHttpsUrl(const char* s)
{
	return !strncmp(s, "https://", 8) && s[8];
}

// "sha256:" + exactly 64 hex digits, stored lowercase; anything else leaves hex empty.
static void parseDigest(const char* digest, char* hex)
{
	hex[0] = 0;
	if (strncmp(digest, "sha256:", 7))
		return;
	const char* h = digest + 7;
	for (int i = 0; i < 64; i++)
		if (hexDigit(h[i]) < 0)
			return;
	if (h[64])
		return;
	for (int i = 0; i < 64; i++)
		hex[i] = (h[i] >= 'A' && h[i] <= 'F') ? (char)(h[i] - 'A' + 'a') : h[i];
	hex[64] = 0;
}

// One assets[] entry. full NULL: validate only. Field types are checked the same way either way.
static bool parseAsset(JsonCursor* c, UpdateRelease* full)
{
	if (!jsonOpen(c, '{'))
		return false;
	UpdateAsset asset;
	memset(&asset, 0, sizeof(asset));
	char digest[80] = "";
	bool nameOk = false;
	bool urlOk = false;
	bool digestOk = false;
	char key[JSON_KEY_MAX];
	bool first = true;
	for (;;)
	{
		int r = jsonObjectNext(c, &first, key, sizeof(key));
		if (r < 0)
			return false;
		if (r == 0)
			break;
		JsonText t;
		if (!strcmp(key, "name"))
		{
			textInit(&t, asset.name, sizeof(asset.name));
			if (!jsonReadString(c, &t))
				return false;
			nameOk = !t.truncated && asset.name[0];
		}
		else if (!strcmp(key, "browser_download_url"))
		{
			textInit(&t, asset.url, sizeof(asset.url));
			if (!jsonReadString(c, &t))
				return false;
			urlOk = !t.truncated && isHttpsUrl(asset.url);
		}
		else if (!strcmp(key, "digest"))
		{
			bool isNull;
			textInit(&t, digest, sizeof(digest));
			if (!jsonReadStringOrNull(c, &t, &isNull))
				return false;
			digestOk = !isNull && !t.truncated;
		}
		else if (!jsonSkipValue(c))
			return false;
	}
	if (!full)
		return true;
	// A cut name or URL would name the wrong file, so such an asset is dropped rather than kept damaged.
	if (!nameOk || !urlOk || full->nbAssets >= UPDATE_MAX_ASSETS)
	{
		full->assetsOmitted = true;
		return true;
	}
	if (digestOk)
		parseDigest(digest, asset.sha256);
	full->assets[full->nbAssets++] = asset;
	return true;
}

// One release object. full NULL: only head is filled, but every field is type-checked the same way.
static bool parseRelease(JsonCursor* c, ReleaseHead* head, UpdateRelease* full)
{
	memset(head, 0, sizeof(*head));
	head->draft = -1;
	head->prerelease = -1;
	if (!jsonOpen(c, '{'))
		return false;
	char key[JSON_KEY_MAX];
	bool first = true;
	for (;;)
	{
		int r = jsonObjectNext(c, &first, key, sizeof(key));
		if (r < 0)
			return false;
		if (r == 0)
			return true;
		JsonText t;
		if (!strcmp(key, "tag_name"))
		{
			textInit(&t, head->tag, sizeof(head->tag));
			if (!jsonReadString(c, &t))
				return false;
			head->tagUsable = !t.truncated;
		}
		else if (!strcmp(key, "draft") || !strcmp(key, "prerelease"))
		{
			bool value;
			if (!jsonReadBool(c, &value))
				return false;
			*(key[0] == 'd' ? &head->draft : &head->prerelease) = value ? 1 : 0;
		}
		else if (!strcmp(key, "html_url"))
		{
			textInit(&t, full ? full->pageUrl : NULL, UPDATE_URL_MAX);
			if (!jsonReadString(c, &t))
				return false;
			if (full && (t.truncated || !isHttpsUrl(full->pageUrl)))
				full->pageUrl[0] = 0;
		}
		else if (!strcmp(key, "body"))
		{
			bool isNull;
			textInit(&t, full ? full->notes : NULL, UPDATE_NOTES_MAX);
			if (!jsonReadStringOrNull(c, &t, &isNull))
				return false;
			if (full)
				full->notesTruncated = t.truncated;
		}
		else if (!strcmp(key, "assets"))
		{
			if (!jsonOpen(c, '['))
				return false;
			if (full)
			{
				full->nbAssets = 0;
				full->assetsOmitted = false;
			}
			bool firstAsset = true;
			for (;;)
			{
				int ra = jsonArrayNext(c, &firstAsset);
				if (ra < 0)
					return false;
				if (ra == 0)
					break;
				if (!parseAsset(c, full))
					return false;
			}
		}
		else if (!jsonSkipValue(c))
			return false;
	}
}

/* Pass 1 validates the whole document and picks the release by its head only;
   pass 2 decodes just that release into out, so no second ~16 KB release ever
   sits on the caller's stack. wantedTag NULL: newest semver release. */
static int selectRelease(const char* json, DWORD len, bool includePrerelease, const char* wantedTag, UpdateRelease* out)
{
	memset(out, 0, sizeof(*out));
	if (!json)
		return UPDATE_BAD_RESPONSE;

	JsonCursor c = { json, json + len, 0 };
	ReleaseHead head;
	int bestIndex = -1;
	UpdateVersion bestVersion;
	memset(&bestVersion, 0, sizeof(bestVersion));
	bool bestPrerelease = false;
	if (!jsonOpen(&c, '['))
		return UPDATE_BAD_RESPONSE;
	bool first = true;
	for (int index = 0; ; index++)
	{
		int r = jsonArrayNext(&c, &first);
		if (r < 0)
			return UPDATE_BAD_RESPONSE;
		if (r == 0)
			break;
		if (!parseRelease(&c, &head, NULL))
			return UPDATE_BAD_RESPONSE;
		if (!head.tagUsable || head.draft != 0 || head.prerelease < 0)
			continue;
		UpdateVersion version;
		bool semver = UpdateParseTag(head.tag, &version);
		if (!semver)
			memset(&version, 0, sizeof(version));
		// Either signal is enough, so a -rc tag published without GitHub's flag never reaches the stable channel.
		bool prerelease = head.prerelease == 1 || (semver && version.channel != UPDATE_PRE_NONE);
		if (wantedTag)
		{
			if (bestIndex >= 0 || strcmp(head.tag, wantedTag))
				continue;
		}
		else if (!semver || (prerelease && !includePrerelease)
			|| (bestIndex >= 0 && UpdateCompareVersions(&version, &bestVersion) <= 0))
			continue;
		bestIndex = index;
		bestVersion = version;
		bestPrerelease = prerelease;
	}
	jsonSkipSpace(&c);
	if (c.p != c.end)
		return UPDATE_BAD_RESPONSE;
	if (bestIndex < 0)
		return UPDATE_NO_RELEASE;

	c.p = json;
	c.depth = 0;
	first = true;
	bool decoded = false;
	if (jsonOpen(&c, '['))
		for (int index = 0; jsonArrayNext(&c, &first) == 1; index++)
		{
			if (index < bestIndex)
			{
				if (!jsonSkipValue(&c))
					break;
				continue;
			}
			decoded = parseRelease(&c, &head, out);
			break;
		}
	// Pass 1 already accepted these bytes; this only guards against the two passes ever disagreeing.
	if (!decoded)
	{
		memset(out, 0, sizeof(*out));
		return UPDATE_BAD_RESPONSE;
	}
	memcpy(out->tag, head.tag, sizeof(out->tag));
	out->version = bestVersion;
	out->prerelease = bestPrerelease;
	return UPDATE_OK;
}

int UpdateSelectRelease(const char* json, DWORD len, bool includePrerelease, UpdateRelease* out)
{
	if (!out)
		return UPDATE_BAD_ARGUMENT;
	return selectRelease(json, len, includePrerelease, NULL, out);
}

int UpdateFindReleaseByTag(const char* json, DWORD len, const char* tag, UpdateRelease* out)
{
	if (!out)
		return UPDATE_BAD_ARGUMENT;
	if (!tag || !tag[0])
	{
		memset(out, 0, sizeof(*out));
		return UPDATE_BAD_ARGUMENT;
	}
	return selectRelease(json, len, true, tag, out);
}


/*============================== Transport =============================*/

static bool formatWide(wchar_t* out, DWORD maxChars, const wchar_t* format, ...)
{
	va_list args;
	va_start(args, format);
	int n = _vsnwprintf(out, maxChars, format, args);
	va_end(args);
	out[maxChars - 1] = 0;
	return n >= 0 && (DWORD)n < maxChars;
}

static void closeInternet(HINTERNET* h)
{
	if (*h)
	{
		WinHttpCloseHandle(*h);
		*h = NULL;
	}
}

static DWORD remainingMs(DWORD start, DWORD deadlineMs)
{
	DWORD elapsed = GetTickCount() - start;
	return elapsed >= deadlineMs ? 0 : deadlineMs - elapsed;
}

static DWORD atLeastOne(DWORD ms)
{
	return ms ? ms : 1;
}

void UpdateHttpStageTimeouts(DWORD leftMs, UpdateStageTimeouts* out)
{
	// Below 15 s, the three stages of WinHttpSendRequest share what is left in equal thirds.
	DWORD share = leftMs >= 15000 ? 5000 : leftMs / 3;
	out->resolveMs = atLeastOne(share);
	out->connectMs = atLeastOne(share);
	out->sendMs = atLeastOne(share);
	out->responseMs = atLeastOne(leftMs < 10000 ? leftMs : 10000);
}

static bool isRedirectStatus(DWORD status)
{
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Spaces, controls (CR/LF would split a header), backslashes and non-ASCII never appear in a URL this module follows.
static bool hasUnsafeUrlChar(const char* s)
{
	for (; *s; s++)
		if ((BYTE)*s <= 0x20 || (BYTE)*s >= 0x7F || *s == '\\')
			return true;
	return false;
}

/* Length of the authority after "https://", or 0 when it is unusable: empty, a
   port without a host, over 255 characters, or carrying userinfo ('@'), which
   could make a URL read as one host to a person and another to WinHTTP. */
static DWORD httpsAuthorityLength(const char* url)
{
	if (_strnicmp(url, "https://", 8))
		return 0;
	const char* authority = url + 8;
	DWORD n = 0;
	while (authority[n] && authority[n] != '/' && authority[n] != '?' && authority[n] != '#')
	{
		if (authority[n] == '@')
			return 0;
		n++;
	}
	return n && n <= 255 && authority[0] != ':' ? n : 0;
}

static bool isSafeHttpsUrl(const char* url)
{
	return !hasUnsafeUrlChar(url) && httpsAuthorityLength(url);
}

/* Only an absolute https URL or an absolute path on the current host is
   followed. Scheme-relative ("//host"), relative ("next", "?x", "../x") and
   every other scheme are refused on purpose: GitHub only sends the first two,
   and refusing the rest keeps the resolver too small to get wrong. */
int UpdateResolveRedirect(const char* currentUrl, const char* location, char* out, DWORD outSize)
{
	if (!out || !outSize)
		return UPDATE_BAD_ARGUMENT;
	out[0] = 0;
	if (!currentUrl || !location || !isSafeHttpsUrl(currentUrl))
		return UPDATE_BAD_ARGUMENT;
	if (!location[0] || hasUnsafeUrlChar(location))
		return UPDATE_BAD_RESPONSE;
	int n;
	if (!_strnicmp(location, "https://", 8))
	{
		if (!httpsAuthorityLength(location))
			return UPDATE_BAD_RESPONSE;
		n = _snprintf(out, outSize, "%s", location);
	}
	else if (location[0] == '/' && location[1] != '/')
		n = _snprintf(out, outSize, "https://%.*s%s", (int)httpsAuthorityLength(currentUrl), currentUrl + 8, location);
	else
		return UPDATE_BAD_RESPONSE;
	if (n < 0 || (DWORD)n >= outSize)
	{
		out[0] = 0;
		return UPDATE_BAD_RESPONSE;
	}
	return UPDATE_OK;
}

// The URL being requested, the next hop's, the Location header, and their wide forms: kilobytes, so on the heap.
typedef struct
{
	char url[HTTP_URL_MAX];
	char nextUrl[HTTP_URL_MAX];
	char location[HTTP_URL_MAX];
	wchar_t wideUrl[HTTP_URL_MAX];
	wchar_t wideText[HTTP_URL_MAX];	// the request's object name, later the Location header
} HttpBuffers;

static int httpGetInner(const char* url, const char* extraHeaders, DWORD maxBytes, DWORD flags, DWORD deadlineMs,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info)
{
	DWORD start = GetTickCount();
	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	HttpBuffers* b = NULL;
	wchar_t host[256];
	wchar_t headers[HTTP_HEADERS_MAX];
	BYTE buffer[8192];
	URL_COMPONENTS parts;
	UpdateStageTimeouts timeouts;
	DWORD value, size, total, remaining;
	DWORD status = 0;
	int result = UPDATE_BAD_ARGUMENT;

	if (!url || !sink || !maxBytes || (flags & ~UPDATE_HTTP_CHECK_REVOCATION) || !isSafeHttpsUrl(url)
		|| strlen(url) >= HTTP_URL_MAX)
		return UPDATE_BAD_ARGUMENT;
	headers[0] = 0;
	if (extraHeaders && extraHeaders[0] && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, extraHeaders, -1, headers, HTTP_HEADERS_MAX))
		return UPDATE_BAD_ARGUMENT;
	b = (HttpBuffers*)malloc(sizeof(HttpBuffers));
	if (!b)
		return UPDATE_NO_MEMORY;
	strcpy(b->url, url);

	// The automatic proxy (Windows 8.1+) follows the system settings; older systems refuse it.
	hSession = WinHttpOpen(UPDATE_WIDEN(PLUGY_UPDATE_USER_AGENT), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		hSession = WinHttpOpen(UPDATE_WIDEN(PLUGY_UPDATE_USER_AGENT), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		goto failed;
	// GitHub requires TLS 1.2, which Windows 7 WinHTTP only offers when asked; systems without TLS 1.3 refuse that flag.
	value = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
	if (!WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &value, sizeof(value)))
	{
		value = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
		WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &value, sizeof(value));
	}

	// Redirects are followed here, one hop at a time, so every target is checked and the deadline holds between hops.
	for (int hop = 0; ; hop++)
	{
		const wchar_t* object;
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b->url, -1, b->wideUrl, HTTP_URL_MAX))
		{
			result = hop ? UPDATE_BAD_RESPONSE : UPDATE_BAD_ARGUMENT;
			goto done;
		}
		// A fragment is never sent.
		wchar_t* fragment = wcschr(b->wideUrl, L'#');
		if (fragment)
			*fragment = 0;
		memset(&parts, 0, sizeof(parts));
		parts.dwStructSize = sizeof(parts);
		parts.dwHostNameLength = (DWORD)-1;
		parts.dwUserNameLength = (DWORD)-1;
		parts.dwPasswordLength = (DWORD)-1;
		parts.dwUrlPathLength = (DWORD)-1;
		parts.dwExtraInfoLength = (DWORD)-1;
		if (!WinHttpCrackUrl(b->wideUrl, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS
			|| parts.dwUserNameLength || parts.dwPasswordLength || !parts.dwHostNameLength || parts.dwHostNameLength >= 256)
		{
			result = hop ? UPDATE_BAD_RESPONSE : UPDATE_BAD_ARGUMENT;
			goto done;
		}
		memcpy(host, parts.lpszHostName, parts.dwHostNameLength * sizeof(wchar_t));
		host[parts.dwHostNameLength] = 0;
		// Path and query are contiguous in wideUrl. Without a path, "https://host?x=1" still sends its query, as "/?x=1".
		if (parts.dwUrlPathLength)
			object = parts.lpszUrlPath;
		else
		{
			if (!formatWide(b->wideText, HTTP_URL_MAX, L"/%s", parts.dwExtraInfoLength ? parts.lpszExtraInfo : L""))
			{
				result = hop ? UPDATE_BAD_RESPONSE : UPDATE_BAD_ARGUMENT;
				goto done;
			}
			object = b->wideText;
		}

		hConnect = WinHttpConnect(hSession, host, parts.nPort, 0);
		if (!hConnect)
			goto failed;
		hRequest = WinHttpOpenRequest(hConnect, L"GET", object, NULL, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (!hRequest)
			goto failed;
		value = WINHTTP_DISABLE_REDIRECTS;
		if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &value, sizeof(value)))
			goto failed;
		// Never offer Windows credentials to a server.
		value = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
		WinHttpSetOption(hRequest, WINHTTP_OPTION_AUTOLOGON_POLICY, &value, sizeof(value));
		if (flags & UPDATE_HTTP_CHECK_REVOCATION)
		{
			value = WINHTTP_ENABLE_SSL_REVOCATION;
			if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_ENABLE_FEATURE, &value, sizeof(value)))
				goto failed;
		}
		if (headers[0] && !WinHttpAddRequestHeaders(hRequest, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD))
			goto failed;

		// Each blocking call gets only what is left of the deadline, recomputed right before it.
		remaining = remainingMs(start, deadlineMs);
		if (!remaining)
			goto timedOut;
		UpdateHttpStageTimeouts(remaining, &timeouts);
		if (!WinHttpSetTimeouts(hRequest, timeouts.resolveMs, timeouts.connectMs, timeouts.sendMs, timeouts.responseMs))
			goto failed;
		if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
			goto failed;
		remaining = remainingMs(start, deadlineMs);
		if (!remaining)
			goto timedOut;
		// The wait for the response headers is its own timeout, 90 s by default.
		UpdateHttpStageTimeouts(remaining, &timeouts);
		value = timeouts.responseMs;
		if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &value, sizeof(value))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &value, sizeof(value)))
			goto failed;
		if (!WinHttpReceiveResponse(hRequest, NULL))
			goto failed;
		if (!remainingMs(start, deadlineMs))
			goto timedOut;
		size = sizeof(status);
		if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
			goto failed;
		if (info)
			info->httpStatus = status;
		if (!isRedirectStatus(status))
			break;
		if (hop >= HTTP_MAX_REDIRECTS)
		{
			result = UPDATE_HTTP_ERROR;
			goto done;
		}
		size = HTTP_URL_MAX * sizeof(wchar_t);
		if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
			b->wideText, &size, WINHTTP_NO_HEADER_INDEX)
			|| !WideCharToMultiByte(CP_UTF8, 0, b->wideText, -1, b->location, HTTP_URL_MAX, NULL, NULL)
			|| UpdateResolveRedirect(b->url, b->location, b->nextUrl, HTTP_URL_MAX) != UPDATE_OK)
		{
			result = UPDATE_BAD_RESPONSE;
			goto done;
		}
		strcpy(b->url, b->nextUrl);
		closeInternet(&hRequest);
		closeInternet(&hConnect);
	}

	if (status != 200)
	{
		result = UPDATE_HTTP_ERROR;
		goto done;
	}
	size = sizeof(value);
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &value, &size, WINHTTP_NO_HEADER_INDEX) && value > maxBytes)
	{
		result = UPDATE_TOO_LARGE;
		goto done;
	}

	total = 0;
	for (;;)
	{
		DWORD got = 0;
		remaining = remainingMs(start, deadlineMs);
		if (!remaining)
			goto timedOut;
		UpdateHttpStageTimeouts(remaining, &timeouts);
		value = timeouts.responseMs;
		WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &value, sizeof(value));
		if (!WinHttpReadData(hRequest, buffer, sizeof(buffer), &got))
			goto failed;
		if (!got)
			break;
		if (got > maxBytes - total)
		{
			result = UPDATE_TOO_LARGE;
			goto done;
		}
		total += got;
		result = sink(context, buffer, got);
		if (result != UPDATE_OK)
			goto done;
	}
	result = UPDATE_OK;
	goto done;

timedOut:
	if (info)
		info->winError = ERROR_TIMEOUT;
	result = UPDATE_NETWORK_ERROR;
	goto done;
failed:
	if (info)
		info->winError = GetLastError();
	result = UPDATE_NETWORK_ERROR;
done:
	closeInternet(&hRequest);
	closeInternet(&hConnect);
	closeInternet(&hSession);
	free(b);
	return result;
}

// PlugY.dll delay-loads winhttp.dll: a system without it raises these on the first call, which must not crash the game.
static int delayLoadFilter(DWORD code)
{
	return code == VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND)
		|| code == VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
		? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int httpGet(const char* url, const char* extraHeaders, DWORD maxBytes, DWORD flags, DWORD deadlineMs,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info)
{
	if (info)
		memset(info, 0, sizeof(*info));
	__try
	{
		return httpGetInner(url, extraHeaders, maxBytes, flags, deadlineMs, sink, context, info);
	}
	__except (delayLoadFilter(GetExceptionCode()))
	{
		if (info)
			info->winError = ERROR_MOD_NOT_FOUND;
		return UPDATE_NETWORK_ERROR;
	}
}

int UpdateHttpGetEx(const char* url, const char* extraHeaders, DWORD maxBytes, DWORD flags,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info)
{
	return httpGet(url, extraHeaders, maxBytes, flags, UPDATE_HTTP_DEADLINE_MS, sink, context, info);
}

int UpdateHttpGet(const char* url, const char* extraHeaders, DWORD maxBytes,
	UpdateSinkFn sink, void* context, UpdateHttpInfo* info)
{
	return UpdateHttpGetEx(url, extraHeaders, maxBytes, 0, sink, context, info);
}

typedef struct
{
	char* data;
	DWORD size;
	DWORD cap;		// usable bytes; one more is always allocated for the NUL
	DWORD max;
} MemorySink;

static int memorySinkWrite(void* context, const BYTE* data, DWORD size)
{
	MemorySink* m = (MemorySink*)context;
	if (size > m->max - m->size)
		return UPDATE_TOO_LARGE;
	if (size > m->cap - m->size)
	{
		DWORD need = m->size + size;
		DWORD cap = m->cap ? m->cap : 64 * 1024;
		while (cap < need)
			cap = cap > m->max / 2 ? m->max : cap * 2;
		char* grown = (char*)realloc(m->data, cap + 1);
		if (!grown)
			return UPDATE_NO_MEMORY;
		m->data = grown;
		m->cap = cap;
	}
	memcpy(m->data + m->size, data, size);
	m->size += size;
	return UPDATE_OK;
}

// owner/name, as GitHub allows them: letters, digits, '-', '_', '.'; never "." or "..".
static bool isRepoName(const char* repo)
{
	int slashes = 0;
	int partLen = 0;
	int dots = 0;
	for (const char* p = repo; ; p++)
	{
		if (*p == '/' || !*p)
		{
			if (!partLen || partLen > 100 || dots == partLen)
				return false;
			if (!*p)
				return slashes == 1;
			slashes++;
			partLen = 0;
			dots = 0;
			continue;
		}
		bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || isDigit(*p)
			|| *p == '-' || *p == '_' || *p == '.';
		if (!ok)
			return false;
		if (*p == '.')
			dots++;
		partLen++;
	}
}

int UpdateFetchReleasesEx(const char* repo, DWORD flags, char** json, DWORD* len, UpdateHttpInfo* info)
{
	if (info)
		memset(info, 0, sizeof(*info));
	if (!json || !len)
		return UPDATE_BAD_ARGUMENT;
	*json = NULL;
	*len = 0;
	if (!repo || !isRepoName(repo))
		return UPDATE_BAD_ARGUMENT;
	char url[300];
	// 50 releases (about 15 KB each with their asset objects) stay far below UPDATE_API_MAX_BYTES as the
	// repo grows, and reach back further than any run of pre-releases between two stable releases.
	int n = _snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=50", repo);
	if (n < 0 || n >= (int)sizeof(url))
		return UPDATE_BAD_ARGUMENT;

	MemorySink m = { NULL, 0, 0, UPDATE_API_MAX_BYTES };
	int result = httpGet(url, "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28",
		UPDATE_API_MAX_BYTES, flags, UPDATE_API_DEADLINE_MS, memorySinkWrite, &m, info);
	if (result == UPDATE_OK && !m.data)
	{
		m.data = (char*)malloc(1);
		if (!m.data)
			result = UPDATE_NO_MEMORY;
	}
	if (result != UPDATE_OK)
	{
		free(m.data);
		return result;
	}
	m.data[m.size] = 0;
	*json = m.data;
	*len = m.size;
	return UPDATE_OK;
}

int UpdateFetchReleases(const char* repo, char** json, DWORD* len, UpdateHttpInfo* info)
{
	return UpdateFetchReleasesEx(repo, 0, json, len, info);
}

void UpdateFree(void* p)
{
	free(p);
}

int UpdateFetchLatestEx(const char* repo, bool includePrerelease, DWORD flags, UpdateRelease* out, UpdateHttpInfo* info)
{
	if (!out)
		return UPDATE_BAD_ARGUMENT;
	memset(out, 0, sizeof(*out));
	char* json;
	DWORD len;
	int result = UpdateFetchReleasesEx(repo, flags, &json, &len, info);
	if (result != UPDATE_OK)
		return result;
	result = UpdateSelectRelease(json, len, includePrerelease, out);
	UpdateFree(json);
	return result;
}

int UpdateFetchLatest(const char* repo, bool includePrerelease, UpdateRelease* out, UpdateHttpInfo* info)
{
	return UpdateFetchLatestEx(repo, includePrerelease, 0, out, info);
}


/*========================== Reparse-safe files ========================*/

static bool isAbsolutePath(const wchar_t* p)
{
	bool drive = ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':'
		&& (p[2] == L'\\' || p[2] == L'/');
	return drive || (p[0] == L'\\' && p[1] == L'\\');
}

static const DWORD unsafeAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;

// The handle's own file, never a link target: callers open with FILE_FLAG_OPEN_REPARSE_POINT.
static bool handleIsPlainFile(HANDLE h)
{
	BY_HANDLE_FILE_INFORMATION fi;
	return GetFileInformationByHandle(h, &fi) && !(fi.dwFileAttributes & unsafeAttributes);
}

// GetFileAttributes reports a link itself, not its target. Missing or unreadable is not "unsafe" here.
static bool pathIsDirOrReparse(const wchar_t* path)
{
	DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & unsafeAttributes);
}

/* Opens dir and checks it is a real directory, not a junction, a symlink or a
   file. The handle reads the directory and denies delete sharing, so while it
   is open neither that folder nor any ancestor can be renamed or replaced, and
   the paths checked inside it keep naming the same folder. */
static HANDLE pinDirectory(const wchar_t* dir)
{
	HANDLE h = CreateFileW(dir, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return h;
	BY_HANDLE_FILE_INFORMATION fi;
	if (!GetFileInformationByHandle(h, &fi) || !(fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		|| (fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
	{
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}
	return h;
}

// Index of the last path separator, or -1.
static int lastSeparator(const wchar_t* path)
{
	int last = -1;
	for (int i = 0; path[i]; i++)
		if (path[i] == L'\\' || path[i] == L'/')
			last = i;
	return last;
}

// Pins the folder of an absolute file path (see pinDirectory).
static HANDLE pinParentDirectory(const wchar_t* filePath)
{
	wchar_t dir[MAX_PATH + 64];
	if (!filePath || !isAbsolutePath(filePath) || !formatWide(dir, MAX_PATH + 64, L"%s", filePath))
		return INVALID_HANDLE_VALUE;
	int slash = lastSeparator(dir);
	if (slash < 2 || !dir[slash + 1])
		return INVALID_HANDLE_VALUE;
	dir[slash == 2 ? 3 : slash] = 0;	// "C:\name" keeps the root's separator
	return pinDirectory(dir);
}

static bool renameHandle(HANDLE h, const wchar_t* target)
{
	size_t len = wcslen(target);
	DWORD size = (DWORD)(sizeof(FILE_RENAME_INFO) + (len + 1) * sizeof(wchar_t));
	FILE_RENAME_INFO* renameInfo = (FILE_RENAME_INFO*)calloc(1, size);
	if (!renameInfo)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return false;
	}
	renameInfo->ReplaceIfExists = TRUE;
	renameInfo->RootDirectory = NULL;
	renameInfo->FileNameLength = (DWORD)(len * sizeof(wchar_t));
	memcpy(renameInfo->FileName, target, (len + 1) * sizeof(wchar_t));
	BOOL ok = SetFileInformationByHandle(h, FileRenameInfo, renameInfo, size);
	DWORD error = GetLastError();
	free(renameInfo);
	SetLastError(error);
	return ok != FALSE;
}

static void deleteHandle(HANDLE h)
{
	FILE_DISPOSITION_INFO disposition;
	disposition.DeleteFile = TRUE;
	SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition));
}


/*================================ Cache ===============================*/

#define TEMP_STALE_100NS (3600ULL * 10000000ULL)

ULONGLONG UpdateUnixTimeNow()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER t;
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	const ULONGLONG epoch = 116444736000000000ULL;	// 1970-01-01 in FILETIME units
	return t.QuadPart < epoch ? 0 : (t.QuadPart - epoch) / 10000000ULL;
}

// Wide APIs throughout: %LOCALAPPDATA% holds the user name, which the ANSI code page may not represent.
static bool cacheFolder(wchar_t* dir, DWORD maxChars)
{
	wchar_t base[MAX_PATH];
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
	// A relative value would resolve against the game's working directory.
	if (!n || n >= MAX_PATH || !isAbsolutePath(base) || !formatWide(dir, maxChars, L"%s\\PlugY4Kolbot", base))
		return false;
	if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	HANDLE hDir = pinDirectory(dir);
	if (hDir == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(hDir);
	return true;
}

bool UpdateCacheChannelPath(int channel, wchar_t* path, DWORD maxChars)
{
	if (!path || !maxChars)
		return false;
	path[0] = 0;
	wchar_t dir[MAX_PATH];
	if ((channel != UPDATE_CHANNEL_STABLE && channel != UPDATE_CHANNEL_PRERELEASE) || !cacheFolder(dir, MAX_PATH)
		|| !formatWide(path, maxChars, L"%s\\update-check-%s.txt", dir,
			channel == UPDATE_CHANNEL_PRERELEASE ? L"prerelease" : L"stable"))
	{
		path[0] = 0;
		return false;
	}
	return true;
}

bool UpdateCacheDefaultPath(wchar_t* path, DWORD maxChars)
{
	return UpdateCacheChannelPath(UPDATE_CHANNEL_STABLE, path, maxChars);
}

static bool hasControlChar(const char* s)
{
	for (; *s; s++)
		if ((BYTE)*s < 0x20 || *s == 0x7F)
			return true;
	return false;
}

// The values the reader accepts; the writer refuses anything else, so a write always reads back.
static bool cacheTagValid(const char* tag)
{
	UpdateVersion v;
	return !tag[0] || UpdateParseTag(tag, &v);
}

static bool cacheUrlValid(const char* url)
{
	return !url[0] || (isHttpsUrl(url) && !hasControlChar(url));
}

// Copies a value into a fixed field; false when it does not fit.
static bool copyField(char* field, DWORD cap, const char* value, DWORD len)
{
	if (len >= cap)
		return false;
	memcpy(field, value, len);
	field[len] = 0;
	return true;
}

static bool parseCacheText(const char* text, DWORD got, UpdateCache* out)
{
	UpdateCache cache;
	memset(&cache, 0, sizeof(cache));
	bool hasCheckedAt = false, hasChannel = false, hasStatus = false, hasTag = false, hasUrl = false;
	DWORD pos = 0;
	while (pos < got)
	{
		DWORD lineEnd = pos;
		while (lineEnd < got && text[lineEnd] != '\n')
			lineEnd++;
		DWORD next = lineEnd < got ? lineEnd + 1 : got;
		if (lineEnd > pos && text[lineEnd - 1] == '\r')
			lineEnd--;
		const char* line = text + pos;
		DWORD lineLen = lineEnd - pos;
		pos = next;
		if (!lineLen)
			continue;
		const char* eq = (const char*)memchr(line, '=', lineLen);
		if (!eq)
			return false;
		DWORD keyLen = (DWORD)(eq - line);
		const char* value = eq + 1;
		DWORD valueLen = lineLen - keyLen - 1;
		char field[16];
		bool shortField = copyField(field, sizeof(field), value, valueLen);

		if (keyLen == 9 && !memcmp(line, "checkedAt", 9))
		{
			// 19 digits cannot overflow a ULONGLONG.
			if (hasCheckedAt || !valueLen || valueLen > 19)
				return false;
			ULONGLONG t = 0;
			for (DWORD i = 0; i < valueLen; i++)
			{
				if (!isDigit(value[i]))
					return false;
				t = t * 10 + (value[i] - '0');
			}
			cache.checkedAt = t;
			hasCheckedAt = true;
		}
		else if (keyLen == 7 && !memcmp(line, "channel", 7))
		{
			if (hasChannel || !shortField)
				return false;
			if (!strcmp(field, "stable")) cache.channel = UPDATE_CHANNEL_STABLE;
			else if (!strcmp(field, "prerelease")) cache.channel = UPDATE_CHANNEL_PRERELEASE;
			else return false;
			hasChannel = true;
		}
		else if (keyLen == 6 && !memcmp(line, "status", 6))
		{
			if (hasStatus || !shortField)
				return false;
			if (!strcmp(field, "ok")) cache.ok = true;
			else if (!strcmp(field, "error")) cache.ok = false;
			else return false;
			hasStatus = true;
		}
		else if (keyLen == 9 && !memcmp(line, "latestTag", 9))
		{
			if (hasTag || !copyField(cache.latestTag, sizeof(cache.latestTag), value, valueLen) || !cacheTagValid(cache.latestTag))
				return false;
			hasTag = true;
		}
		else if (keyLen == 7 && !memcmp(line, "pageUrl", 7))
		{
			if (hasUrl || !copyField(cache.pageUrl, sizeof(cache.pageUrl), value, valueLen) || !cacheUrlValid(cache.pageUrl))
				return false;
			hasUrl = true;
		}
		// Unknown keys are skipped, so a newer PlugY can add some without older ones discarding the file.
	}
	if (!hasCheckedAt || !hasChannel || !hasStatus)
		return false;
	*out = cache;
	return true;
}

enum { CACHE_MISSING, CACHE_VALID, CACHE_GARBLED, CACHE_UNSAFE };

// The caller holds the folder pinned. UNSAFE: a reparse point, a directory, or a file that stays unreadable.
static int readCacheFile(const wchar_t* path, UpdateCache* out)
{
	memset(out, 0, sizeof(*out));
	HANDLE hFile;
	for (int retry = 0; ; retry++)
	{
		hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
			break;
		DWORD error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND)
			return CACHE_MISSING;
		// A writer renaming over the file holds it only briefly.
		if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) || pathIsDirOrReparse(path) || retry >= 10)
			return CACHE_UNSAFE;
		Sleep(50);
	}
	char text[CACHE_FILE_MAX];
	DWORD got = 0;
	DWORD extra = 0;
	char probe;
	bool plain = handleIsPlainFile(hFile);
	bool readOk = plain && ReadFile(hFile, text, sizeof(text), &got, NULL)
		&& (got < sizeof(text) ? ReadFile(hFile, &probe, 1, &extra, NULL) : true);
	CloseHandle(hFile);
	if (!readOk)
		return CACHE_UNSAFE;
	// An oversized or garbled file is only stale: the next write replaces it.
	if (got >= sizeof(text) || extra || memchr(text, 0, got) || !parseCacheText(text, got, out))
	{
		memset(out, 0, sizeof(*out));
		return CACHE_GARBLED;
	}
	return CACHE_VALID;
}

bool UpdateCacheRead(const wchar_t* path, UpdateCache* out)
{
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	HANDLE hDir = pinParentDirectory(path);
	if (hDir == INVALID_HANDLE_VALUE)
		return false;
	int state = readCacheFile(path, out);
	CloseHandle(hDir);
	return state == CACHE_VALID;
}

// <name>.<digits>.<digits>.tmp: exactly the names writeCacheFile gives its temps.
static bool isOwnTempName(const wchar_t* candidate, const wchar_t* name)
{
	size_t n = wcslen(name);
	if (_wcsnicmp(candidate, name, n) || candidate[n] != L'.')
		return false;
	const wchar_t* p = candidate + n + 1;
	for (int part = 0; part < 2; part++)
	{
		int digits = 0;
		while (*p >= L'0' && *p <= L'9')
		{
			if (++digits > 10)
				return false;
			p++;
		}
		if (!digits || *p != L'.')
			return false;
		p++;
	}
	return !_wcsicmp(p, L"tmp");
}

static ULONGLONG fileTimeValue(FILETIME ft)
{
	ULARGE_INTEGER t;
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	return t.QuadPart;
}

static bool isStale(FILETIME written, ULONGLONG now)
{
	ULONGLONG t = fileTimeValue(written);
	return t <= now && now - t >= TEMP_STALE_100NS;
}

/* A crash between creating a temp and renaming it leaves the temp behind. Only
   this module's own names for path are swept, only after an hour (a live writer
   needs milliseconds), and each is re-checked through the handle that deletes it. */
static void sweepStaleTemps(const wchar_t* path)
{
	int slash = lastSeparator(path);
	wchar_t pattern[MAX_PATH + 64];
	if (slash < 0 || !formatWide(pattern, MAX_PATH + 64, L"%s.*.tmp", path))
		return;
	const wchar_t* name = path + slash + 1;
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	FILETIME nowTime;
	GetSystemTimeAsFileTime(&nowTime);
	ULONGLONG now = fileTimeValue(nowTime);
	do
	{
		wchar_t full[MAX_PATH + 64];
		if ((fd.dwFileAttributes & unsafeAttributes) || !isOwnTempName(fd.cFileName, name)
			|| !isStale(fd.ftLastWriteTime, now)
			|| !formatWide(full, MAX_PATH + 64, L"%.*s%s", slash + 1, path, fd.cFileName))
			continue;
		HANDLE h = CreateFileW(full, DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		BY_HANDLE_FILE_INFORMATION fi;
		if (GetFileInformationByHandle(h, &fi) && !(fi.dwFileAttributes & unsafeAttributes) && isStale(fi.ftLastWriteTime, now))
			deleteHandle(h);
		CloseHandle(h);
	} while (FindNextFileW(hFind, &fd));
	FindClose(hFind);
}

/* A read-only cache file could never be replaced, so every game start would ask
   GitHub again. The flag is cleared through a handle on the file itself, and
   only when that handle shows a plain file, never a link or a folder. */
static void clearReadOnly(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	FILE_BASIC_INFO basic;
	if (handleIsPlainFile(h) && GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic))
		&& (basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
	{
		// Zero times leave them unchanged; zero attributes would too, hence FILE_ATTRIBUTE_NORMAL.
		DWORD attributes = basic.FileAttributes & ~FILE_ATTRIBUTE_READONLY;
		memset(&basic, 0, sizeof(basic));
		basic.FileAttributes = attributes ? attributes : FILE_ATTRIBUTE_NORMAL;
		SetFileInformationByHandle(h, FileBasicInfo, &basic, sizeof(basic));
	}
	CloseHandle(h);
}

// The caller holds the folder pinned. The temp is renamed by its own handle, so exactly the file written moves.
static bool writeCacheFile(const wchar_t* path, const char* text, DWORD size)
{
	// Renaming would replace a link rather than follow it, but a link or folder there was planted: nothing is written.
	if (pathIsDirOrReparse(path))
		return false;
	sweepStaleTemps(path);
	wchar_t tempPath[MAX_PATH + 64];
	if (!formatWide(tempPath, MAX_PATH + 64, L"%s.%u.%u.tmp", path, GetCurrentProcessId(), GetTickCount()))
		return false;
	// CREATE_NEW with FILE_FLAG_OPEN_REPARSE_POINT fails on any existing name, a planted link included.
	HANDLE hFile = CreateFileW(tempPath, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	bool ok = handleIsPlainFile(hFile) && WriteFile(hFile, text, size, &written, NULL) && written == size;
	if (ok)
		clearReadOnly(path);
	// A reader holds the file only for one read; replacing a file that is open fails until it closes.
	for (int retry = 0; ok; retry++)
	{
		if (renameHandle(hFile, path))
		{
			CloseHandle(hFile);
			return true;
		}
		DWORD error = GetLastError();
		if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) || retry >= 40)
			break;
		Sleep(50);
	}
	deleteHandle(hFile);
	CloseHandle(hFile);
	return false;
}

bool UpdateCacheWrite(const wchar_t* path, const UpdateCache* cache)
{
	if (!path || !cache
		|| (cache->channel != UPDATE_CHANNEL_STABLE && cache->channel != UPDATE_CHANNEL_PRERELEASE)
		|| !memchr(cache->latestTag, 0, sizeof(cache->latestTag)) || !memchr(cache->pageUrl, 0, sizeof(cache->pageUrl))
		|| !cacheTagValid(cache->latestTag) || !cacheUrlValid(cache->pageUrl))
		return false;
	char text[CACHE_FILE_MAX];
	int n = _snprintf(text, sizeof(text), "checkedAt=%I64u\r\nchannel=%s\r\nstatus=%s\r\nlatestTag=%s\r\npageUrl=%s\r\n",
		cache->checkedAt, cache->channel == UPDATE_CHANNEL_PRERELEASE ? "prerelease" : "stable",
		cache->ok ? "ok" : "error", cache->latestTag, cache->pageUrl);
	if (n < 0 || n >= (int)sizeof(text))
		return false;
	HANDLE hDir = pinParentDirectory(path);
	if (hDir == INVALID_HANDLE_VALUE)
		return false;
	bool ok = writeCacheFile(path, text, (DWORD)n);
	CloseHandle(hDir);
	return ok;
}

bool UpdateShouldCheck(const UpdateCache* cache, int channel, ULONGLONG now)
{
	if (!cache || cache->channel != channel || cache->checkedAt > now)
		return true;
	return now - cache->checkedAt >= (ULONGLONG)(cache->ok ? UPDATE_RECHECK_OK_SECONDS : UPDATE_RECHECK_ERROR_SECONDS);
}


/*============================ One-call check ==========================*/

enum { LOCK_ACQUIRED, LOCK_BUSY, LOCK_UNUSABLE };

/* Only one process per user and channel fetches at a time. The lock file is
   opened exclusively and deleted on close, so a crashed holder releases it.
   The caller holds the folder pinned. */
static int acquireFetchLock(const wchar_t* cachePath, HANDLE* hLock)
{
	*hLock = INVALID_HANDLE_VALUE;
	wchar_t lockPath[MAX_PATH + 64];
	if (!formatWide(lockPath, MAX_PATH + 64, L"%s.lock", cachePath))
		return LOCK_UNUSABLE;
	DWORD start = GetTickCount();
	int deniedWithoutFile = 0;
	for (;;)
	{
		HANDLE h = CreateFileW(lockPath, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (h != INVALID_HANDLE_VALUE)
		{
			if (!handleIsPlainFile(h))
			{
				CloseHandle(h);
				return LOCK_UNUSABLE;
			}
			*hLock = h;
			return LOCK_ACQUIRED;
		}
		DWORD error = GetLastError();
		if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)
			return LOCK_UNUSABLE;
		// A lock file pending deletion answers ERROR_ACCESS_DENIED until its last handle closes. A folder or link
		// there always does, and so does a folder that refuses new files, seen as no file at all; the holder's
		// file vanishing between the two calls is why that takes three strikes.
		if (error == ERROR_ACCESS_DENIED)
		{
			DWORD attributes = GetFileAttributesW(lockPath);
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & unsafeAttributes))
				return LOCK_UNUSABLE;
			if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND && ++deniedWithoutFile >= 3)
				return LOCK_UNUSABLE;
		}
		if (GetTickCount() - start >= FETCH_LOCK_WAIT_MS)
			return LOCK_BUSY;
		Sleep(250);
	}
}

// Asks GitHub and builds the entry to cache. A failure keeps the previous entry's release, so the hint survives it.
static void fetchIntoCache(const char* repo, int channel, const UpdateCache* previous, UpdateCache* entry, UpdateHint* out)
{
	memset(entry, 0, sizeof(*entry));
	entry->channel = channel;
	// About 16 KB: kept off the stack of the caller's thread.
	UpdateRelease* release = (UpdateRelease*)malloc(sizeof(UpdateRelease));
	int result = release
		? UpdateFetchLatest(repo, channel == UPDATE_CHANNEL_PRERELEASE, release, &out->info)
		: UPDATE_NO_MEMORY;
	out->fetched = true;
	out->result = result;
	entry->checkedAt = UpdateUnixTimeNow();
	if (result == UPDATE_OK || result == UPDATE_NO_RELEASE)
	{
		entry->ok = true;
		if (result == UPDATE_OK)
		{
			copyField(entry->latestTag, sizeof(entry->latestTag), release->tag, (DWORD)strlen(release->tag));
			// A page URL the cache file cannot hold (e.g. an escaped newline) is dropped, not allowed to block the write.
			if (cacheUrlValid(release->pageUrl))
				copyField(entry->pageUrl, sizeof(entry->pageUrl), release->pageUrl, (DWORD)strlen(release->pageUrl));
		}
	}
	else if (previous && previous->channel == channel)
	{
		memcpy(entry->latestTag, previous->latestTag, sizeof(entry->latestTag));
		memcpy(entry->pageUrl, previous->pageUrl, sizeof(entry->pageUrl));
	}
	free(release);
}

bool UpdateCheckForNewerEx(const char* repo, const wchar_t* cachePath, int channel,
	const char* currentVersion, UpdateHint* out)
{
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	UpdateVersion current;
	if (!repo || (channel != UPDATE_CHANNEL_STABLE && channel != UPDATE_CHANNEL_PRERELEASE)
		|| !UpdateParseTag(currentVersion, &current))
	{
		out->result = UPDATE_BAD_ARGUMENT;
		return false;
	}
	// No cache, no request: without one, every game start would ask GitHub.
	HANDLE hDir = cachePath ? pinParentDirectory(cachePath) : INVALID_HANDLE_VALUE;
	if (hDir == INVALID_HANDLE_VALUE)
	{
		out->result = UPDATE_CACHE_UNAVAILABLE;
		return false;
	}

	UpdateCache cache;
	int state = readCacheFile(cachePath, &cache);
	bool usable = state != CACHE_UNSAFE;
	if (usable && UpdateShouldCheck(state == CACHE_VALID ? &cache : NULL, channel, UpdateUnixTimeNow()))
	{
		HANDLE hLock;
		int lock = acquireFetchLock(cachePath, &hLock);
		if (lock == LOCK_UNUSABLE)
			usable = false;
		else
		{
			// Re-read under the lock (or after giving up on it): the holder may have just refreshed the cache.
			state = readCacheFile(cachePath, &cache);
			if (state == CACHE_UNSAFE)
				usable = false;
			else if (lock == LOCK_BUSY)
				out->result = UPDATE_BUSY;
			else if (UpdateShouldCheck(state == CACHE_VALID ? &cache : NULL, channel, UpdateUnixTimeNow()))
			{
				UpdateCache entry;
				fetchIntoCache(repo, channel, state == CACHE_VALID ? &cache : NULL, &entry, out);
				cache = entry;
				state = CACHE_VALID;
				UpdateCacheWrite(cachePath, &cache);
			}
			if (lock == LOCK_ACQUIRED)
				CloseHandle(hLock);
		}
	}
	CloseHandle(hDir);
	if (!usable)
	{
		out->result = UPDATE_CACHE_UNAVAILABLE;
		return false;
	}

	// Compared now rather than when cached, so an install updated since then stops showing the hint.
	UpdateVersion latest;
	if (state != CACHE_VALID || cache.channel != channel || !UpdateParseTag(cache.latestTag, &latest)
		|| UpdateCompareVersions(&latest, &current) <= 0)
		return false;
	memcpy(out->tag, cache.latestTag, sizeof(out->tag));
	const char* version = cache.latestTag[0] == 'v' ? cache.latestTag + 1 : cache.latestTag;
	copyField(out->version, sizeof(out->version), version, (DWORD)strlen(version));
	memcpy(out->pageUrl, cache.pageUrl, sizeof(out->pageUrl));
	out->newer = true;
	return true;
}

bool UpdateCheckForNewer(int channel, const char* currentVersion, UpdateHint* out)
{
	wchar_t cachePath[MAX_PATH];
	bool haveCachePath = UpdateCacheChannelPath(channel, cachePath, MAX_PATH);
	return UpdateCheckForNewerEx(PLUGY_UPDATE_REPO, haveCachePath ? cachePath : NULL, channel, currentVersion, out);
}

///////////////////////// END OF FILE ///////////////////////

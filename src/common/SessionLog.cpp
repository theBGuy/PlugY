/*=================================================================
	PlugY's per-session log files: naming, line stamps, removals.
	See SessionLog.h.
=================================================================*/

#include "SessionLog.h"
#include <stdio.h>
#include <string.h>

static const char NAME_PREFIX[] = "PlugY_";
static const char NAME_SUFFIX[] = ".log";

bool SessionLogName(const SYSTEMTIME* t, DWORD pid, char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return false;
	int n = _snprintf(out, outSize, "%s%04u-%02u-%02u_%02u-%02u-%02u_%lu%s", NAME_PREFIX,
		t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute, t->wSecond, (unsigned long)pid, NAME_SUFFIX);
	if (n < 0 || (size_t)n >= outSize)
	{
		out[0] = 0;
		return false;
	}
	return true;
}

static bool digits(const char*& p, int count)
{
	for (int i = 0; i < count; i++, p++)
		if (*p < '0' || *p > '9')
			return false;
	return true;
}

static bool literal(const char*& p, char c)
{
	if (*p != c)
		return false;
	p++;
	return true;
}

bool SessionLogIsOurs(const char* name)
{
	if (!name || _strnicmp(name, NAME_PREFIX, sizeof(NAME_PREFIX) - 1))
		return false;
	const char* p = name + sizeof(NAME_PREFIX) - 1;
	if (!(digits(p, 4) && literal(p, '-') && digits(p, 2) && literal(p, '-') && digits(p, 2) && literal(p, '_')
		&& digits(p, 2) && literal(p, '-') && digits(p, 2) && literal(p, '-') && digits(p, 2) && literal(p, '_')))
		return false;
	int pidDigits = 0;
	while (*p >= '0' && *p <= '9')
	{
		p++;
		pidDigits++;
	}
	return pidDigits >= 1 && pidDigits <= 10 && !_stricmp(p, NAME_SUFFIX);
}

void SessionLogClock(const SYSTEMTIME* t, char out[SESSION_LOG_CLOCK_SIZE])
{
	_snprintf(out, SESSION_LOG_CLOCK_SIZE, "%02u:%02u:%02u.%03u ", t->wHour, t->wMinute, t->wSecond, t->wMilliseconds);
	out[SESSION_LOG_CLOCK_SIZE - 1] = 0;
}

void SessionLogStamp(const char* text, const char* stamp, bool* atLineStart, SessionLogSink sink, void* ctx)
{
	if (!text)
		return;
	const size_t stampLen = strlen(stamp);
	const char* segment = text;
	const char* p = text;
	for (; *p; p++)
	{
		if (*atLineStart && *p != '\n')
		{
			if (p > segment)
				sink(segment, p - segment, ctx);
			sink(stamp, stampLen, ctx);
			segment = p;
			*atLineStart = false;
		}
		if (*p == '\n')
			*atLineStart = true;
	}
	if (p > segment)
		sink(segment, p - segment, ctx);
}

static ULONGLONG ticks(FILETIME f)
{
	ULARGE_INTEGER u;
	u.LowPart = f.dwLowDateTime;
	u.HighPart = f.dwHighDateTime;
	return u.QuadPart;
}

int SessionLogSelectRemovals(SessionLogEntry* entries, int count, FILETIME now, int keepDays, int keepMax)
{
	const ULONGLONG nowTicks = ticks(now);
	const ULONGLONG maxAge = (ULONGLONG)(keepDays > 0 ? keepDays : 0) * 24 * 60 * 60 * 10000000ULL;
	int marked = 0;
	int kept = 0;
	for (int i = 0; i < count; i++)
	{
		const ULONGLONG written = ticks(entries[i].lastWrite);
		// A log stamped in the future (clock changed back) is never too old; the count rule still bounds it.
		entries[i].remove = written < nowTicks && nowTicks - written > maxAge;
		if (entries[i].remove)
			marked++;
		else
			kept++;
	}
	const int keepOld = keepMax > 0 ? keepMax - 1 : 0;
	while (kept > keepOld)
	{
		int oldest = -1;
		for (int i = 0; i < count; i++)
			if (!entries[i].remove && (oldest < 0 || ticks(entries[i].lastWrite) < ticks(entries[oldest].lastWrite)))
				oldest = i;
		entries[oldest].remove = true;
		marked++;
		kept--;
	}
	return marked;
}

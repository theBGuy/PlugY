/*=================================================================
	PlugY's per-session log files: file names, line timestamps and
	which old logs to remove. No file I/O here (src/plugy/Error.cpp
	does it), so tests/SessionLogTests.cpp can cover every rule.
=================================================================*/
#pragma once

#include <windows.h>
#include <stddef.h>

#define SESSION_LOG_KEEP_DAYS 7
#define SESSION_LOG_KEEP_MAX 500
#define SESSION_LOG_CLOCK_SIZE 16

// "PlugY_2026-10-10_20-44-47_287472.log": start time (local) and process id, so two games started in the same
// second still get their own file and names sort by start time. False when out is too small.
bool SessionLogName(const SYSTEMTIME* t, DWORD pid, char* out, size_t outSize);

// Whether a file name has exactly the SessionLogName shape. FindFirstFile's "*.log" also matches longer
// extensions through 8.3 short names, so removal checks every name with this first.
bool SessionLogIsOurs(const char* name);

// "20:44:47.123 " (local time, trailing space): the stamp written at the start of each line.
void SessionLogClock(const SYSTEMTIME* t, char out[SESSION_LOG_CLOCK_SIZE]);

typedef void (*SessionLogSink)(const char* text, size_t len, void* ctx);

// Sends text to sink with stamp at the start of every non-empty line. *atLineStart carries the line state across
// calls, so a message that continues a line gets no second stamp.
void SessionLogStamp(const char* text, const char* stamp, bool* atLineStart, SessionLogSink sink, void* ctx);

struct SessionLogEntry
{
	FILETIME lastWrite;
	bool remove;
};

// Marks the logs to remove before a new one is created: every log last written more than keepDays before now,
// then the oldest of the rest until keepMax - 1 remain (the new log makes keepMax). Returns how many are marked.
int SessionLogSelectRemovals(SessionLogEntry* entries, int count, FILETIME now, int keepDays, int keepMax);

/*=================================================================
	PlugY4Kolbot updater: adds the options a kept PlugY.ini lacks.
	See UpdaterIniMerge.h.
=================================================================*/

#include "UpdaterIniMerge.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define INI_VALUE_MAX 1024

// The default GetPrivateProfileString hands back for a missing key: no real value holds these control characters.
static const wchar_t MISSING[] = L"\x01missing\x01";

static bool hasIniValue(const wchar_t* path, const wchar_t* section, const wchar_t* key, wchar_t* out, DWORD size)
{
	GetPrivateProfileStringW(section, key, MISSING, out, size, path);
	return wcscmp(out, MISSING) != 0;
}

// Ends path with an empty line, so the section WritePrivateProfileString appends next stands apart.
static bool appendEmptyLine(const wchar_t* path)
{
	HANDLE file = CreateFileW(path, FILE_READ_DATA | FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size;
	bool ok = GetFileSizeEx(file, &size) != 0;
	if (ok && size.QuadPart > 0)
	{
		LARGE_INTEGER last;
		last.QuadPart = size.QuadPart - 1;
		char c = 0;
		DWORD read = 0;
		ok = SetFilePointerEx(file, last, NULL, FILE_BEGIN) && ReadFile(file, &c, 1, &read, NULL) && read == 1;
		if (ok)
		{
			// FILE_APPEND_DATA alone writes at the end, wherever the pointer is.
			const char* text = c == '\n' ? "\r\n" : "\r\n\r\n";
			DWORD len = (DWORD)strlen(text);
			DWORD written = 0;
			ok = WriteFile(file, text, len, &written, NULL) && written == len;
		}
	}
	CloseHandle(file);
	return ok;
}

// The template as wide text: its option lines are ASCII (scripts/installer-ini.test.mjs), like the installer reads it.
static wchar_t* readTemplate(const wchar_t* path)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return NULL;
	wchar_t* text = NULL;
	LARGE_INTEGER size;
	if (GetFileSizeEx(file, &size) && size.QuadPart < 1024 * 1024)
	{
		char* bytes = (char*)malloc((size_t)size.QuadPart + 1);
		DWORD read = 0;
		if (bytes && ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL) && read == (DWORD)size.QuadPart)
		{
			int chars = MultiByteToWideChar(CP_ACP, 0, bytes, (int)read, NULL, 0);
			text = (wchar_t*)malloc(((size_t)chars + 1) * sizeof(wchar_t));
			if (text)
			{
				MultiByteToWideChar(CP_ACP, 0, bytes, (int)read, text, chars);
				text[chars] = 0;
			}
		}
		free(bytes);
	}
	CloseHandle(file);
	return text;
}

// The next line of *cursor without its line end (cut in place), or NULL at the end.
static wchar_t* nextLine(wchar_t** cursor)
{
	wchar_t* line = *cursor;
	if (!line || !*line)
		return NULL;
	wchar_t* end = wcschr(line, L'\n');
	*cursor = end ? end + 1 : line + wcslen(line);
	if (end)
		*end = 0;
	size_t len = wcslen(line);
	if (len && line[len - 1] == L'\r')
		line[len - 1] = 0;
	return line;
}

// "Key=value" -> key (cut at the '=') and value; false for anything else, and for a key too long to look up.
static bool splitOption(wchar_t* line, wchar_t** key, wchar_t** value)
{
	if (!line[0] || line[0] == L';' || line[0] == L'[')
		return false;
	wchar_t* eq = wcschr(line, L'=');
	if (!eq || eq == line || eq - line >= 256)
		return false;
	*eq = 0;
	*key = line;
	*value = eq + 1;
	return true;
}

int UpdaterAddMissingOptions(const wchar_t* ini, const wchar_t* templ, const wchar_t* defaults)
{
	if (!ini || !templ || !defaults)
		return -1;
	wchar_t* text = readTemplate(templ);
	if (!text)
		return -1;
	int added = 0;
	wchar_t section[256] = L"";
	wchar_t value[INI_VALUE_MAX];
	bool sectionKnown = true;
	wchar_t* cursor = text;
	for (wchar_t* line; (line = nextLine(&cursor)) != NULL; )
	{
		if (line[0] == L'[')
		{
			wchar_t* close = wcschr(line, L']');
			size_t len = close ? (size_t)(close - line - 1) : 0;
			section[0] = 0;
			if (close && len < _countof(section) && CompareStringOrdinal(line + 1, (int)len, L"LAUNCHING", -1, TRUE) != CSTR_EQUAL)
			{
				wcsncpy(section, line + 1, len);
				section[len] = 0;
			}
			if (!section[0])
				continue;
			// Does ini hold an option of this section already? Look ahead, without cutting the template's lines.
			sectionKnown = false;
			for (const wchar_t* p = cursor; *p && *p != L'[' && !sectionKnown; )
			{
				const wchar_t* end = wcschr(p, L'\n');
				size_t lineLen = end ? (size_t)(end - p) : wcslen(p);
				const wchar_t* eq = (const wchar_t*)wmemchr(p, L'=', lineLen);
				if (*p != L';' && eq && eq > p && eq - p < 256)
				{
					wchar_t key[256];
					wcsncpy(key, p, eq - p);
					key[eq - p] = 0;
					sectionKnown = hasIniValue(ini, section, key, value, INI_VALUE_MAX);
				}
				p = end ? end + 1 : p + lineLen;
			}
			continue;
		}
		wchar_t* key;
		wchar_t* shipped;
		if (!section[0] || !splitOption(line, &key, &shipped) || hasIniValue(ini, section, key, value, INI_VALUE_MAX))
			continue;
		if (!hasIniValue(defaults, section, key, value, INI_VALUE_MAX))
		{
			wcsncpy(value, shipped, INI_VALUE_MAX - 1);
			value[INI_VALUE_MAX - 1] = 0;
		}
		if (!sectionKnown)
		{
			if (!appendEmptyLine(ini))
			{
				free(text);
				return -1;
			}
			sectionKnown = true;
		}
		if (!WritePrivateProfileStringW(section, key, value, ini))
		{
			free(text);
			return -1;
		}
		added++;
	}
	free(text);
	return added;
}

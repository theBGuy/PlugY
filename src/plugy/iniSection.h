/*=================================================================
	Where an INI section's options start, shared by INIFile (char) and INIFileW (WCHAR). No D2 or PlugY
	dependencies, so tests/IniSectionTests.cpp covers it.
=================================================================*/
#pragma once

#include <ctype.h>
#include <stddef.h>
#include <wctype.h>

inline int iniLower(char c) { return tolower((unsigned char)c); }
inline wint_t iniLower(wchar_t c) { return towlower(c); }

// Byte order marks a read file keeps at its start (LocalizedStrings.ini is UTF-16LE with one).
inline const char* skipIniBom(const char* text)
{
	return (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF ? text + 3 : text;
}
inline const wchar_t* skipIniBom(const wchar_t* text)
{
	return text[0] == 0xFEFF ? text + 1 : text;
}

// The text right after the first "[section]" header (any case) that starts a line, spaces or tabs allowed before it,
// or NULL. Only a header counts: "[MAIN SCREEN]" inside a comment or a value names no section.
template <class C> const C* findIniSection(const C* text, const C* section)
{
	if (!text || !section)
		return NULL;
	size_t len = 0;
	while (section[len])
		len++;
	for (const C* line = skipIniBom(text); *line; )
	{
		const C* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '[')
		{
			size_t i = 0;
			while (i < len && p[1 + i] && iniLower(p[1 + i]) == iniLower(section[i]))
				i++;
			if (i == len && p[1 + len] == ']')
				return p + len + 2;
		}
		while (*line && *line != '\n')
			line++;
		if (*line)
			line++;
	}
	return NULL;
}

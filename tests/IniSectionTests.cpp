/*=================================================================
	Unit tests for src/plugy/iniSection.h.
	Built and run by scripts/build.ps1 -Test with the shipped game-files\PlugY.ini, PlugY\PlugYDefault.ini,
	PlugY\PlugYFixed.ini and the UTF-16 PlugY\LocalizedStrings.ini as arguments; exits 1 on any failure.
=================================================================*/

#include "../src/plugy/iniSection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int nbPassed = 0;
static int nbFailed = 0;

static void check(bool ok, const char* text, int line)
{
	if (ok)
	{
		nbPassed++;
		return;
	}
	nbFailed++;
	printf("  FAIL line %d: %s\n", line, text);
}

#define CHECK(cond) check((cond) ? true : false, #cond, __LINE__)

// The offset findIniSection answers, -1 for NULL.
static long at(const char* text, const char* section)
{
	const char* p = findIniSection(text, section);
	return p ? (long)(p - text) : -1;
}

static void testHeaders()
{
	CHECK(at("[A]\r\nk=1", "A") == 3);
	CHECK(at("[A]\r\nk=1", "a") == 3);								// any case
	CHECK(at("x=1\r\n[MAIN SCREEN]\r\nk=1", "MAIN SCREEN") == 18);
	CHECK(at("[EXTRA] \r\nk=1", "EXTRA") == 7);						// spaces after the header
	CHECK(at("  \t[A]\r\n", "A") == 6);								// spaces or tabs before it
	CHECK(at("\xEF\xBB\xBF[A]\r\n", "A") == 6);						// a UTF-8 byte order mark
	CHECK(at("", "A") == -1);
	CHECK(at("[A]", "") == -1);
	CHECK(at("[AB]\r\n", "A") == -1);
	CHECK(at("[A\r\n", "A") == -1);
	CHECK(at("[B]\r\n", "A") == -1);
	CHECK(at("x=1\n[B]\nk=1", "B") == 7);							// LF-only lines
	CHECK(at("k=1\r\n[B]", "B") == 8);								// the last line, with no line end
	CHECK(at("[MAIN]\r\n[MAIN SCREEN]\r\n", "MAIN SCREEN") == 21);	// a shorter name first
	CHECK(at("[MAIN SCREEN]\r\n", "MAIN") == -1);					// a longer name only
	CHECK(findIniSection((const char*)NULL, "A") == NULL);
	CHECK(findIniSection("[A]", (const char*)NULL) == NULL);
}

static void testNotHeaders()
{
	// A comment that names a section, as the shipped PlugY.ini's [GENERAL] did: the real header comes later.
	const char* comment = "[GENERAL]\r\n;1: show (needs [MAIN SCREEN] ActivePrintPlugYVersion=1)\r\nk=1\r\n\r\n[MAIN SCREEN]\r\nActivePrintPlugYVersion=0";
	const char* real = strstr(comment, "\r\n[MAIN SCREEN]") + 2;
	CHECK(findIniSection(comment, "MAIN SCREEN") == real + strlen("[MAIN SCREEN]"));
	CHECK(at("[A]\r\nv=[B]\r\n", "B") == -1);						// in a value
	CHECK(at("; [B]\r\n", "B") == -1);								// in a comment line
	CHECK(at("x [B]\r\n", "B") == -1);								// not at the line's start
	CHECK(at("[A]\r\nv=[B]\r\n[B]\r\nk=2", "B") == 15);
}

static void testWide()
{
	const wchar_t* text = L"\xFEFF[ENG]\r\nk=1\r\n;[FRA]\r\n[fra]\r\nk=2";	// LocalizedStrings.ini is UTF-16LE with a BOM
	const wchar_t* p = findIniSection(text, L"ENG");
	CHECK(p && p - text == 6);
	p = findIniSection(text, L"FRA");
	CHECK(p && p - text == wcsstr(text, L"[fra]") - text + 5);
	CHECK(findIniSection(text, L"DEU") == NULL);
}

static char* readFile(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* data = (char*)malloc(size + 2);
	if (data && fread(data, 1, size, file) != (size_t)size)
	{
		free(data);
		data = NULL;
	}
	if (data)
		data[size] = data[size + 1] = 0;	// a char and a wchar_t terminator
	fclose(file);
	return data;
}

// Every header line of the UTF-16 LocalizedStrings.ini, read by INIFileW, is found at that very line. Its values
// are translations, so a '[' inside a line is not checked here.
static void testShippedWideFile(const char* path, const wchar_t* text)
{
	int headers = 0;
	for (const wchar_t* line = text[0] == 0xFEFF ? text + 1 : text; *line; )
	{
		const wchar_t* end = wcsstr(line, L"\r\n");
		size_t lineLen = end ? (size_t)(end - line) : wcslen(line);
		if (line[0] == L'[')
		{
			const wchar_t* close = wmemchr(line, L']', lineLen);
			CHECK(close != NULL);
			if (close)
			{
				wchar_t name[64] = L"";
				size_t nameLen = close - line - 1;
				if (nameLen < sizeof(name) / sizeof(name[0]))
				{
					wmemcpy(name, line + 1, nameLen);
					name[nameLen] = 0;
					headers++;
					if (findIniSection(text, name) != close + 1)
						printf("  %s: [%ls] is not found at its own header\n", path, name);
					CHECK(findIniSection(text, name) == close + 1);
				}
			}
		}
		if (!end)
			break;
		line = end + 2;
	}
	CHECK(headers >= 10);
	printf("  %s: %d section headers (UTF-16)\n", path, headers);
}

// Every header line of a shipped INI is found at that very line, and no other line names a section in brackets, as
// the [GENERAL] comment once did: a simpler INI reader (an older PlugY, a script) would take it for the header.
static void testShippedFile(const char* path)
{
	char* text = readFile(path);
	CHECK(text != NULL);
	if (!text)
	{
		printf("  cannot read %s\n", path);
		return;
	}
	if ((unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE)
	{
		testShippedWideFile(path, (const wchar_t*)text);
		free(text);
		return;
	}
	int headers = 0;
	for (char* line = text; *line; )
	{
		char* end = strstr(line, "\r\n");
		size_t lineLen = end ? (size_t)(end - line) : strlen(line);
		if (line[0] == '[')
		{
			char* close = (char*)memchr(line, ']', lineLen);
			CHECK(close != NULL);
			if (close)
			{
				char name[64] = "";
				size_t nameLen = close - line - 1;
				if (nameLen < sizeof(name))
				{
					memcpy(name, line + 1, nameLen);
					name[nameLen] = 0;
					headers++;
					if (findIniSection(text, name) != close + 1)
						printf("  %s: [%s] is not found at its own header\n", path, name);
					CHECK(findIniSection(text, name) == close + 1);
				}
			}
		}
		if (!end)
			break;
		line = end + 2;
	}
	CHECK(headers >= 10);
	// A '[' anywhere but at a header's start is where a bracketed section name would hide.
	int strays = 0;
	for (char* line = text; *line; )
	{
		char* end = strstr(line, "\r\n");
		size_t lineLen = end ? (size_t)(end - line) : strlen(line);
		char* open = (char*)memchr(line, '[', lineLen);
		if (open && open != line)
		{
			strays++;
			printf("  %s: '[' inside the line \"%.60s\"\n", path, line);
		}
		if (!end)
			break;
		line = end + 2;
	}
	CHECK(strays == 0);
	printf("  %s: %d section headers\n", path, headers);
	free(text);
}

int main(int argc, char** argv)
{
	testHeaders();
	testNotHeaders();
	testWide();
	for (int i = 1; i < argc; i++)
		testShippedFile(argv[i]);
	CHECK(argc >= 5);	// the four shipped INI files
	printf("%d passed, %d failed\n", nbPassed, nbFailed);
	return nbFailed ? 1 : 0;
}

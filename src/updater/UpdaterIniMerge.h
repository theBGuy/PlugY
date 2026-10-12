/*=================================================================
	PlugY4Kolbot updater: adds the options a kept PlugY.ini lacks, by the installer's rules (AddMissingOptions in
	installer/PlugY.nsi), so a zip-style update shows new options too. tests/UpdaterTests.cpp covers it.
=================================================================*/
#pragma once

#include <windows.h>

/* Adds to ini each option of templ (the release's PlugY.ini) that ini lacks, set to its value in defaults (the
   installed PlugY\PlugYDefault.ini, which PlugY already read it from), else templ's. An option already in ini, even
   empty, keeps its value, and [LAUNCHING] is left alone: PlugY.exe reads it from PlugY.ini only. When ini holds none
   of a section's options, an empty line is written at its end first, so a section appended there stands apart (if
   ini has the header without those options, they go under it and the empty line stays at the end). Returns the number
   of options added, or -1 when templ can't be read or a write fails (options added before it stay). */
int UpdaterAddMissingOptions(const wchar_t* ini, const wchar_t* templ, const wchar_t* defaults);

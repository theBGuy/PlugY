// The installer's AddMissingOptions (installer/PlugY.nsi) reads the shipped PlugY.ini line by line: a section header
// or an option must start its line exactly as these tests require, or an upgrade silently skips the option. It writes
// each missing option with its PlugYDefault.ini value, the one PlugY already used, so every option must be there too.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { readLF } from "./changelog-lib.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const SHIPPED_INI = join(repoRoot, "game-files", "PlugY.ini");
const DEFAULT_INI = join(repoRoot, "game-files", "PlugY", "PlugYDefault.ini");

// Windows-1252 files: latin1 keeps every byte. PlugY's own parser needs CRLF.
function readLines(path) {
  const text = readFileSync(path, "latin1");
  assert.equal(text.replace(/\r\n/g, "").includes("\n"), false, `${path} has a line ending without CR`);
  return text.split("\r\n");
}

/** Options as the Windows profile API sees them: "SECTION/KEY" (upper case) -> value. */
function readOptions(path) {
  const options = new Map();
  let section = null;
  for (const raw of readLines(path)) {
    const line = raw.trim();
    if (!line || line.startsWith(";")) continue;
    if (line.startsWith("[")) {
      section = line.slice(1, line.indexOf("]")).trim().toUpperCase();
      continue;
    }
    const eq = line.indexOf("=");
    if (section !== null && eq > 0) {
      const key = `${section}/${line.slice(0, eq).trim().toUpperCase()}`;
      if (!options.has(key)) options.set(key, line.slice(eq + 1).trim());
    }
  }
  return options;
}

test("every line of the shipped PlugY.ini is one the installer's option merge reads as written", () => {
  const sections = new Set();
  const keys = new Set();
  let section = null;
  for (const [i, line] of readLines(SHIPPED_INI).entries()) {
    const where = `PlugY.ini line ${i + 1}: ${JSON.stringify(line)}`;
    // NSIS FileRead returns at most 1023 characters, so a longer line's tail would be read as a line of its own.
    assert.ok(line.length + 2 < 1000, `${where} is too long for FileRead`);
    if (line === "" || line.startsWith(";")) continue;
    // FileRead and WriteINIStr go through the system ANSI code page, which keeps only ASCII alike everywhere.
    assert.ok(/^[\x20-\x7E]*$/.test(line), `${where} has a byte outside printable ASCII`);
    const header = /^\[([^\]\s][^\]]*[^\]\s]|[^\]\s])\] *$/.exec(line);
    if (header) {
      section = header[1].toUpperCase();
      assert.ok(!sections.has(section), `${where} repeats a section`);
      sections.add(section);
      continue;
    }
    const option = /^([A-Za-z0-9_]+)=(.*)$/.exec(line);
    assert.ok(option, `${where} is neither blank, a ";" comment, a [SECTION] header nor a Key=value option`);
    assert.ok(section !== null, `${where} comes before any section`);
    // The profile API trims a value and drops quotes around it, so such a value would not round-trip.
    assert.equal(option[2], option[2].trim(), `${where} has spaces around its value`);
    assert.ok(!/^".*"$|^'.*'$/.test(option[2]), `${where} has a quoted value`);
    const key = `${section}/${option[1].toUpperCase()}`;
    assert.ok(!keys.has(key), `${where} repeats an option`);
    keys.add(key);
  }
  // Read with the profile API's rules the file gives the same options, so the parse above covered them all.
  assert.deepEqual([...readOptions(SHIPPED_INI).keys()], [...keys]);
  assert.ok(keys.has("STAT BONUS/MAGICFIND"), "the parse found none of the options");
});

// [LAUNCHING] is read by PlugY.exe from PlugY.ini alone, so the merge leaves it out.
test("every option the merge can add is in PlugYDefault.ini, so it gets the defaults file's value, not the shipped one", () => {
  const defaults = readOptions(DEFAULT_INI);
  const merged = [...readOptions(SHIPPED_INI).keys()].filter((key) => !key.startsWith("LAUNCHING/"));
  assert.ok(merged.length > 50, "the shipped PlugY.ini lost its options");
  assert.deepEqual(merged.filter((key) => !defaults.has(key)), []);
});

test("the installer adds the missing options once PlugYDefault.ini is installed", () => {
  const nsi = readLF(join(repoRoot, "installer", "PlugY.nsi"));
  const core = nsi.slice(nsi.indexOf('Section "!$(SECTION_NAME_CORE)" Core'));
  const coreEnd = core.indexOf("\nSectionEnd");
  const defaults = core.indexOf('File "${D2FILES}\\PlugY\\PlugYDefault.ini"');
  const merge = core.indexOf("  Call AddMissingOptions\n");
  assert.ok(defaults > 0 && defaults < coreEnd, "the Core section no longer installs PlugYDefault.ini");
  assert.ok(merge > defaults && merge < coreEnd, "AddMissingOptions must run in Core, after PlugYDefault.ini is installed");
  // The pre-release flip must see the merged file, whatever default the merge writes for a missing ActiveLogFile.
  const flip = core.indexOf("!ifdef PRERELEASE_ID\n");
  assert.ok(flip > merge && flip < coreEnd, "the pre-release ActiveLogFile flip must run after AddMissingOptions");
  const fn = nsi.slice(nsi.indexOf("Function AddMissingOptions\n"));
  const body = fn.slice(0, fn.indexOf("FunctionEnd"));
  assert.ok(body.includes('${IfThen} $2 == "LAUNCHING" ${|} StrCpy $2 "" ${|}'), "the merge no longer skips [LAUNCHING]");
  assert.ok(body.includes('File "/oname=$PLUGINSDIR\\PlugY.ini" "${D2FILES}\\PlugY.ini"'));
  assert.ok(body.includes(String.raw`ReadINIStr $4 "$INSTDIR\PlugY\PlugYDefault.ini" $2 $3`));
  assert.ok(body.includes(String.raw`WriteINIStr "$INSTDIR\PlugY.ini" $2 $3 $4`));
});

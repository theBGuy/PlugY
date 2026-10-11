#!/usr/bin/env node
// Prints the release notes for a tag, for use as the GitHub Release body. A
// stable tag gets its CHANGELOG.md section. A pre-release tag gets everything
// since the last stable release: the [Unreleased] body merged with the pending
// changelog.d fragments (a pre-release never consumes them). A stable tag with
// no section falls back to a pointer line; the script always exits 0 so a notes problem
// never fails the release. Usage:
//   node scripts/changelog-extract.mjs v14.4.0
//   node scripts/changelog-extract.mjs v14.4.0-rc.1
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  findUnreleased,
  hasEntries,
  lastStableVersion,
  pendingBody,
  prereleaseIntro,
  readFragments,
} from "./changelog-lib.mjs";
import { parseVersion } from "./version-lib.mjs";

// GitHub renders a release body like a comment, turning every newline into a
// line break, so CHANGELOG.md's wrapped lines would break mid-sentence there.
// The body joins each wrapped line onto the line it continues; CHANGELOG.md
// itself stays wrapped. Lines that open a block (heading, list item, quote,
// table, rule, link definition, HTML), code, and hard breaks keep their own line.
const FENCE_OPEN_RE = /^\s*(`{3,}|~{3,})/;
const BLOCK_START_RE = /^\s*(#{1,6}(\s|$)|[-*+](\s|$)|\d{1,9}[.)](\s|$)|>|\||<|\[[^\]]+\]:)/;
const NOT_JOINED_ONTO_RE = /^\s*(#{1,6}(\s|$)|\||<|\[[^\]]+\]:)/;
const RULE_RE = /^ {0,3}([-*_=])(\s*\1){2,}\s*$/;
const isTableDelimiter = (line) => /^[\s|:-]+$/.test(line) && line.includes("|") && line.includes("-");

/** `text` (LF) with each wrapped continuation line joined onto the line it continues. */
export function unwrapMarkdown(text) {
  const out = [];
  let fence = null; // the closing-fence pattern while inside fenced code
  let table = false; // table rows run until a blank line
  let prev = "blank"; // the last line: blank, text (a continuation may join it), code or other
  for (const line of text.split("\n")) {
    if (fence) {
      out.push(line);
      if (fence.test(line)) {
        fence = null;
        prev = "other";
      }
      continue;
    }
    const open = line.match(FENCE_OPEN_RE);
    if (open) {
      fence = new RegExp(`^\\s*${open[1][0] === "`" ? "`" : "~"}{${open[1].length},}\\s*$`);
      out.push(line);
      continue;
    }
    if (line.trim() === "") {
      out.push(line);
      table = false;
      prev = "blank";
      continue;
    }
    if (isTableDelimiter(line)) table = true;
    const hardBreak = / {2,}$|\\$/.test(line);
    if (prev === "text" && !table && !BLOCK_START_RE.test(line) && !RULE_RE.test(line)) {
      const last = out[out.length - 1].trimEnd();
      out[out.length - 1] = `${last} ${hardBreak ? line.trimStart() : line.trim()}`;
      if (hardBreak) prev = "other";
      continue;
    }
    out.push(line);
    // Indented code only starts after a blank line; within a paragraph the same indent is a continuation.
    if (/^( {4}|\t)/.test(line) && (prev === "blank" || prev === "code")) prev = "code";
    else if (table || hardBreak || RULE_RE.test(line) || NOT_JOINED_ONTO_RE.test(line)) prev = "other";
    else prev = "text";
  }
  return out.join("\n");
}

// GitHub rejects a release body over 125,000 characters, and Linux caps any
// single env var (a workflow step receiving the body as an input) at 128 KiB
// (MAX_ARG_STRLEN). When the notes don't fit, keep the top and link out to the
// full file. Budget stays under both limits with margin.
export const MAX_BYTES = 120_000;
const byteLen = (s) => Buffer.byteLength(s, "utf8");

const pointer = (tag) => `See the CHANGELOG for ${tag || "this release"}.`;

// The section under "## [version]" (with or without a trailing date), trimmed.
// The compare-link definitions closing the file belong to no section, so the
// oldest section stops before them.
function stableSection(lines, version) {
  const esc = version.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const header = new RegExp(`^##\\s*\\[${esc}\\]`);
  const start = lines.findIndex((l) => header.test(l));
  if (start === -1) return "";
  let end = lines.length;
  for (let i = start + 1; i < lines.length; i++) {
    if (/^##\s*\[/.test(lines[i]) || /^\[[^\]]+\]:\s*\S/.test(lines[i])) {
      end = i;
      break;
    }
  }
  return lines
    .slice(start + 1, end)
    .join("\n")
    .trim();
}

/**
 * Build the release body for `tag`. Pure: `changelogText` is CHANGELOG.md's
 * text (null when unreadable) and `groups` the pending fragments, used only
 * for a pre-release tag. `env` supplies GITHUB_REPOSITORY / GITHUB_SERVER_URL
 * for the truncation footer's link.
 */
export function buildReleaseNotes({ tag, changelogText, groups, env = {} }) {
  if (changelogText === null || changelogText === undefined) {
    return "See CHANGELOG.md for release notes.\n";
  }
  const version = (tag ?? "").replace(/^v/, "");
  const parsed = parseVersion(version);
  const lines = changelogText.split(/\r?\n/);

  let notes;
  if (parsed?.channel) {
    const { body } = findUnreleased(lines);
    const pending = pendingBody(
      body,
      groups ?? { Added: [], Changed: [], Fixed: [] },
    )
      .join("\n")
      .trim();
    // No pointer either way: the repo's CHANGELOG.md never gets a pre-release section.
    const withEntries = hasEntries(pending.split("\n"));
    const intro = prereleaseIntro(parsed.base, withEntries, lastStableVersion(lines));
    notes = withEntries ? `${intro}\n\n${pending}` : intro;
  } else {
    notes = stableSection(lines, version) || pointer(tag);
  }
  notes = unwrapMarkdown(notes);

  if (byteLen(`${notes}\n`) <= MAX_BYTES) return `${notes}\n`;

  // GITHUB_REPOSITORY / GITHUB_SERVER_URL are always set in Actions; fall back
  // to a bare filename when run locally.
  const repo = env.GITHUB_REPOSITORY;
  const server = env.GITHUB_SERVER_URL ?? "https://github.com";
  const changelogUrl = repo
    ? `${server}/${repo}/blob/${tag}/CHANGELOG.md`
    : "CHANGELOG.md";
  const footer = `\n\n---\n\n_Release notes truncated; see the [full changelog](${changelogUrl}) for the rest._\n`;
  const budget = MAX_BYTES - byteLen(footer);

  // Keep whole lines only, so the truncated markdown still renders cleanly.
  let kept = "";
  let used = 0;
  for (const line of notes.split("\n")) {
    const chunk = `${line}\n`;
    const chunkBytes = byteLen(chunk);
    if (used + chunkBytes > budget) break;
    kept += chunk;
    used += chunkBytes;
  }
  return `${kept.trimEnd()}${footer}`;
}

function main() {
  const tag = process.argv[2] ?? "";
  const root = join(dirname(fileURLToPath(import.meta.url)), "..");
  let out;
  try {
    let changelogText = null;
    try {
      changelogText = readFileSync(join(root, "CHANGELOG.md"), "utf8");
    } catch {
      // unreadable: buildReleaseNotes prints the generic pointer
    }
    // Only a pre-release reads the fragments, so a malformed one can't touch a
    // stable release's notes.
    const groups = parseVersion(tag.replace(/^v/, ""))?.channel
      ? readFragments(join(root, "changelog.d")).groups
      : null;
    out = buildReleaseNotes({ tag, changelogText, groups, env: process.env });
  } catch (err) {
    // A malformed fragment must not fail the release; the pointer still ships.
    process.stderr.write(`changelog-extract: ${err.message}\n`);
    out = `${pointer(tag)}\n`;
  }
  process.stdout.write(out);
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  main();
}

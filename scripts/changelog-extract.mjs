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
// The body joins wrapped lines back together only in the shape changelog
// entries take: a paragraph, or a list item with one space after its marker
// ("- ", "1. "), starting in column 0 and continued by lines of plain text. Every
// other line is kept as written, so what this doesn't recognise renders as it
// does today. From the first code fence or raw HTML block (<pre>, a comment)
// on, nothing is joined: their blank lines and look-alike closers make pairing
// them guesswork. CHANGELOG.md itself stays wrapped.

// CommonMark's blank lines and line-edge whitespace are spaces and tabs only; other spaces are content.
// The trims scan characters: an end-anchored regex retries every start and goes quadratic on long runs.
const BLANK_RE = /^[ \t]*$/;
const isSpace = (c) => c === " " || c === "\t";
const trimStartTabs = (s) => {
  let a = 0;
  while (a < s.length && isSpace(s[a])) a++;
  return s.slice(a);
};
const trimTabs = (s) => {
  const t = trimStartTabs(s);
  let b = t.length;
  while (b > 0 && isSpace(t[b - 1])) b--;
  return t.slice(0, b);
};

// The quote and list markers before a line's content.
const CONTAINER_RE = /^([ \t]*(>[ \t]?|[-*+]([ \t]+|$)|\d{1,9}[.)]([ \t]+|$)))*/;
const QUOTE_RE = /^([ \t]*>[ \t]?)*/;
const FENCE_RE = /^[ \t]*(`{3,}|~{3,})(.*)$/;
const MATH_RE = /^[ \t]*\$\$[ \t]*$/; // a GitHub math block, whose TeX comments run to the end of a line
const RAW_HTML_RE = /^[ \t]*(<(pre|script|style|textarea)([ \t>]|$)|<!--|<\?|<![A-Za-z]|<!\[CDATA\[)/i;
// A link label may hold escaped brackets.
const LABEL = String.raw`\[(?:\\.|[^\]\\])*\]:`;
// A list item that a continuation may join: one space after the marker, then content.
const ITEM_RE = /^([-*+]|\d{1,9}[.)]) (?=[^ \t])/;
// Content that opens or belongs to some other block: a heading, list item, quote, table, HTML,
// alert or link definition.
const NOT_PLAIN_RE = new RegExp(
  String.raw`^(#{1,6}([ \t]|$)|[-*+]([ \t]|$)|\d{1,9}[.)]([ \t]|$)|>|\||<|\[!|` + String.raw`${LABEL}|[^[]*\]:)`,
);
const UNDERLINE_RE = /^[ \t]*(=+|-+)[ \t]*$/;
const RULE_RE = /^[ \t]*([-*_])([ \t]*\1){2,}[ \t]*$/;
// A list item or quote moves the indent its underlines and rules may have, so any indent counts.
const isUnderline = (line) => UNDERLINE_RE.test(line) || UNDERLINE_RE.test(line.replace(CONTAINER_RE, ""));
const isRule = (line) => RULE_RE.test(line) || RULE_RE.test(line.replace(CONTAINER_RE, ""));
const isPlain = (content) =>
  content !== "" && !NOT_PLAIN_RE.test(content) && !isUnderline(content) && !isRule(content);
// A delimiter row needs a pipe or a colon: a bare run of dashes is a setext underline or a rule.
const isTableDelimiter = (line) =>
  line !== undefined &&
  [line, line.replace(QUOTE_RE, "")].some(
    (l) => /^[ \t|:-]+$/.test(l) && l.includes("-") && (l.includes("|") || l.includes(":")),
  );
// A table, an HTML block or a link definition may hold line breaks of its own, so no join enters
// one until the next blank line.
const BLOCK_CONTENT_RE = new RegExp(String.raw`^[ \t]*([|<]|${LABEL}|[^[]*\]:)`);
const opensBlock = (line, next) =>
  isTableDelimiter(line) || isTableDelimiter(next) || BLOCK_CONTENT_RE.test(line.replace(CONTAINER_RE, ""));
// A link destination in <...> can't span a line break, so joining one would make a broken link work.
const OPEN_ANGLE_RE = /\]\([ \t]*<(?:\\.|[^>\\])*$/;
// Inline <pre>, <code> and <textarea> keep their line breaks; whether one is still open after `part`.
const leavesRawOpen = (part, open) => {
  for (const m of part.matchAll(/<(\/?)(pre|code|textarea)\b/gi)) open = m[1] === "";
  return open;
};
// The length of the backtick run of a code span still open after `part` (0: none). A code span keeps
// the next line's leading spaces, which a join trims; a run that never closes is counted as open too.
const leavesTicksOpen = (part, open) => {
  for (let k = 0; k < part.length; ) {
    if (!open && part[k] === "\\") k += 2;
    else if (part[k] !== "`") k++;
    else {
      let n = 0;
      while (part[k + n] === "`") n++;
      if (!open) open = n;
      else if (n === open) open = 0;
      k += n;
    }
  }
  return open;
};

// A backtick fence's info string can't hold a backtick, so "```x``` text" is inline code.
const opensFence = (content) => {
  const m = content.match(FENCE_RE);
  return (m !== null && !(m[1][0] === "`" && m[2].includes("`"))) || MATH_RE.test(content);
};

/** `text` (LF) with each wrapped paragraph or list-item line joined onto the line it continues. */
export function unwrapMarkdown(text) {
  const lines = text.split("\n");
  // Lines with an underline-like line below them in their paragraph stay as written: a setext heading's
  // anchor is built from its text as written, and a rule there means more than one block.
  const underlined = new Array(lines.length + 1).fill(false);
  for (let j = lines.length - 1; j >= 0; j--)
    underlined[j] = !BLANK_RE.test(lines[j]) && (isUnderline(lines[j]) || underlined[j + 1]);
  // Each output line is kept as its parts, so a join touches only the newest part, never the whole line.
  const out = [];
  let joinable = false; // the last line is a paragraph or list-item line a plain continuation may join
  let block = false; // inside a table, an HTML block or a link definition: no joins until a blank line
  let openAngle = false; // the last line ends inside a link destination's <...>
  let rawOpen = false; // an inline <pre>, <code> or <textarea> is open in this paragraph
  let ticksOpen = 0; // a code span is open in this paragraph
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (opensFence(line.replace(CONTAINER_RE, "")) || RAW_HTML_RE.test(line.replace(CONTAINER_RE, ""))) {
      for (const rest of lines.slice(i)) out.push([rest]);
      break;
    }
    // An inline <pre>, <code> or <textarea> left open at a paragraph's end can run on through the
    // HTML after it, so it ends all joining like a fence.
    if (BLANK_RE.test(line) && rawOpen) {
      for (const rest of lines.slice(i)) out.push([rest]);
      break;
    }
    if (BLANK_RE.test(line)) {
      out.push([line]);
      joinable = false;
      block = false;
      ticksOpen = 0;
      continue;
    }
    if (opensBlock(line, lines[i + 1])) block = true;
    const hardBreak = line.endsWith("  ") || line.endsWith("\\");
    const rawBefore = rawOpen;
    const ticksBefore = ticksOpen;
    rawOpen = leavesRawOpen(line, rawBefore);
    ticksOpen = leavesTicksOpen(line, ticksBefore);
    const inSpan = rawBefore || ticksBefore > 0 || openAngle;
    if (joinable && !block && !underlined[i] && !inSpan && isPlain(trimTabs(line))) {
      // The previous part keeps its trailing space: inside a code span that crosses the break it is text.
      const body = hardBreak ? trimStartTabs(line) : trimTabs(line);
      out[out.length - 1].push(body);
      joinable = !hardBreak;
      openAngle = OPEN_ANGLE_RE.test(body);
      continue;
    }
    out.push([line]);
    const item = line.match(ITEM_RE);
    const content = item ? line.slice(item[0].length) : /^[^ \t]/.test(line) ? line : "";
    joinable = !block && !hardBreak && !underlined[i] && isPlain(content);
    openAngle = OPEN_ANGLE_RE.test(line);
  }
  return out.map((parts) => parts.join(" ")).join("\n");
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

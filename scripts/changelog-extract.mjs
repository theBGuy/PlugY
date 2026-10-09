#!/usr/bin/env node
// Prints the release notes for a tag, for use as the GitHub Release body. A
// stable tag gets its CHANGELOG.md section. A pre-release tag gets everything
// since the last release: the [Unreleased] body merged with the pending
// changelog.d fragments (a pre-release never consumes them). A stable tag with
// no section falls back to a pointer line; the script always exits 0 so a notes problem
// never fails the release. Usage:
//   node scripts/changelog-extract.mjs v14.4.0
//   node scripts/changelog-extract.mjs v14.4.0-rc.1
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { findUnreleased, pendingBody, readFragments } from "./changelog-lib.mjs";
import { parseVersion } from "./version-lib.mjs";

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
    // Entries are bullets, so a body of bare `### Category` headings counts as
    // empty. No pointer either way: a pre-release never gets a CHANGELOG section.
    const hasEntries = pending.split("\n").some((l) => /^\s*[-*+] \S/.test(l));
    notes = hasEntries
      ?`Pre-release of ${parsed.base} for testing; these notes cover everything since the last release.\n\n${pending}`
      : `Pre-release of ${parsed.base} for testing; no changes are recorded since the last release.`;
  } else {
    notes = stableSection(lines, version) || pointer(tag);
  }

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

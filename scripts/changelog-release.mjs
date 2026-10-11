#!/usr/bin/env node
// Prepare a release: stamp the version into src/common/PlugYVersion.h and the 8
// game-files banners, and, for a stable version, assemble the changelog.d/*.md
// fragments into CHANGELOG.md as a new "## [X.Y.Z] - <today>" section and
// delete the consumed fragments. A pre-release stamps only: its notes and its
// packaged CHANGELOG.md (changelog-package.mjs) are rendered from the pending
// fragments, which wait for the stable release.
// Usage:
//   node scripts/changelog-release.mjs 14.4.0            (stable)
//   node scripts/changelog-release.mjs 14.4.0-rc.1       (pre-release: stamp only)
//   node scripts/changelog-release.mjs 14.4.0 --no-merge (append fragments below
//                                          the pending body instead of merging
//                                          into its categories)
//
// The pending "## [Unreleased]" body is preserved: fragments are merged into its
// matching category blocks (existing bytes untouched), then it's promoted to the
// version heading with a fresh empty "## [Unreleased]" left above. The emitted
// heading matches changelog-extract.mjs, so the release workflow keeps working.
// Every file is computed before any is written, so a refusal leaves the tree
// untouched. The script never runs git; review the diff, then commit + tag, or
// just run: node scripts/release.mjs

import { rmSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import {
  assembleChangelog,
  CATEGORY_ORDER,
  countEntries,
  localDate,
  readFragments,
  readLF,
  writeLF,
} from "./changelog-lib.mjs";
import {
  HEADER_PATH,
  isPrerelease,
  planBannerStamp,
  readHeaderFile,
  releasePaths,
  renderHeader,
  versionProblem,
  writeBannerPlans,
  writeHeaderFile,
} from "./version-lib.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

const fail = (msg) => {
  process.stderr.write(`error: ${msg}\n`);
  process.exit(1);
};

const USAGE =
  "usage: node scripts/changelog-release.mjs <version> [--no-merge]\n" +
  "  <version>   the bare version, e.g. 14.4.0 or 14.4.0-rc.1 (no leading 'v')\n" +
  "  --no-merge  append fragments below the pending Unreleased body\n" +
  "  --help      show this message\n";

// Unknown arguments are refused: a typo'd flag must never run as a real prepare.
const args = process.argv.slice(2);
if (args.includes("--help") || args.includes("-h")) {
  process.stdout.write(USAGE);
  process.exit(0);
}
const unknown = args.filter((a) => a.startsWith("-") && a !== "--no-merge");
const positional = args.filter((a) => !a.startsWith("-"));
if (unknown.length > 0 || positional.length !== 1) {
  if (unknown.length > 0) process.stderr.write(`error: unknown option ${unknown.join(", ")}\n`);
  else if (positional.length > 1) process.stderr.write(`error: expected one version, got ${positional.join(" ")}\n`);
  process.stderr.write(USAGE);
  process.exit(1);
}
const noMerge = args.includes("--no-merge");
const version = positional[0];
const problem = versionProblem(version);
if (problem) fail(problem);
const prerelease = isPrerelease(version);
const date = localDate();

// --- 1. Plan every write before making any ------------------------------------
let headerText;
let bannerPlans;
try {
  headerText = renderHeader(readHeaderFile(root), version);
  bannerPlans = planBannerStamp(root, version);
} catch (err) {
  fail(err.message);
}

const changelogPath = join(root, "CHANGELOG.md");
const fragmentDir = join(root, "changelog.d");
let files = [];
let groups;
let changelogText;
if (!prerelease) {
  try {
    ({ files, groups } = readFragments(fragmentDir));
    changelogText = assembleChangelog(readLF(changelogPath), version, groups, {
      noMerge,
      date,
    });
  } catch (err) {
    fail(err.message);
  }
}

// --- 2. Write, then delete consumed fragments (stable only) -------------------
// Everything was planned above, so only an I/O error (a read-only or locked
// file) can stop this midway; the hint names the restore that undoes it.
const written = [];
try {
  if (!prerelease) {
    writeLF(changelogPath, changelogText);
    written.push("CHANGELOG.md");
  }
  writeHeaderFile(root, headerText);
  written.push(HEADER_PATH);
  writeBannerPlans(bannerPlans);
  written.push(...bannerPlans.map((p) => p.path));
  for (const f of files) rmSync(f);
} catch (err) {
  process.stderr.write(
    `error: ${err.message}\n` +
      "The tree is partly prepared. On a clean tree, undo it with:\n" +
      `  git restore -- ${releasePaths(prerelease).map((p) => (/\s/.test(p) ? `"${p}"` : p)).join(" ")}\n`,
  );
  process.exit(1);
}

// --- 3. Summary -----------------------------------------------------------------
const lines = [
  `Prepared ${version}${prerelease ? " (pre-release)" : ` (${date})`}.`,
  "  wrote:",
  ...written.map((p) => `    ${p}`),
];
if (prerelease) {
  lines.push(
    "  CHANGELOG.md and changelog.d untouched (a pre-release does not consume fragments)",
  );
} else {
  const perCat = CATEGORY_ORDER.filter((c) => groups[c].length)
    .map((c) => `${groups[c].length} ${c}`)
    .join(", ");
  lines.push(
    `  changelog.d fragments consumed: ${countEntries(groups)}${perCat ? ` (${perCat})` : ""}`,
    ...files.map((f) => `    deleted ${relative(root, f).replace(/\\/g, "/")}`),
  );
}
lines.push(
  "",
  "Review the diff, then commit + tag (or let node scripts/release.mjs do it).",
);
process.stdout.write(`${lines.join("\n")}\n`);

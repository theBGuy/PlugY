#!/usr/bin/env node
// Writes the CHANGELOG.md a pre-release's downloads ship. The repo's CHANGELOG.md
// has no section for a pre-release (its fragments wait for the stable release),
// so this renders "## [X.Y.Z-rc.N] - <date>" from the [Unreleased] body merged
// with the pending changelog.d fragments: the entries of the pre-release's
// GitHub notes. Nothing in the tree is written or consumed. scripts/package.ps1
// runs it for a pre-release only; a stable release ships CHANGELOG.md as committed.
// Usage:
//   node scripts/changelog-package.mjs 15.0.0-rc.1 <output file>
import { spawnSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  assembleChangelog,
  countEntries,
  findUnreleased,
  hasEntries,
  lastStableVersion,
  localDate,
  pendingBody,
  prereleaseIntro,
  readFragments,
} from "./changelog-lib.mjs";
import { parseVersion, versionProblem } from "./version-lib.mjs";

/** Why `version` can't be packaged as a pre-release, or null when it can. */
export function prereleaseProblem(version) {
  const problem = versionProblem(version);
  if (problem) return problem;
  return parseVersion(version).channel
    ? null
    : `${version} is a stable version, whose CHANGELOG.md section is committed; ship the file as is`;
}

/**
 * The packaged CHANGELOG.md for pre-release `version`. Pure: `text` is the
 * checked-out CHANGELOG.md, whose line endings the result keeps, and `groups`
 * the pending fragments. Throws on a stable version or a CHANGELOG.md that
 * can't be assembled.
 */
export function prereleaseChangelog({ text, version, groups, date }) {
  const problem = prereleaseProblem(version);
  if (problem) throw new Error(problem);
  const parsed = parseVersion(version);
  const crlf = text.includes("\r\n");
  const lf = text.replace(/\r\n/g, "\n").replace(/^\uFEFF/, "");
  const lines = lf.split("\n");
  const { body } = findUnreleased(lines);
  const intro = prereleaseIntro(parsed.base, hasEntries(pendingBody(body, groups)), lastStableVersion(lines));
  const out = assembleChangelog(lf, version, groups, { date, intro });
  return crlf ? out.replace(/\n/g, "\r\n") : out;
}

// The HEAD commit's date, so packaging the same tag again gives the same file.
// The ceiling keeps git from answering for a repository that encloses a tree
// without history.
export function commitDate(root) {
  const r = spawnSync("git", ["log", "-1", "--format=%cs"], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, GIT_CEILING_DIRECTORIES: dirname(root) },
  });
  const date = r.status === 0 ? (r.stdout ?? "").trim() : "";
  return /^\d{4}-\d{2}-\d{2}$/.test(date) ? date : null;
}

const USAGE =
  "usage: node scripts/changelog-package.mjs <pre-release version> <output file>\n" +
  "  e.g. node scripts/changelog-package.mjs 15.0.0-rc.1 build/out/package/changelog/CHANGELOG.md\n";

function main() {
  const args = process.argv.slice(2);
  if (args.includes("--help") || args.includes("-h")) {
    process.stdout.write(USAGE);
    return;
  }
  if (args.length !== 2 || args.some((a) => a.startsWith("-"))) {
    process.stderr.write(USAGE);
    process.exit(1);
  }
  const [version, outPath] = args;
  const problem = prereleaseProblem(version);
  if (problem) {
    process.stderr.write(`error: ${problem}\n`);
    process.exit(1);
  }
  const root = join(dirname(fileURLToPath(import.meta.url)), "..");
  try {
    const text = readFileSync(join(root, "CHANGELOG.md"), "utf8");
    const { groups } = readFragments(join(root, "changelog.d"));
    let date = commitDate(root);
    if (!date) {
      date = localDate();
      process.stderr.write(`changelog-package: no git history in ${root}; dating the section ${date}\n`);
    }
    writeFileSync(resolve(outPath), prereleaseChangelog({ text, version, groups, date }), "utf8");
    process.stdout.write(
      `Wrote ${outPath}: CHANGELOG.md with a ${version} section dated ${date} (${countEntries(groups)} pending fragment(s))\n`,
    );
  } catch (err) {
    process.stderr.write(`error: ${err.message}\n`);
    process.exit(1);
  }
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  main();
}

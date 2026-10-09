#!/usr/bin/env node
// Prints a Keep-a-Changelog draft from the Conventional Commits since the last
// release tag (or all history if there are no tags). This is a STARTING POINT:
// curate the output into clear, user-facing prose and save each entry as a
// changelog.d fragment. Run with: node scripts/changelog-draft.mjs
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  countEntries,
  readFragments,
  renderSectionLines,
  validateFragments,
} from "./changelog-lib.mjs";
import { commitBase } from "./version-lib.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

// `--check` mode: validate every pending fragment (filename + bullet format)
// without assembling anything, reporting ALL problems at once. Emits GitHub
// Actions ::error annotations so the PR check fails red on the exact file
// BEFORE merge, instead of the release driver tripping over it later.
// Run locally with: node scripts/changelog-draft.mjs --check
if (process.argv.includes("--check")) {
  const dir = join(root, "changelog.d");
  const problems = validateFragments(dir);
  if (problems.length > 0) {
    for (const p of problems) {
      process.stderr.write(`::error file=changelog.d/${p.file}::${p.problem}\n`);
    }
    process.stderr.write(
      `\n${problems.length} invalid changelog fragment(s); see changelog.d/README.md for the format.\n`,
    );
    process.exit(1);
  }
  const { files } = readFragments(dir);
  process.stdout.write(`${files.length} pending fragment(s), all valid.\n`);
  process.exit(0);
}

// `--preview` mode: show the pending changelog.d/ fragments assembled under an
// Unreleased heading, without touching any file. (The default mode, below,
// still drafts a starting point from the Conventional Commits since the last tag.)
if (process.argv.includes("--preview")) {
  const { groups } = readFragments(join(root, "changelog.d"));
  if (countEntries(groups) === 0) {
    process.stdout.write(
      "No pending changelog.d fragments. Add one as changelog.d/<added|changed|fixed>-<slug>.md.\n",
    );
    process.exit(0);
  }
  process.stdout.write(
    `${renderSectionLines("## [Unreleased] (pending fragments)", groups).join("\n").trimEnd()}\n`,
  );
  process.exit(0);
}

// Array args, never a shell string (Windows quoting). Null on failure.
const git = (...args) => {
  const r = spawnSync("git", args, { cwd: root, encoding: "utf8" });
  if (r.error || r.status !== 0) return null;
  return (r.stdout || "").trim();
};

// The last STABLE release tag, the same base release.mjs counts commits from
// (notes cover everything since it); before the first one, the nearest
// non-pre-release tag (the inherited upstream v14.03).
const lastTag = commitBase({
  tagListOutput: git("tag", "--list", "v*"),
  describeTag: git("describe", "--tags", "--abbrev=0", "--exclude", "v*-*"),
});

const log = git(
  "log",
  ...(lastTag ? [`${lastTag}..HEAD`] : []),
  "--pretty=format:%s",
);
if (log === null) {
  process.stderr.write("error: git log failed (is this a git checkout?)\n");
  process.exit(1);
}
const subjects = log ? log.split("\n") : [];

// Conventional-commit type → Keep a Changelog group. Noise types are skipped.
const TYPE_GROUP = {
  feat: "Added",
  fix: "Fixed",
  perf: "Changed",
  refactor: "Changed",
  revert: "Changed",
};
const SKIP = new Set(["chore", "docs", "style", "test", "ci", "build"]);

const groups = { Added: [], Changed: [], Fixed: [] };
for (const subject of subjects) {
  const m = subject.match(/^(\w+)(?:\(([^)]*)\))?(!)?:\s*(.+)$/);
  if (!m) continue;
  const [, type, scope, _bang, desc] = m;
  if (SKIP.has(type)) continue;
  const group = TYPE_GROUP[type];
  if (!group) continue;
  const body = scope ? `**${scope}:** ${desc}` : desc;
  groups[group].push(`- ${body.charAt(0).toUpperCase()}${body.slice(1)}`);
}

const out = ["## [Unreleased]", ""];
for (const [name, items] of Object.entries(groups)) {
  if (items.length === 0) continue;
  out.push(`### ${name}`, "", ...items, "");
}
const body = countEntries(groups) > 0 ? out.join("\n").trim() : "";
process.stdout.write(
  `${body || `No user-facing commits since ${lastTag ?? "the start of history"}.`}\n\n` +
    `// ^ Draft only (since ${lastTag ?? "the start of history"}): rewrite these into curated,\n` +
    "// user-facing changelog.d fragments before committing.\n",
);

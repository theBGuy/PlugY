// changelog-lib: fragment validation (the prose tripwire behind
// `node scripts/changelog-draft.mjs --check`) and CHANGELOG.md assembly.
// validateFragments' fail-open direction is a strip rule that swallows too
// much and lets an interrupter through, so every exempt span keeps a fixture
// that must PASS beside prose that must FAIL.
//
// validateFragments reads a directory, so fixtures land in a throwaway
// mkdtemp dir, never in changelog.d/, where a stray fixture would ship as a
// real changelog bullet.
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import {
  assembleChangelog,
  localDate,
  findUnreleased,
  pendingBody,
  readFragments,
  readLF,
  validateFragments,
} from "./changelog-lib.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

/** Run `fn` over a `{ filename: body }` set of fragments in a throwaway dir. */
function withFragments(fixtures, fn) {
  const dir = mkdtempSync(join(tmpdir(), "plugy-changelog-"));
  try {
    for (const [name, body] of Object.entries(fixtures)) {
      writeFileSync(join(dir, name), body, "utf8");
    }
    return fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

const validate = (fixtures) => withFragments(fixtures, validateFragments);

// --- validation ---------------------------------------------------------------

test("a paired interrupter in plain prose is reported", () => {
  const problems = validate({
    "fixed-interrupter.md":
      "- The stash now waits — even mid-switch — until the save settles.\n",
  });
  assert.equal(problems.length, 1);
  assert.equal(problems[0].file, "fixed-interrupter.md");
  assert.match(problems[0].problem, /interrupter/);
});

test("em-dashes inside quoted, bold and code spans are exempt across a wrap", () => {
  // The bold span only closes on the continuation line, so this fails unless
  // the bullet is joined before the spans are stripped.
  const problems = validate({
    "fixed-quoted.md": [
      '- The box reads "Unable to enter game — bad inventory" and the log says **Shared',
      "  stash — not saved**, with `_LOD_.sss — kept` beside it.",
      "",
    ].join("\n"),
  });
  assert.deepEqual(problems, []);
});

test("a single em-dash gloss is fine", () => {
  const problems = validate({
    "changed-gloss.md":
      "- The shared stash is written only when it changed — an idle game never rewrites it.\n",
  });
  assert.deepEqual(problems, []);
});

test("em-dashes are counted per bullet, not per fragment", () => {
  const problems = validate({
    "fixed-two-bullets.md": [
      "- The first bullet has one gloss — just one.",
      "- The second bullet has one too — a separate gloss.",
      "",
    ].join("\n"),
  });
  assert.deepEqual(problems, []);
});

test("a fragment failing an earlier check is not also flagged as an interrupter", () => {
  const problems = validate({
    "fixed-bad-indent.md": [
      "- The stash now waits — even mid-switch — until the save settles.",
      "not indented, so the assembler would concatenate it verbatim.",
      "",
    ].join("\n"),
  });
  assert.equal(problems.length, 1);
  assert.match(problems[0].problem, /indented 2 spaces/);
  assert.doesNotMatch(problems[0].problem, /interrupter/);
});

test("misnamed, empty and non-bullet fragments are all reported at once", () => {
  const problems = validate({
    "README.md": "# ignored\n",
    "add-typo.md": "- Never picked up.\n",
    "added-empty.md": "\n",
    "fixed-prose.md": "Not a bullet.\n",
    "notes.txt": "ignored, not .md\n",
  });
  assert.deepEqual(
    problems.map((p) => p.file),
    ["add-typo.md", "added-empty.md", "fixed-prose.md"],
  );
});

test("the committed changelog.d fragments are valid", () => {
  assert.deepEqual(validateFragments(join(repoRoot, "changelog.d")), []);
});

// --- assembly -----------------------------------------------------------------

const FRAGMENTS = {
  "added-alpha.md": "- Alpha feature, with a wrapped\n  continuation line.\n",
  "fixed-beta.md": "- Beta fix.\n",
  "changed-gamma.md": "- Gamma change.\n",
};

const BASELINE = [
  "# Changelog",
  "",
  "## [Unreleased]",
  "",
  "## [14.3.0] - 2021-09-01",
  "",
  "Upstream baseline.",
  "",
  "[Unreleased]: https://github.com/theBGuy/PlugY4Kolbot/compare/v14.03...HEAD",
  "[14.3.0]: https://github.com/theBGuy/PlugY4Kolbot/tree/v14.03",
  "",
].join("\n");

test("first release compares against the upstream v14.03 tag verbatim", () => {
  const { groups } = withFragments(FRAGMENTS, readFragments);
  const out = assembleChangelog(BASELINE, "14.4.0", groups, { date: "2026-10-08" });
  assert.equal(
    out,
    [
      "# Changelog",
      "",
      "## [Unreleased]",
      "",
      "## [14.4.0] - 2026-10-08",
      "",
      "### Added",
      "",
      "- Alpha feature, with a wrapped",
      "  continuation line.",
      "",
      "### Changed",
      "",
      "- Gamma change.",
      "",
      "### Fixed",
      "",
      "- Beta fix.",
      "",
      "## [14.3.0] - 2021-09-01",
      "",
      "Upstream baseline.",
      "",
      "[Unreleased]: https://github.com/theBGuy/PlugY4Kolbot/compare/v14.4.0...HEAD",
      "[14.4.0]: https://github.com/theBGuy/PlugY4Kolbot/compare/v14.03...v14.4.0",
      "[14.3.0]: https://github.com/theBGuy/PlugY4Kolbot/tree/v14.03",
      "",
    ].join("\n"),
  );

  // The next release chains from the semver tag the first one left behind.
  const next = assembleChangelog(out, "14.5.0", { Added: [], Changed: [], Fixed: ["- Delta fix."] }, {
    date: "2026-11-01",
  });
  assert.match(next, /^\[Unreleased\]: https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/compare\/v14\.5\.0\.\.\.HEAD$/m);
  assert.match(next, /^\[14\.5\.0\]: https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/compare\/v14\.4\.0\.\.\.v14\.5\.0$/m);
  assert.match(next, /^\[14\.4\.0\]: https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/compare\/v14\.03\.\.\.v14\.4\.0$/m);
});

// Release-proof: the base tag is read from the file, so this keeps passing
// after each real release moves [Unreleased] forward.
test("the real CHANGELOG.md assembles against its own compare base", () => {
  // A Windows checkout with core.autocrlf=true has CRLF here; the release tools read it as LF too.
  const text = readLF(join(repoRoot, "CHANGELOG.md"));
  const prevTag = text.match(/^\[Unreleased\]:.*\/compare\/(\S+?)\.\.\.HEAD$/m)[1];
  const out = assembleChangelog(text, "99.0.0", { Added: ["- A."], Changed: [], Fixed: [] }, {
    date: "2026-10-08",
  });
  assert.match(out, /^## \[Unreleased\]\n\n## \[99\.0\.0\] - 2026-10-08\n\n### Added\n\n- A\.\n/m);
  assert.ok(
    out.includes(`\n[Unreleased]: https://github.com/theBGuy/PlugY4Kolbot/compare/v99.0.0...HEAD\n[99.0.0]: https://github.com/theBGuy/PlugY4Kolbot/compare/${prevTag}...v99.0.0\n`),
  );
  assert.match(out, /^\[14\.3\.0\]: https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/tree\/v14\.03$/m);
});

test("fragments merge into a hand-written Unreleased body", () => {
  const text = BASELINE.replace(
    "## [Unreleased]\n\n",
    "## [Unreleased]\n\n### Fixed\n\n- Hand-written fix.\n\n",
  );
  const { groups } = withFragments(FRAGMENTS, readFragments);
  const out = assembleChangelog(text, "14.4.0", groups, { date: "2026-10-08" });
  const section = out.slice(out.indexOf("## [14.4.0]"), out.indexOf("## [14.3.0]"));
  assert.equal(
    section,
    [
      "## [14.4.0] - 2026-10-08",
      "",
      "### Added",
      "",
      "- Alpha feature, with a wrapped",
      "  continuation line.",
      "",
      "### Changed",
      "",
      "- Gamma change.",
      "",
      "### Fixed",
      "",
      "- Hand-written fix.",
      "- Beta fix.",
      "",
      "",
    ].join("\n"),
  );
  // --no-merge appends the rendered blocks below the body instead.
  const appended = assembleChangelog(text, "14.4.0", groups, { date: "2026-10-08", noMerge: true });
  assert.ok(appended.indexOf("- Hand-written fix.") < appended.indexOf("### Added"));
});

test("assembly refuses a changelog without [Unreleased] or its compare link", () => {
  const groups = { Added: [], Changed: [], Fixed: [] };
  assert.throws(
    () => assembleChangelog(BASELINE.replace("## [Unreleased]\n", ""), "14.4.0", groups, { date: "d" }),
    /Unreleased/,
  );
  assert.throws(
    () =>
      assembleChangelog(BASELINE.replace(/^\[Unreleased\]:.*$/m, ""), "14.4.0", groups, { date: "d" }),
    /link definition/,
  );
});

test("pending notes merge the Unreleased body with the fragments", () => {
  const lines = BASELINE.replace("## [Unreleased]\n\n", "## [Unreleased]\n\n### Fixed\n\n- Hand-written fix.\n\n").split("\n");
  const { body } = findUnreleased(lines);
  const out = pendingBody(body, { Added: ["- New thing."], Changed: [], Fixed: ["- Fragment fix."] })
    .join("\n")
    .trim();
  assert.equal(
    out,
    "### Added\n\n- New thing.\n\n### Fixed\n\n- Hand-written fix.\n- Fragment fix.",
  );
});

test("compare-link rewrite keeps the newline and blank line after the [Unreleased] def", () => {
  const groups = { Added: ["- A."], Changed: [], Fixed: [] };
  // [Unreleased] as the last def, with a trailing blank line after the final newline.
  const lastDef = "# C\n\n## [Unreleased]\n\n## [14.3.0] - 2021-09-01\n\n[14.3.0]: https://x/tree/v14.03\n[Unreleased]: https://x/compare/v14.03...HEAD\n\n";
  const out = assembleChangelog(lastDef, "14.4.0", groups, { date: "2026-10-08" });
  assert.ok(
    out.endsWith("[Unreleased]: https://x/compare/v14.4.0...HEAD\n[14.4.0]: https://x/compare/v14.03...v14.4.0\n\n"),
    JSON.stringify(out.slice(-120)),
  );
  // Trailing spaces on the def are tolerated and dropped; the next line survives.
  const spaced = BASELINE.replace("v14.03...HEAD\n", "v14.03...HEAD  \t\n");
  const out2 = assembleChangelog(spaced, "14.4.0", groups, { date: "2026-10-08" });
  assert.ok(out2.includes("/compare/v14.03...v14.4.0\n[14.3.0]: "), out2);
});

test("assembly refuses a version whose section already exists", () => {
  const once = assembleChangelog(BASELINE, "14.4.0", { Added: ["- A."], Changed: [], Fixed: [] }, { date: "2026-10-08" });
  const groups = { Added: [], Changed: [], Fixed: [] };
  assert.throws(() => assembleChangelog(once, "14.4.0", groups, { date: "2026-10-09" }), /already has a '## \[14\.4\.0\]' section/);
  assert.throws(() => assembleChangelog(BASELINE, "14.3.0", groups, { date: "d" }), /already has/);
  // A pre-release-looking prefix is a different version.
  assert.doesNotThrow(() => assembleChangelog(once, "14.4.1", groups, { date: "d" }));
});

test("the release date is the local calendar day", () => {
  // 23:30 local on Oct 8 is Oct 9 in UTC for any zone west of UTC; local wins.
  assert.equal(localDate(new Date(2026, 9, 8, 23, 30)), "2026-10-08");
  assert.equal(localDate(new Date(2026, 0, 5, 0, 1)), "2026-01-05");
});

test("upper-case .MD fragments are validated as well as consumed", () => {
  const problems = validate({ "added-x.MD": "Not a bullet.\n", "README.MD": "# ignored\n" });
  assert.deepEqual(problems.map((p) => p.file), ["added-x.MD"]);
  withFragments({ "added-x.MD": "- Fine.\n" }, (dir) => {
    assert.deepEqual(validateFragments(dir), []);
    assert.equal(readFragments(dir).groups.Added[0], "- Fine.");
  });
});

test("a leading UTF-8 BOM is stripped before a fragment is read or validated", () => {
  const bom = String.fromCharCode(0xfeff);
  withFragments({ "fixed-bom.md": `${bom}- Written by PowerShell 5.1.\r\n` }, (dir) => {
    assert.deepEqual(validateFragments(dir), []);
    assert.equal(readFragments(dir).groups.Fixed[0], "- Written by PowerShell 5.1.");
  });
});

// changelog-package: the CHANGELOG.md a pre-release's downloads ship. The pure
// renderer is checked on fixtures and on the real CHANGELOG.md + changelog.d;
// the CLI runs for real into a throwaway dir, and must leave the tree as it was.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
  copyFileSync,
  cpSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { buildReleaseNotes, unwrapMarkdown } from "./changelog-extract.mjs";
import { localDate, readFragments, readLF } from "./changelog-lib.mjs";
import { commitDate, prereleaseChangelog } from "./changelog-package.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const script = join(repoRoot, "scripts", "changelog-package.mjs");

const empty = { Added: [], Changed: [], Fixed: [] };

const CHANGELOG = [
  "# Changelog",
  "",
  "Intro text.",
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

const GROUPS = {
  Added: ["- Pending feature,\n  wrapped."],
  Changed: [],
  Fixed: ["- Pending fix."],
};

const render = (overrides) =>
  prereleaseChangelog({ text: CHANGELOG, version: "15.0.0-rc.1", groups: GROUPS, date: "2026-10-11", ...overrides });

test("a pre-release gets its own dated section with the intro and the pending entries", () => {
  assert.equal(
    render(),
    [
      "# Changelog",
      "",
      "Intro text.",
      "",
      "## [Unreleased]",
      "",
      "## [15.0.0-rc.1] - 2026-10-11",
      "",
      "Pre-release of 15.0.0 for testing; these notes cover everything since 14.3.0.",
      "",
      "### Added",
      "",
      "- Pending feature,",
      "  wrapped.",
      "",
      "### Fixed",
      "",
      "- Pending fix.",
      "",
      "## [14.3.0] - 2021-09-01",
      "",
      "Upstream baseline.",
      "",
      "[Unreleased]: https://github.com/theBGuy/PlugY4Kolbot/compare/v15.0.0-rc.1...HEAD",
      "[15.0.0-rc.1]: https://github.com/theBGuy/PlugY4Kolbot/compare/v14.03...v15.0.0-rc.1",
      "[14.3.0]: https://github.com/theBGuy/PlugY4Kolbot/tree/v14.03",
      "",
    ].join("\n"),
  );
});

test("fragments merge into a hand-written Unreleased body", () => {
  const text = CHANGELOG.replace("## [Unreleased]\n\n", "## [Unreleased]\n\n### Fixed\n\n- Hand-written fix.\n\n");
  const out = render({ text });
  const section = out.slice(out.indexOf("## [15.0.0-rc.1]"), out.indexOf("## [14.3.0]"));
  assert.equal(
    section,
    [
      "## [15.0.0-rc.1] - 2026-10-11",
      "",
      "Pre-release of 15.0.0 for testing; these notes cover everything since 14.3.0.",
      "",
      "### Added",
      "",
      "- Pending feature,",
      "  wrapped.",
      "",
      "### Fixed",
      "",
      "- Hand-written fix.",
      "- Pending fix.",
      "",
      "",
    ].join("\n"),
  );
});

test("with nothing pending the section says so, and bare category headings are not entries", () => {
  for (const text of [CHANGELOG, CHANGELOG.replace("## [Unreleased]\n\n", "## [Unreleased]\n\n### Fixed\n\n")]) {
    const out = render({ text, groups: empty });
    const section = out.slice(out.indexOf("## [15.0.0-rc.1]"), out.indexOf("## [14.3.0]"));
    assert.match(section, /^## \[15\.0\.0-rc\.1\] - 2026-10-11\n\nPre-release of 15\.0\.0 for testing; no changes are recorded since 14\.3\.0\.\n/);
    assert.doesNotMatch(section, /^- /m);
  }
});

test("the checkout's line endings are kept", () => {
  const crlf = render({ text: CHANGELOG.replace(/\n/g, "\r\n") });
  assert.equal(crlf, render().replace(/\n/g, "\r\n"));
  assert.doesNotMatch(crlf, /[^\r]\n/);
  assert.doesNotMatch(render(), /\r/);
  // Any CRLF makes the whole file CRLF, so a mixed input never yields a lone LF.
  assert.equal(render({ text: CHANGELOG.replace("\n", "\r\n") }), crlf);
  // A BOM (Windows PowerShell 5.1 writes one) never reaches the heading match.
  assert.equal(render({ text: `\uFEFF${CHANGELOG}` }), render());
});

test("stable, malformed and too-long versions are refused", () => {
  assert.throws(() => render({ version: "15.0.0" }), /stable version/);
  assert.throws(() => render({ version: "v15.0.0-rc.1" }), /not a valid version/);
  assert.throws(() => render({ version: "15.0.0-pre.1" }), /not a valid version/);
  assert.throws(() => render({ version: "100.100.0-rc.1" }), /13-character limit/);
});

test("a CHANGELOG.md without an Unreleased section, or already holding the version, is refused", () => {
  assert.throws(() => render({ text: CHANGELOG.replace("## [Unreleased]\n\n", "") }), /\[Unreleased\]/);
  assert.throws(
    () => render({ text: CHANGELOG.replace("## [14.3.0]", "## [15.0.0-rc.1] - 2026-10-01\n\n## [14.3.0]") }),
    /already has a '## \[15\.0\.0-rc\.1\]' section/,
  );
});

test("the packaged section lists exactly what the pre-release's GitHub notes list", () => {
  const text = CHANGELOG.replace("## [Unreleased]\n\n", "## [Unreleased]\n\n### Fixed\n\n- Hand-written fix.\n\n");
  const out = render({ text });
  const section = out.slice(out.indexOf("## [15.0.0-rc.1]"), out.indexOf("## [14.3.0]"));
  const notes = buildReleaseNotes({ tag: "v15.0.0-rc.1", changelogText: text, groups: GROUPS });
  const body = section.split("\n").slice(2).join("\n").trim();
  // The notes join the file's wrapped lines, since GitHub breaks a release body at every newline.
  assert.notEqual(body, notes.trim());
  assert.equal(unwrapMarkdown(body), notes.trim());
});

test("the real CHANGELOG.md and changelog.d render a section holding every pending fragment", () => {
  const { groups } = readFragments(join(repoRoot, "changelog.d"));
  const out = prereleaseChangelog({
    text: readLF(join(repoRoot, "CHANGELOG.md")),
    version: "99.0.0-rc.1",
    groups,
    date: "2026-10-11",
  });
  const start = out.indexOf("## [99.0.0-rc.1] - 2026-10-11\n");
  assert.ok(start > out.indexOf("## [Unreleased]"));
  const section = out.slice(start, out.indexOf("\n## [", start + 1));
  for (const bullet of Object.values(groups).flat()) assert.ok(section.includes(bullet), bullet.slice(0, 60));
  assert.match(out, /^\[99\.0\.0-rc\.1\]: https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/compare\/\S+\.\.\.v99\.0\.0-rc\.1$/m);
});

// --- CLI ------------------------------------------------------------------------

const snapshot = () => {
  const dir = join(repoRoot, "changelog.d");
  return [
    readFileSync(join(repoRoot, "CHANGELOG.md")).toString("base64"),
    ...readdirSync(dir)
      .sort()
      .map((n) => `${n}:${readFileSync(join(dir, n)).toString("base64")}`),
  ].join("\n");
};

function withOutDir(fn) {
  const dir = mkdtempSync(join(tmpdir(), "plugy-changelog-package-"));
  try {
    return fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

const run = (...args) => spawnSync(process.execPath, [script, ...args], { encoding: "utf8" });

// Fixture repositories ignore the developer's own git config (hooks, signing),
// and get a fixed date so a fallback to today can't pass for the commit's date.
const FIXTURE_DATE = "2001-02-03T12:00:00+00:00";
function fixtureGit(cwd, emptyConfig, ...args) {
  return spawnSync("git", ["-c", "user.name=test", "-c", "user.email=test@example.invalid", ...args], {
    cwd,
    encoding: "utf8",
    env: {
      ...process.env,
      GIT_CONFIG_GLOBAL: emptyConfig,
      GIT_CONFIG_NOSYSTEM: "1",
      GIT_AUTHOR_DATE: FIXTURE_DATE,
      GIT_COMMITTER_DATE: FIXTURE_DATE,
    },
  });
}

test("commitDate reads the HEAD commit of the tree's own repository, never an enclosing one", () => {
  withOutDir((dir) => {
    const emptyConfig = join(dir, "gitconfig");
    writeFileSync(emptyConfig, "");
    const outer = join(dir, "outer");
    const inner = join(outer, "inner");
    mkdirSync(inner, { recursive: true });
    assert.equal(commitDate(outer), null);
    assert.equal(fixtureGit(outer, emptyConfig, "init", "-q").status, 0);
    const commit = fixtureGit(outer, emptyConfig, "commit", "-q", "--allow-empty", "-m", "base");
    assert.equal(commit.status, 0, commit.stderr);
    assert.equal(commitDate(outer), "2001-02-03");
    // Plain git would answer for `inner` from the enclosing repository; commitDate must not.
    assert.equal(fixtureGit(inner, emptyConfig, "log", "-1", "--format=%cs").stdout.trim(), "2001-02-03");
    assert.equal(commitDate(inner), null);
  });
});

test("the CLI writes the file, dated by the HEAD commit, and leaves the tree untouched", () => {
  const before = snapshot();
  const date = commitDate(repoRoot);
  assert.ok(date, "the repository has a HEAD commit");
  withOutDir((dir) => {
    const outPath = join(dir, "CHANGELOG.md");
    const r = run("99.0.0-rc.1", outPath);
    assert.equal(r.status, 0, r.stderr);
    assert.doesNotMatch(r.stderr, /no git history/);
    const written = readFileSync(outPath, "utf8");
    assert.match(written, new RegExp(`^## \\[99\\.0\\.0-rc\\.1\\] - ${date}$`, "m"));
    // The checkout's CHANGELOG.md line endings carry over.
    const crlf = readFileSync(join(repoRoot, "CHANGELOG.md"), "utf8").includes("\r\n");
    assert.equal(written.includes("\r\n"), crlf);
  });
  assert.equal(snapshot(), before);
});

test("outside a repository the CLI dates the section today and says so; a bad fragment writes nothing", () => {
  withOutDir((dir) => {
    const tree = join(dir, "tree");
    mkdirSync(join(tree, "scripts"), { recursive: true });
    for (const f of ["changelog-package.mjs", "changelog-lib.mjs", "version-lib.mjs"]) {
      copyFileSync(join(repoRoot, "scripts", f), join(tree, "scripts", f));
    }
    copyFileSync(join(repoRoot, "CHANGELOG.md"), join(tree, "CHANGELOG.md"));
    cpSync(join(repoRoot, "changelog.d"), join(tree, "changelog.d"), { recursive: true });
    const copy = join(tree, "scripts", "changelog-package.mjs");
    const outPath = join(dir, "CHANGELOG.md");

    const today = localDate();
    const r = spawnSync(process.execPath, [copy, "15.0.0-rc.1", outPath], { encoding: "utf8" });
    assert.equal(r.status, 0, r.stderr);
    assert.match(r.stderr, /no git history/);
    const dated = readFileSync(outPath, "utf8").match(/^## \[15\.0\.0-rc\.1\] - (\d{4}-\d{2}-\d{2})$/m);
    // Either side of midnight.
    assert.ok([today, localDate()].includes(dated?.[1]), dated?.[1]);

    rmSync(outPath);
    writeFileSync(join(tree, "changelog.d", "fixed-broken.md"), "not a bullet\n");
    const bad = spawnSync(process.execPath, [copy, "15.0.0-rc.1", outPath], { encoding: "utf8" });
    assert.equal(bad.status, 1);
    assert.match(bad.stderr, /fixed-broken\.md/);
    assert.equal(existsSync(outPath), false);
  });
});

test("the CLI refuses a bad version, a wrong argument list or a missing folder and writes nothing", () => {
  withOutDir((dir) => {
    const outPath = join(dir, "CHANGELOG.md");
    for (const args of [
      ["15.0.0", outPath],
      ["", outPath],
      ["15.0.0-rc.1"],
      ["15.0.0-rc.1", outPath, "extra"],
      ["--force", outPath],
      ["15.0.0-rc.1", join(dir, "missing", "CHANGELOG.md")],
    ]) {
      const r = run(...args);
      assert.equal(r.status, 1, `${args.join(" ")}: ${r.stdout}`);
      assert.doesNotMatch(r.stderr, /no git history/);
      assert.deepEqual(readdirSync(dir), []);
    }
    const help = run("--help");
    assert.equal(help.status, 0);
    assert.match(help.stdout, /^usage: node scripts\/changelog-package\.mjs/);
  });
});

// readme-lag-lib: what the zh-TW readme lags behind. The parsing cases run on canned
// `git log` output; the last cases build a throwaway repository, so the real git sees the
// non-ASCII path.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";

import { EN_README, isReleaseCommit, lastCatchUp, parseLog, ZH_README, zhReadmeLag } from "./readme-lag-lib.mjs";
import { BANNER_FILES } from "./version-lib.mjs";

const CATCH_UP = "docs(readme): catch up the Traditional Chinese readme to";

test("parseLog splits each line at its first tab and keeps null", () => {
  assert.deepEqual(parseLog("a1\tfeat: one\nb2\tfix: two\tthree\n"), [
    { sha: "a1", subject: "feat: one" },
    { sha: "b2", subject: "fix: two\tthree" },
  ]);
  assert.deepEqual(parseLog(""), []);
  assert.equal(parseLog(null), null);
});

test("the readme paths are the banner files' own", () => {
  const paths = BANNER_FILES.map((f) => f.path);
  assert.ok(paths.includes(EN_README) && paths.includes(ZH_README));
});

test("only chore(release): v<version> subjects count as release commits", () => {
  assert.equal(isReleaseCommit({ subject: "chore(release): v15.0.0-rc.1" }), true);
  assert.equal(isReleaseCommit({ subject: "chore(release): v16.0.0" }), true);
  assert.equal(isReleaseCommit({ subject: "chore(release): bump tooling" }), false);
  assert.equal(isReleaseCommit({ subject: "docs(readme): mention chore(release): v1" }), false);
});

test("lastCatchUp takes the newest subject that names a commit; other zh-TW edits don't count", () => {
  const zhLog = parseLog(
    [
      "t9\tdocs(readme): fix a typo in the Traditional Chinese readme",
      "r8\tchore(release): v2.0.0",
      `c2\t${CATCH_UP} 1a2b3c4 (#17)`,
      `c1\t${CATCH_UP} ${"d".repeat(40)}`,
    ].join("\n"),
  );
  const typo = { sha: "t9", subject: "docs(readme): fix a typo in the Traditional Chinese readme" };
  assert.deepEqual(lastCatchUp(zhLog), { sha: "c2", translatedTo: "1a2b3c4", editedSince: [typo] });
  assert.deepEqual(lastCatchUp(zhLog.slice(3)), { sha: "c1", translatedTo: "d".repeat(40), editedSince: [] });
  assert.equal(lastCatchUp(parseLog(`x1\t${CATCH_UP} master\ny1\tdocs(readme): catch up the Traditional Chinese readme`)), null);
});

test("lastCatchUp skips a catch-up that a newer commit reverts, by git's and GitHub's revert subjects", () => {
  const c2 = `${CATCH_UP} 1a2b3c4 (#17)`;
  const c1 = `${CATCH_UP} 0a0b0c0 (#9)`;
  const github = parseLog([`v1\tRevert "${c2}" (#18)`, `c2\t${c2}`, `c1\t${c1}`].join("\n"));
  assert.deepEqual(lastCatchUp(github), {
    sha: "c1",
    translatedTo: "0a0b0c0",
    editedSince: [
      { sha: "v1", subject: `Revert "${c2}" (#18)` },
      { sha: "c2", subject: c2 },
    ],
  });
  const local = parseLog([`v1\tRevert "${c2}"`, `c2\t${c2}`].join("\n"));
  assert.equal(lastCatchUp(local), null);
  // A revert of some other subject leaves the catch-up counted (and listed as a later edit).
  assert.equal(lastCatchUp(parseLog([`v1\tRevert "feat: something else"`, `c2\t${c2}`].join("\n"))).sha, "c2");
});

test("zhReadmeLag lists the English commits after the translated-to commit, release commits and the catch-up dropped", () => {
  const calls = [];
  const git = (...args) => {
    calls.push(args);
    if (args[0] === "rev-parse") return "e1full";
    if (args[0] === "merge-base") return "";
    if (args.includes(ZH_README)) return `c3\t${CATCH_UP} e1abcde\nz0\tfeat: both readmes`;
    return "f8\tfeat: both readmes\nr9\tchore(release): v2.0.0\nc3\tcatch-up subject\ne2\tdocs: English only";
  };
  assert.deepEqual(zhReadmeLag(git), {
    since: "c3",
    translatedTo: "e1full",
    behind: [
      { sha: "f8", subject: "feat: both readmes" },
      { sha: "e2", subject: "docs: English only" },
    ],
    editedSince: [],
  });
  assert.deepEqual(calls[1], ["rev-parse", "--verify", "--quiet", "e1abcde^{commit}"]);
  assert.deepEqual(calls[2], ["merge-base", "--is-ancestor", "e1full", "HEAD"]);
  assert.deepEqual(calls[3], ["log", "--format=%H%x09%s", "e1full..HEAD", "--", EN_README]);
});

test("zhReadmeLag reports each way it can't measure", () => {
  const zhOk = `c3\t${CATCH_UP} e1abcde`;
  const failing = (which) => (...args) => (args[0] === which || args.includes(which) ? null : args[0] === "log" && args.includes(ZH_README) ? zhOk : args[0] === "rev-parse" ? "e1full" : "");
  assert.match(zhReadmeLag(() => null).error, /git log failed for .*讀我檔案/);
  assert.match(zhReadmeLag(() => "f1\tfeat: both").error, /no catch-up commit yet/);
  assert.match(zhReadmeLag(failing("rev-parse")).error, /c3 names e1abcde, which is not a commit here/);
  assert.match(zhReadmeLag(failing("merge-base")).error, /not an ancestor of HEAD/);
  assert.match(zhReadmeLag(failing(EN_README)).error, /git log failed for .*Readme/);
});

test("zhReadmeLag on a real repository: partial edits, a merge race and a revert can't pass for a catch-up", () => {
  const dir = mkdtempSync(join(tmpdir(), "plugy-readme-lag-"));
  try {
    const emptyConfig = join(dir, "gitconfig");
    writeFileSync(emptyConfig, "");
    const repo = join(dir, "repo");
    mkdirSync(join(repo, dirname(EN_README)), { recursive: true });
    const run = (...args) =>
      spawnSync("git", ["-c", "user.name=test", "-c", "user.email=test@example.invalid", ...args], {
        cwd: repo,
        encoding: "utf8",
        env: { ...process.env, GIT_CONFIG_GLOBAL: emptyConfig, GIT_CONFIG_NOSYSTEM: "1" },
      });
    const git = (...args) => {
      const r = run(...args);
      return r.status === 0 && !r.error ? r.stdout.trim() : null;
    };
    const commit = (subject, files) => {
      for (const [path, text] of Object.entries(files)) writeFileSync(join(repo, path), text);
      assert.equal(run("add", "--", ...Object.keys(files)).status, 0);
      assert.equal(run("commit", "-q", "-m", subject).status, 0);
      return git("rev-parse", "HEAD");
    };
    const lag = () => zhReadmeLag(git);
    const shas = (list) => list.map((c) => c.sha);
    assert.equal(run("init", "-q").status, 0);
    commit("feat: both readmes", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    const e1 = commit("docs: English only", { [EN_README]: "en 2" });
    assert.match(lag().error, /no catch-up commit yet/);
    // The catch-up translates e1, but e2 merges before it does.
    const e2 = commit("docs: more English", { [EN_README]: "en 3" });
    const c1 = commit(`${CATCH_UP} ${e1.slice(0, 7)} (#5)`, { [ZH_README]: "zh 2" });
    assert.deepEqual(lag(), { since: c1, translatedTo: e1, behind: [{ sha: e2, subject: "docs: more English" }], editedSince: [] });
    // A later zh-TW edit is no catch-up, and is listed for the releaser to check.
    const typo = commit("docs(readme): fix a typo in the Traditional Chinese readme", { [ZH_README]: "zh 2b" });
    assert.deepEqual(shas(lag().behind), [e2]);
    assert.deepEqual(shas(lag().editedSince), [typo]);
    // A catch-up that also edits the English readme translates its own edit too.
    const c2 = commit(`${CATCH_UP} ${e2}`, { [EN_README]: "en 3b", [ZH_README]: "zh 3" });
    assert.deepEqual(lag().behind, []);
    assert.deepEqual(lag().editedSince, []);
    // Reverting it brings back the previous catch-up and everything after that one.
    assert.equal(run("revert", "--no-edit", "HEAD").status, 0);
    const revert = git("rev-parse", "HEAD");
    assert.equal(lag().since, c1);
    assert.deepEqual(shas(lag().behind), [revert, c2, e2]);
    assert.deepEqual(shas(lag().editedSince), [revert, c2, typo]);
    commit(`${CATCH_UP} ${revert.slice(0, 7)}`, { [ZH_README]: "zh 3c" });
    commit("chore(release): v1.0.0", { [EN_README]: "en 3 v1", [ZH_README]: "zh 3c v1" });
    assert.deepEqual(lag().behind, []);
    assert.deepEqual(lag().editedSince, []);
    const feature = commit("feat: a feature that edits both readmes", { [EN_README]: "en 4", [ZH_README]: "zh 4" });
    assert.deepEqual(shas(lag().behind), [feature]);
    assert.deepEqual(shas(lag().editedSince), [feature]);
    // A catch-up naming a commit that is not in this history can't be measured from.
    assert.equal(run("checkout", "-q", "-b", "side").status, 0);
    const side = commit("docs: English on a side branch", { [EN_README]: "en side" });
    assert.equal(run("checkout", "-q", "-").status, 0);
    commit(`${CATCH_UP} ${side.slice(0, 7)}`, { [ZH_README]: "zh 5" });
    assert.match(zhReadmeLag(git).error, /which is not an ancestor of HEAD/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

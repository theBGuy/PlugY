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

test("parseLog splits each line at its first tab and keeps null", () => {
  assert.deepEqual(parseLog("a1\tfeat: one\nb2\tfix: two\tthree\n"), [
    { sha: "a1", subject: "feat: one" },
    { sha: "b2", subject: "fix: two\tthree" },
  ]);
  assert.deepEqual(parseLog(""), []);
  assert.equal(parseLog(null), null);
});

test("only chore(release): v<version> subjects count as release commits", () => {
  assert.equal(isReleaseCommit({ subject: "chore(release): v15.0.0-rc.1" }), true);
  assert.equal(isReleaseCommit({ subject: "chore(release): v16.0.0" }), true);
  assert.equal(isReleaseCommit({ subject: "chore(release): bump tooling" }), false);
  assert.equal(isReleaseCommit({ subject: "docs(readme): mention chore(release): v1" }), false);
});

test("lastCatchUp skips release commits and commits that also changed the English readme", () => {
  const zhLog = parseLog("r1\tchore(release): v2.0.0\nf2\tfeat: both readmes\nz1\tdocs(readme): catch up\nf0\tfeat: both");
  assert.equal(lastCatchUp(zhLog, new Set(["r1", "f2", "f0"])), "z1");
  assert.equal(lastCatchUp(zhLog, new Set(["r1", "f2", "z1", "f0"])), null);
});

test("zhReadmeLag lists the English commits after the last catch-up, release commits dropped", () => {
  const calls = [];
  const git = (...args) => {
    calls.push(args);
    if (args.includes(ZH_README)) return "r9\tchore(release): v2.0.0\nf8\tfeat: both readmes\nz1\tdocs(readme): catch up";
    if (args.includes("z1..HEAD")) return "f8\tfeat: both readmes\nr9\tchore(release): v2.0.0\ne2\tdocs: English only";
    return "f8\tfeat: both readmes\nr9\tchore(release): v2.0.0\ne2\tdocs: English only\ne0\tfeat: first";
  };
  assert.deepEqual(zhReadmeLag(git), {
    since: "z1",
    behind: [
      { sha: "f8", subject: "feat: both readmes" },
      { sha: "e2", subject: "docs: English only" },
    ],
  });
  assert.deepEqual(calls[2], ["log", "--format=%H%x09%s", "z1..HEAD", "--", EN_README]);
});

test("zhReadmeLag reports git failures and a history with no catch-up commit", () => {
  assert.match(zhReadmeLag(() => null).error, /git log failed for .*讀我檔案/);
  assert.match(zhReadmeLag((...a) => (a.includes(ZH_README) ? "z1\tfeat: zh" : null)).error, /git log failed for .*Readme/);
  assert.match(zhReadmeLag(() => "f1\tfeat: both").error, /no commit has changed it without the English readme/);
});

test("zhReadmeLag on a real repository: a feature that edits both readmes keeps the earlier lag", () => {
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
    assert.equal(run("init", "-q").status, 0);
    commit("feat: both readmes", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    assert.match(zhReadmeLag(git).error, /no commit has changed it without the English readme/);
    const catchUp = commit("docs(readme): catch up the Traditional Chinese readme", { [ZH_README]: "zh 1b" });
    assert.deepEqual(zhReadmeLag(git), { since: catchUp, behind: [] });
    const english = commit("docs: English only", { [EN_README]: "en 2" });
    commit("chore(release): v1.0.0", { [EN_README]: "en 2 v1", [ZH_README]: "zh 1b v1" });
    const feature = commit("feat: a feature that edits both readmes", { [EN_README]: "en 3", [ZH_README]: "zh 3" });
    const lag = zhReadmeLag(git);
    assert.equal(lag.since, catchUp);
    assert.deepEqual(
      lag.behind.map((c) => c.sha),
      [feature, english],
    );
    commit("docs(readme): catch up the Traditional Chinese readme again", { [ZH_README]: "zh 3b" });
    assert.deepEqual(zhReadmeLag(git).behind, []);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

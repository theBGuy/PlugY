// zh-readme-mark.mjs: the command line around planMark. Each case copies the script and the
// libraries it imports into a throwaway repository and runs it there with the real git.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { EN_README, formatMarker, parseMarker, ZH_MARKER, ZH_README } from "./readme-lag-lib.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const SCRIPTS = ["zh-readme-mark.mjs", "readme-lag-lib.mjs", "version-lib.mjs"];

function makeRepo() {
  const dir = mkdtempSync(join(tmpdir(), "plugy-zh-mark-"));
  const emptyConfig = join(dir, "gitconfig");
  writeFileSync(emptyConfig, "");
  const repo = join(dir, "repo");
  const env = { ...process.env, GIT_CONFIG_GLOBAL: emptyConfig, GIT_CONFIG_NOSYSTEM: "1" };
  mkdirSync(join(repo, "scripts"), { recursive: true });
  mkdirSync(join(repo, dirname(EN_README)), { recursive: true });
  for (const name of SCRIPTS) copyFileSync(join(here, name), join(repo, "scripts", name));
  const run = (...args) =>
    spawnSync("git", ["-c", "user.name=test", "-c", "user.email=test@example.invalid", ...args], {
      cwd: repo,
      encoding: "utf8",
      env,
    });
  const git = (...args) => run(...args).stdout.trim();
  const commit = (subject, files) => {
    for (const [path, text] of Object.entries(files)) writeFileSync(join(repo, path), text);
    assert.equal(run("add", "--", ...Object.keys(files)).status, 0);
    assert.equal(run("commit", "-q", "-m", subject).status, 0);
    return git("rev-parse", "HEAD");
  };
  const mark = (input, ...args) =>
    spawnSync(process.execPath, [join(repo, "scripts", "zh-readme-mark.mjs"), ...args], {
      cwd: repo,
      encoding: "utf8",
      env,
      input,
    });
  const marker = () => readFileSync(join(repo, ZH_MARKER), "utf8");
  assert.equal(run("init", "-q", "-b", "master").status, 0);
  return { repo, run, git, commit, mark, marker, cleanup: () => rmSync(dir, { recursive: true, force: true }) };
}

test("zh-readme-mark refuses bad arguments before touching anything", () => {
  const { mark, cleanup } = makeRepo();
  try {
    assert.equal(mark("", "--help").status, 0);
    for (const args of [["--nope"], ["--english-only", "--zh-only"], ["--commit"], ["--commit", "--yes"]]) {
      const r = mark("", ...args);
      assert.equal(r.status, 1, args.join(" "));
      assert.match(r.stderr, /^error: .*\nusage:/s, args.join(" "));
    }
  } finally {
    cleanup();
  }
});

test("zh-readme-mark lists what it covers, asks, and writes only on yes", () => {
  const { repo, run, git, commit, mark, marker, cleanup } = makeRepo();
  try {
    const base = commit("feat: both readmes", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    assert.match(mark("", "--yes").stderr, /no origin\/master here: run git fetch origin first/);
    assert.equal(run("update-ref", "refs/remotes/origin/master", base).status, 0);
    assert.match(mark("", "--english-only").stderr, /doesn't exist yet/);
    commit("build: add the marker", { [ZH_MARKER]: formatMarker({ english: base, zhTw: base }) });
    commit("docs: English that needs translating", { [EN_README]: "en 2" });
    commit("fix: a zh-TW fix", { [ZH_README]: "zh 1b" });
    const master = git("rev-parse", "HEAD");
    assert.equal(run("update-ref", "refs/remotes/origin/master", master).status, 0);
    writeFileSync(join(repo, ZH_README), "zh 2");
    const before = marker();
    for (const input of ["n\n", ""]) {
      const r = mark(input);
      assert.equal(r.status, 1);
      assert.match(r.stdout, /English readme commits this marks as translated: 1\n {2}\w{7} docs: English that needs translating/);
      assert.match(r.stdout, /Traditional Chinese readme commits this marks as checked: 1\n {2}\w{7} fix: a zh-TW fix\n {2}plus the uncommitted Traditional Chinese readme edits/);
      assert.match(r.stdout, /Not written\./);
      assert.equal(marker(), before);
    }
    const yes = mark("y\n");
    assert.equal(yes.status, 0, yes.stderr);
    assert.deepEqual(parseMarker(marker()), { english: master, zhTw: master });
    assert.match(yes.stdout, /Write scripts\/zh-readme-translated-to\.txt\? \[y\/N\] Wrote scripts\/zh-readme-translated-to\.txt/);
    const again = mark("", "--yes");
    assert.equal(again.status, 0);
    assert.match(again.stdout, /already says this; nothing to write/);
    const back = mark("y\n", "--zh-only", "--commit", base);
    assert.equal(back.status, 1);
    assert.match(back.stderr, /a mark never moves back/);
    // A damaged marker says how to get it back.
    writeFileSync(join(repo, ZH_MARKER), "garbage\n");
    assert.match(mark("", "--yes").stderr, /unknown line "garbage": restore it with git checkout HEAD -- scripts\/zh-readme-translated-to\.txt/);
    assert.equal(run("checkout", "HEAD", "--", ZH_MARKER).status, 0);
    // On a catch-up branch, its own committed readme edits are shown too: the squash merge counts them.
    assert.equal(run("checkout", "-q", "-b", "catch-up").status, 0);
    commit("docs: a new English section", { [EN_README]: "en 3", [ZH_README]: "zh 2" });
    const own = mark("", "--yes");
    assert.equal(own.status, 0, own.stderr);
    assert.match(own.stdout, /plus this branch's own 1 commit\(s\), which its squash merge counts too:\n {4}\w{7} docs: a new English section/);
    // Another catch-up from the same base merged first with these marks: this branch's own edits stay listed.
    assert.equal(run("checkout", "-q", "--", ZH_MARKER).status, 0);
    assert.equal(run("checkout", "-q", "-b", "first", master).status, 0);
    const first = commit("docs: the other catch-up", { [ZH_MARKER]: formatMarker({ english: master, zhTw: master }) });
    assert.equal(run("update-ref", "refs/remotes/origin/master", first).status, 0);
    assert.equal(run("checkout", "-q", "catch-up").status, 0);
    const note = mark("", "--yes");
    assert.equal(note.status, 0, note.stderr);
    assert.match(note.stdout, /plus this branch's own 1 commit\(s\), which the check will still list/);
    assert.match(note.stdout, /Note: origin\/master already records this mark for the English readme and Traditional Chinese readme/);
  } finally {
    cleanup();
  }
});

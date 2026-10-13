// readme-lag-lib: what the zh-TW readme lags behind. The parsing cases run on canned
// `git` output; the real-repository cases build throwaway repositories, so the real git
// sees the non-ASCII path.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";

import {
  EN_README,
  formatMarker,
  isReleaseCommit,
  parseLog,
  parseMarker,
  planMark,
  ZH_MARKER,
  ZH_README,
  zhReadmeLag,
} from "./readme-lag-lib.mjs";
import { BANNER_FILES } from "./version-lib.mjs";

// A throwaway repository with the readme and marker folders, run with an empty git config.
function makeRepo() {
  const dir = mkdtempSync(join(tmpdir(), "plugy-readme-lag-"));
  const emptyConfig = join(dir, "gitconfig");
  writeFileSync(emptyConfig, "");
  const repo = join(dir, "repo");
  mkdirSync(join(repo, dirname(EN_README)), { recursive: true });
  mkdirSync(join(repo, dirname(ZH_MARKER)), { recursive: true });
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
  assert.equal(run("init", "-q", "-b", "master").status, 0);
  return { dir, repo, run, git, commit, cleanup: () => rmSync(dir, { recursive: true, force: true }) };
}

const mark = (english, zhTw = english) => ({ [ZH_MARKER]: formatMarker({ english, zhTw }) });
const shas = (list) => list.map((c) => c.sha);

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

test("parseMarker reads the english and zh-tw lines in any order, skipping blank and # lines", () => {
  assert.deepEqual(parseMarker("# about\n\nenglish abc1234\nzh-tw def5678\n"), { english: "abc1234", zhTw: "def5678" });
  assert.deepEqual(parseMarker("# about\r\n  zh-tw def5678 \r\nenglish\tabc1234\r\n"), { english: "abc1234", zhTw: "def5678" });
  assert.match(parseMarker("english abc1234\n").error, /has no zh-tw line/);
  assert.match(parseMarker("zh-tw abc1234\n").error, /has no english line/);
  assert.match(parseMarker("english abc1234\nenglish abc1234\nzh-tw abc1234\n").error, /names english twice/);
  assert.match(parseMarker("toString abc1234\nenglish abc1234\nzh-tw abc1234\n").error, /unknown line "toString abc1234"/);
  assert.match(parseMarker("english abc1234 # note\nzh-tw abc1234\n").error, /unknown line/);
  assert.match(parseMarker("abc1234\n").error, /unknown line "abc1234"/);
  for (const bad of ["ABC1234", "abc12", "e".repeat(41), "master", ""]) {
    assert.match(parseMarker(`english ${bad}\nzh-tw abc1234\n`).error, /not a commit hash/, bad);
  }
  // What `... > file` in Windows PowerShell 5.1 leaves, read back as UTF-8.
  const text = "english abc1234\r\nzh-tw abc1234\r\n";
  const utf16 = Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(text, "utf16le")]).toString("utf8");
  assert.match(parseMarker(utf16).error, /not plain ASCII text/);
});

test("formatMarker writes what parseMarker reads, and the committed marker is in that format", () => {
  const marks = { english: "a".repeat(40), zhTw: "b".repeat(40) };
  assert.deepEqual(parseMarker(formatMarker(marks)), marks);
  assert.ok(formatMarker(marks).endsWith("\n") && !formatMarker(marks).includes("\r"));
  const committed = readFileSync(new URL(`../${ZH_MARKER}`, import.meta.url), "utf8").split("\r\n").join("\n");
  const parsed = parseMarker(committed);
  assert.equal(parsed.error, undefined);
  assert.equal(committed, formatMarker(parsed));
});

test("zhReadmeLag measures each list from its own mark, without its setter and release commits", () => {
  const calls = [];
  const markerText = formatMarker({ english: "e1abcde", zhTw: "f1abcde" });
  const git = (...args) => {
    calls.push(args);
    if (args[0] === "show") return markerText;
    if (args[0] === "rev-parse") return args[3].startsWith("e1") ? "e1full" : "f1full";
    if (args[0] === "merge-base") return "";
    if (args.includes(ZH_MARKER)) return "c3\tdocs(readme): any title at all";
    if (args.includes(ZH_README)) return "r9\tchore(release): v2.0.0\nc3\tthe marker commit\nz4\tfix: a zh-TW typo";
    return "f8\tfeat: both readmes\nr9\tchore(release): v2.0.0\nc3\tthe marker commit\ne2\tdocs: English only";
  };
  assert.deepEqual(zhReadmeLag(git), {
    english: "e1full",
    zhTw: "f1full",
    englishSetBy: "c3",
    zhSetBy: "c3",
    behind: [
      { sha: "f8", subject: "feat: both readmes" },
      { sha: "e2", subject: "docs: English only" },
    ],
    editedSince: [{ sha: "z4", subject: "fix: a zh-TW typo" }],
  });
  assert.deepEqual(calls, [
    ["show", `HEAD:${ZH_MARKER}`],
    ["rev-parse", "--verify", "--quiet", "e1abcde^{commit}"],
    ["merge-base", "--is-ancestor", "e1full", "HEAD"],
    ["rev-parse", "--verify", "--quiet", "f1abcde^{commit}"],
    ["merge-base", "--is-ancestor", "f1full", "HEAD"],
    ["log", "--format=%H%x09%s", "HEAD", "--", ZH_MARKER],
    ["show", `c3:${ZH_MARKER}`],
    ["log", "--format=%H%x09%s", "e1full..HEAD", "--", EN_README],
    ["log", "--format=%H%x09%s", "f1full..HEAD", "--", ZH_README],
  ]);
});

test("zhReadmeLag reports each way it can't measure", () => {
  const ok = formatMarker({ english: "e1abcde", zhTw: "f1abcde" });
  // `fails(args)` picks the one call that fails; every other call answers as in a healthy repository.
  const failing = (fails, markerText = ok) => (...args) => {
    if (fails(args)) return null;
    if (args[0] === "show") return markerText;
    if (args[0] === "log" && args.includes(ZH_MARKER)) return "c3\tset the marker";
    return args[0] === "rev-parse" ? `${args[3].slice(0, 2)}full` : "";
  };
  const isLog = (path) => (args) => args[0] === "log" && args.includes(path);
  assert.match(zhReadmeLag(() => null).error, /no scripts\/zh-readme-translated-to\.txt at HEAD/);
  assert.match(zhReadmeLag(failing(() => false, "english main\nzh-tw f1abcde")).error, /holds "english main", not a commit hash/);
  assert.match(zhReadmeLag(failing((a) => a[3] === "e1abcde^{commit}")).error, /names e1abcde \(english\), which is not a commit here/);
  assert.match(zhReadmeLag(failing((a) => a[3] === "f1abcde^{commit}")).error, /names f1abcde \(zh-tw\), which is not a commit here/);
  assert.match(zhReadmeLag(failing((a) => a[2] === "f1full")).error, /\(zh-tw\), which is not an ancestor of HEAD/);
  assert.match(zhReadmeLag(failing(isLog(ZH_MARKER))).error, /git log failed for scripts\/zh-readme-translated-to\.txt/);
  assert.match(zhReadmeLag(failing(isLog(EN_README))).error, /git log failed for .*Readme/);
  assert.match(zhReadmeLag(failing(isLog(ZH_README))).error, /git log failed for .*讀我檔案/);
});

test("zhReadmeLag on a real repository: merge races, reverts and English-only marks follow the marker file", () => {
  const { run, git, commit, cleanup } = makeRepo();
  try {
    const lag = () => zhReadmeLag(git);
    commit("feat: both readmes", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    const e1 = commit("docs: English only", { [EN_README]: "en 2" });
    assert.match(lag().error, /no scripts\/zh-readme-translated-to\.txt at HEAD/);
    // The catch-up starts from e1, but e2 merges before it does. Its title says nothing about a catch-up.
    const e2 = commit("docs: more English", { [EN_README]: "en 3" });
    const c1 = commit("docs: update the zh-TW readme (#5)", { [ZH_README]: "zh 2", ...mark(e1) });
    assert.deepEqual(lag(), {
      english: e1,
      zhTw: e1,
      englishSetBy: c1,
      zhSetBy: c1,
      behind: [{ sha: e2, subject: "docs: more English" }],
      editedSince: [],
    });
    // A later zh-TW edit is listed for the releaser to check.
    const typo = commit("docs(readme): fix a typo in the Traditional Chinese readme", { [ZH_README]: "zh 2b" });
    assert.deepEqual(shas(lag().behind), [e2]);
    assert.deepEqual(shas(lag().editedSince), [typo]);
    // A catch-up that also edits the English readme translates its own edit too.
    const c2 = commit("docs: catch up", { [EN_README]: "en 3b", [ZH_README]: "zh 3", ...mark(typo) });
    assert.deepEqual(lag().behind, []);
    assert.deepEqual(lag().editedSince, []);
    // Reverting it restores c1's marks, so everything after e1 is listed again.
    assert.equal(run("revert", "--no-edit", "HEAD").status, 0);
    const revert = git("rev-parse", "HEAD");
    assert.equal(lag().englishSetBy, c1);
    assert.equal(lag().zhSetBy, c1);
    assert.deepEqual(shas(lag().behind), [revert, c2, e2]);
    assert.deepEqual(shas(lag().editedSince), [revert, c2, typo]);
    const c3 = commit("docs: catch up again", { [ZH_README]: "zh 3c", ...mark(revert) });
    const release = commit("chore(release): v1.0.0", { [EN_README]: "en 3 v1", [ZH_README]: "zh 3c v1" });
    assert.deepEqual(lag().behind, []);
    assert.deepEqual(lag().editedSince, []);
    // An English fix the translation already has moves only the english mark.
    const fix = commit("docs: fix an English key name", { [EN_README]: "en 3 v1 fixed", ...mark(release, revert) });
    assert.deepEqual(lag(), { english: release, zhTw: revert, englishSetBy: fix, zhSetBy: c3, behind: [], editedSince: [] });
    // A binary merge keeps an older zh-TW copy: listed, and an English-only mark does not clear it.
    const stale = commit("feat: merge that kept an old zh-TW copy", { [ZH_README]: "zh 2" });
    assert.deepEqual(shas(lag().editedSince), [stale]);
    const fix2 = commit("docs: fix another English key name", { [EN_README]: "en 3 v1 fixed 2", ...mark(stale, revert) });
    assert.deepEqual(shas(lag().editedSince), [stale]);
    assert.deepEqual(lag().behind, []);
    // A repair checks it; reverting the repair lists it again, measured from the catch-up that set the zh-tw mark.
    const repair = commit("docs: repair the zh-TW readme", { [ZH_README]: "zh 3c v1", ...mark(fix2) });
    assert.deepEqual(lag().editedSince, []);
    assert.equal(run("revert", "--no-edit", "HEAD").status, 0);
    const revert2 = git("rev-parse", "HEAD");
    assert.equal(lag().englishSetBy, fix2);
    assert.equal(lag().zhSetBy, c3);
    assert.deepEqual(shas(lag().editedSince), [revert2, repair, stale]);
    assert.deepEqual(lag().behind, []);
    // Marks naming a commit outside this history, or no commit at all, can't be measured from.
    assert.equal(run("checkout", "-q", "-b", "side").status, 0);
    const side = commit("docs: English on a side branch", { [EN_README]: "en side" });
    assert.equal(run("checkout", "-q", "-").status, 0);
    commit("docs: point at the side branch", mark(side));
    assert.match(lag().error, /\(english\), which is not an ancestor of HEAD/);
    commit("docs: point at nothing", mark(revert, "0123456"));
    assert.match(lag().error, /names 0123456 \(zh-tw\), which is not a commit here/);
    commit("docs: garble the marker", { [ZH_MARKER]: "see the readme\n" });
    assert.match(lag().error, /unknown line "see the readme"/);
  } finally {
    cleanup();
  }
});

test("zhReadmeLag: zh-TW edits merged before the first marker or while a catch-up was open are listed", () => {
  const { git, commit, cleanup } = makeRepo();
  try {
    const t = commit("docs: a catch-up made before the marker file existed", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    // A zh-TW fix merges before the pull request that adds the marker, which edits no zh-TW readme.
    const early = commit("fix: a zh-TW fix", { [ZH_README]: "zh 1b" });
    const seed = commit("build: add the marker file", mark(t));
    assert.deepEqual(zhReadmeLag(git), {
      english: t,
      zhTw: t,
      englishSetBy: seed,
      zhSetBy: seed,
      behind: [],
      editedSince: [{ sha: early, subject: "fix: a zh-TW fix" }],
    });
    // A catch-up starts from the seed; another zh-TW edit merges while it is open, and its binary copy may drop it.
    const during = commit("fix: another zh-TW fix", { [ZH_README]: "zh 1c" });
    const c1 = commit("docs: catch up", { [ZH_README]: "zh 2", ...mark(seed) });
    assert.deepEqual(shas(zhReadmeLag(git).editedSince), [during]);
    // A catch-up that checks it and has nothing to change in the readme still clears the list.
    commit("docs: catch up, nothing to change", mark(c1));
    assert.deepEqual(zhReadmeLag(git).editedSince, []);
    assert.deepEqual(zhReadmeLag(git).behind, []);
  } finally {
    cleanup();
  }
});

test("planMark moves the chosen marks to the branch's master base and lists what they newly cover", () => {
  const { run, git, commit, cleanup } = makeRepo();
  try {
    const base = commit("feat: both readmes", { [EN_README]: "en 1", [ZH_README]: "zh 1" });
    assert.deepEqual(planMark(git, { current: null, mode: "both", commit: null }), {
      error: "no origin/master here: run git fetch origin first",
    });
    // The previous catch-up edited both readmes and recorded base: the check already counts it, so no list shows it.
    commit("docs: the previous catch-up", { [EN_README]: "en 1b", [ZH_README]: "zh 1c", ...mark(base) });
    const current = formatMarker({ english: base, zhTw: base });
    const en = commit("docs: English", { [EN_README]: "en 2" });
    const zh = commit("fix: a zh-TW fix", { [ZH_README]: "zh 1b" });
    commit("chore(release): v1.0.0", { [EN_README]: "en 2 v1", [ZH_README]: "zh 1b v1" });
    const master = git("rev-parse", "HEAD");
    assert.equal(run("update-ref", "refs/remotes/origin/master", master).status, 0);
    const onMaster = planMark(git, { current, mode: "both", commit: null });
    assert.deepEqual([onMaster.target, shas(onMaster.english), shas(onMaster.zh)], [master, [en], [zh]]);
    assert.deepEqual([onMaster.ownEnglish, onMaster.ownZh, onMaster.onMaster], [[], [], { english: false, zhTw: false }]);
    // The catch-up branch: its own commits are listed apart, and can't be a mark.
    assert.equal(run("checkout", "-q", "-b", "catch-up").status, 0);
    const wip = commit("docs: work in progress", { [ZH_README]: "zh 2" });
    const wipEn = commit("docs: a new English section", { [EN_README]: "en 3" });
    assert.match(planMark(git, { current, mode: "both", commit: wip }).error, /is not on origin\/master/);
    const both = planMark(git, { current, mode: "both", commit: null });
    assert.equal(both.target, master);
    assert.deepEqual([shas(both.english), shas(both.ownEnglish)], [[en], [wipEn]]);
    assert.deepEqual([shas(both.zh), shas(both.ownZh)], [[zh], [wip]]);
    assert.deepEqual(parseMarker(both.text), { english: master, zhTw: master });
    const english = planMark(git, { current, mode: "english", commit: null });
    assert.deepEqual([shas(english.english), english.zh, english.ownZh], [[en], null, null]);
    assert.deepEqual(english.onMaster, { english: false, zhTw: null });
    assert.deepEqual(parseMarker(english.text), { english: master, zhTw: base });
    // An older target leaves master's later commits to the check; own still means this branch's commits only.
    const zhOnly = planMark(git, { current, mode: "zh", commit: en });
    assert.deepEqual([zhOnly.english, zhOnly.zh, shas(zhOnly.ownZh)], [null, [], [wip]]);
    assert.deepEqual(parseMarker(zhOnly.text), { english: base, zhTw: en });
    // Another catch-up from the same base merged first: a squash of this branch would record neither mark.
    assert.equal(run("checkout", "-q", "-b", "first", master).status, 0);
    const first = commit("docs: the other catch-up", { [ZH_README]: "zh 2", ...mark(master) });
    assert.equal(run("update-ref", "refs/remotes/origin/master", first).status, 0);
    assert.equal(run("checkout", "-q", "catch-up").status, 0);
    assert.deepEqual(planMark(git, { current, mode: "both", commit: null }).onMaster, { english: true, zhTw: true });
    // Only the zh-tw mark already recorded on master: the squash records the english mark alone.
    assert.equal(run("checkout", "-q", "-b", "zh-first", master).status, 0);
    const zhFirst = commit("docs: a zh-TW check", { [ZH_README]: "zh 2b", ...mark(base, master) });
    assert.equal(run("update-ref", "refs/remotes/origin/master", zhFirst).status, 0);
    assert.equal(run("checkout", "-q", "catch-up").status, 0);
    assert.deepEqual(planMark(git, { current, mode: "both", commit: master }).onMaster, { english: false, zhTw: true });
    assert.equal(run("update-ref", "refs/remotes/origin/master", master).status, 0);
    // Mid-merge, the marker in the working tree may already be master's: finish the merge first.
    assert.equal(run("merge", "-q", "--no-commit", "--no-ff", "first").status, 0);
    assert.match(planMark(git, { current, mode: "both", commit: null }).error, /a merge is in progress/);
    assert.equal(run("merge", "--abort").status, 0);
    // Refusals.
    const moved = formatMarker({ english: master, zhTw: master });
    assert.match(planMark(git, { current: moved, mode: "both", commit: base }).error, /a mark never moves back/);
    assert.match(planMark(git, { current: null, mode: "english", commit: null }).error, /doesn't exist yet/);
    assert.match(planMark(git, { current: "nonsense\n", mode: "both", commit: null }).error, /unknown line.*restore it with git checkout HEAD --/);
    assert.match(planMark(git, { current: mark("0123456")[ZH_MARKER], mode: "both", commit: null }).error, /mark 0123456 is not a commit here: fix/);
    assert.match(planMark(git, { current, mode: "both", commit: "0123456" }).error, /0123456 is not a commit here/);
    assert.equal(run("checkout", "-q", "-b", "elsewhere", base).status, 0);
    const away = commit("docs: elsewhere", { [EN_README]: "en x" });
    assert.equal(run("checkout", "-q", "catch-up").status, 0);
    assert.match(planMark(git, { current, mode: "both", commit: away }).error, /is not an ancestor of HEAD/);
    // On master itself, unpushed commits can't be folded into a catch-up: push first.
    assert.equal(run("checkout", "-q", "master").status, 0);
    commit("docs: not pushed yet", { [EN_README]: "en 4" });
    assert.match(planMark(git, { current, mode: "both", commit: null }).error, /master is ahead of origin\/master/);
    assert.equal(run("checkout", "-q", "--detach").status, 0);
    assert.match(planMark(git, { current, mode: "both", commit: null }).error, /HEAD is detached and ahead of origin\/master/);
    assert.equal(run("checkout", "-q", "master").status, 0);
    // With no marker yet, both marks start at the target and cover nothing listed.
    assert.equal(run("update-ref", "refs/remotes/origin/master", git("rev-parse", "HEAD")).status, 0);
    assert.deepEqual(planMark(git, { current: null, mode: "both", commit: null }).english, []);
  } finally {
    cleanup();
  }
});

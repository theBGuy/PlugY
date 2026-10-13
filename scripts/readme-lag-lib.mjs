// What the Traditional Chinese readme lags behind. Git merges that UTF-16 file as binary, so pull requests
// leave it behind the English readme and release prep catches it up. A tracked marker file holds two master
// commits: the one whose English readme the translation covers, and the one the zh-TW readme was last checked
// against. Each moves only when someone records it (scripts/zh-readme-mark.mjs), so commit subjects and pull
// request titles never matter, and a revert of a catch-up restores the older marks with it.

import { EN_README, ZH_README } from "./version-lib.mjs";

export { EN_README, ZH_README };

export const ZH_MARKER = "scripts/zh-readme-translated-to.txt";

// Release commits stamp every readme's version banner, which translates nothing.
const RELEASE_SUBJECT_RE = /^chore\(release\): v\d/;

const HASH_RE = /^[0-9a-f]{7,40}$/;
const KEYS = { english: "english", "zh-tw": "zhTw" };

// Windows PowerShell 5.1's `>` writes UTF-16, which reaches here full of NULs or replacement characters.
const NOT_TEXT = [String.fromCharCode(0), String.fromCharCode(0xfffd)];

/** `git log --format=%H%x09%s` output to [{ sha, subject }], newest first; null stays null. */
export function parseLog(out) {
  if (out === null) return null;
  return out
    .split("\n")
    .filter((line) => line !== "")
    .map((line) => {
      const tab = line.indexOf("\t");
      return tab < 0 ? { sha: line, subject: "" } : { sha: line.slice(0, tab), subject: line.slice(tab + 1) };
    });
}

export const isReleaseCommit = (entry) => RELEASE_SUBJECT_RE.test(entry.subject);

/** The marker file's text to { english, zhTw }, or { error }; blank and # lines skipped. */
export function parseMarker(text) {
  if (NOT_TEXT.some((c) => text.includes(c))) return { error: `${ZH_MARKER} is not plain ASCII text (UTF-16?)` };
  const marks = {};
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    const [key, hash, ...rest] = line.split(/\s+/);
    if (!Object.hasOwn(KEYS, key) || rest.length > 0) return { error: `${ZH_MARKER} has an unknown line "${line}"` };
    if (KEYS[key] in marks) return { error: `${ZH_MARKER} names ${key} twice` };
    if (!HASH_RE.test(hash ?? "")) return { error: `${ZH_MARKER} holds "${line}", not a commit hash` };
    marks[KEYS[key]] = hash;
  }
  for (const [key, field] of Object.entries(KEYS)) {
    if (!(field in marks)) return { error: `${ZH_MARKER} has no ${key} line` };
  }
  return marks;
}

/** The marker file's text for { english, zhTw } (full hashes), as scripts/zh-readme-mark.mjs writes it. */
export function formatMarker({ english, zhTw }) {
  return [
    "# What the Traditional Chinese readme covers; release.mjs checks it before a release.",
    "# english: the master commit whose English readme it translates.",
    "# zh-tw: the master commit it was last checked against; later edits of it are listed until the next check.",
    "# Write it with node scripts/zh-readme-mark.mjs. Lines starting with # are ignored.",
    `english ${english}`,
    `zh-tw ${zhTw}`,
    "",
  ].join("\n");
}

/** Non-release commits that changed `path` in from..to, newest first, or null when git fails. */
export function commitsSince(git, from, to, path) {
  const log = parseLog(git("log", "--format=%H%x09%s", `${from}..${to}`, "--", path));
  return log === null ? null : log.filter((entry) => !isReleaseCommit(entry));
}

// A revision to its full commit hash, or null; cached, since the marker history repeats the same few hashes.
function commitResolver(git) {
  const resolved = new Map();
  return (rev) => {
    if (!resolved.has(rev)) resolved.set(rev, git("rev-parse", "--verify", "--quiet", `${rev}^{commit}`));
    return resolved.get(rev);
  };
}

/**
 * What the zh-TW readme lags behind at HEAD, as { english, zhTw, englishSetBy, zhSetBy, behind, editedSince }:
 * the two marks as full hashes, the oldest commit that set each, the English readme commits after the english mark
 * and the zh-TW readme commits after the zh-tw mark (each without its setter and release commits), newest first.
 * { error } when git fails, the marker is missing or malformed, or a mark names no commit here or one that is not an
 * ancestor of HEAD.
 * `git(...args)` returns trimmed stdout, or null when the command fails.
 */
export function zhReadmeLag(git) {
  const text = git("show", `HEAD:${ZH_MARKER}`);
  if (text === null) return { error: `no ${ZH_MARKER} at HEAD` };
  const marks = parseMarker(text);
  if (marks.error) return marks;
  const resolve = commitResolver(git);
  const full = {};
  for (const [key, field] of Object.entries(KEYS)) {
    const named = `${ZH_MARKER} names ${marks[field]} (${key})`;
    full[field] = resolve(marks[field]);
    if (full[field] === null) return { error: `${named}, which is not a commit here` };
    if (git("merge-base", "--is-ancestor", full[field], "HEAD") === null) {
      return { error: `${named}, which is not an ancestor of HEAD` };
    }
  }
  const setters = markSetters(git, resolve, full);
  if (setters === null) return { error: `git log failed for ${ZH_MARKER}` };
  const en = commitsSince(git, full.english, "HEAD", EN_README);
  if (en === null) return { error: `git log failed for ${EN_README}` };
  const zh = commitsSince(git, full.zhTw, "HEAD", ZH_README);
  if (zh === null) return { error: `git log failed for ${ZH_README}` };
  return {
    english: full.english,
    zhTw: full.zhTw,
    englishSetBy: setters.english,
    zhSetBy: setters.zhTw,
    behind: en.filter((entry) => entry.sha !== setters.english),
    editedSince: zh.filter((entry) => entry.sha !== setters.zhTw),
  };
}

// The oldest commit in rev's history whose marker records each wanted mark (full hashes), or null when none does: a
// revert brings back an older mark, and with it the catch-up that first recorded it, whose own edits count as
// translated or checked. Returns null when git log fails.
function markSetters(git, resolve, wanted, rev = "HEAD") {
  const log = parseLog(git("log", "--format=%H%x09%s", rev, "--", ZH_MARKER));
  if (log === null) return null;
  const setters = { english: null, zhTw: null };
  for (const entry of log) {
    const own = git("show", `${entry.sha}:${ZH_MARKER}`);
    const marks = own === null ? {} : parseMarker(own);
    for (const field of Object.values(KEYS)) {
      if (wanted[field] && marks[field] && resolve(marks[field]) === wanted[field]) setters[field] = entry.sha;
    }
  }
  return setters;
}

/**
 * What scripts/zh-readme-mark.mjs would record, as { text, target, english, zh, ownEnglish, ownZh, onMaster }: the
 * new file text; the target commit; for each moved mark, the non-release readme commits it newly covers up to the
 * target (english, zh), the branch's own ones after its base on origin/master (ownEnglish, ownZh), and whether
 * origin/master's history already records the new mark (onMaster.english, onMaster.zhTw), in which case the squash
 * merge doesn't record it and the branch's own edits stay listed. A mark kept as it is gets null throughout.
 * { error } when it can't. `current` is the marker file's text in the working tree, or null; `mode` is "both",
 * "english" or "zh"; `commit` names the target, by default the branch's base on origin/master.
 */
export function planMark(git, { current, mode, commit }) {
  const resolve = commitResolver(git);
  const master = resolve("origin/master");
  if (master === null) return { error: "no origin/master here: run git fetch origin first" };
  const branchBase = git("merge-base", "HEAD", master);
  if (branchBase === null) return { error: "no merge base with origin/master" };
  const target = commit ? resolve(commit) : branchBase;
  if (target === null) return { error: `${commit} is not a commit here` };
  if (git("merge-base", "--is-ancestor", target, "HEAD") === null) return { error: `${target} is not an ancestor of HEAD` };
  // A branch's own commit vanishes in the squash merge, and the check could then measure from neither mark.
  if (git("merge-base", "--is-ancestor", target, master) === null) {
    return { error: `${target} is not on origin/master: name a master commit` };
  }
  // On master, or on a detached HEAD, no squash merge folds the commits after origin/master into the catch-up.
  if (git("rev-list", "--count", `${branchBase}..HEAD`) !== "0") {
    const branch = git("symbolic-ref", "-q", "--short", "HEAD");
    if (branch === "master") return { error: "master is ahead of origin/master: push it first, so the marks can name its commits" };
    if (branch === null) return { error: "HEAD is detached and ahead of origin/master: check out the catch-up branch first" };
  }
  // Mid-merge, HEAD is still the branch before the merge while the marker may already be master's.
  if (git("rev-parse", "-q", "--verify", "MERGE_HEAD") !== null) {
    return { error: "a merge is in progress: commit it first (on a conflict, take origin/master's marker file)" };
  }
  const old = current === null ? null : parseMarker(current);
  if (old?.error) return { error: `${old.error}: restore it with git checkout HEAD -- ${ZH_MARKER}` };
  if (old === null && mode !== "both") return { error: `${ZH_MARKER} doesn't exist yet: record both marks first` };
  const marks = { english: old?.english ?? null, zhTw: old?.zhTw ?? null };
  const from = {};
  for (const [key, field] of Object.entries(KEYS)) {
    from[field] = marks[field] && resolve(marks[field]);
    if (marks[field] && from[field] === null) {
      return { error: `the current ${key} mark ${marks[field]} is not a commit here: fix ${ZH_MARKER} by hand` };
    }
  }
  // The previous catch-up's own commit sits after its marks; the check already counts it as covered.
  const setters = markSetters(git, resolve, from);
  if (setters === null) return { error: `git log failed for ${ZH_MARKER}` };
  const plan = { english: null, zh: null, ownEnglish: null, ownZh: null, onMaster: { english: null, zhTw: null } };
  const move = (field, key, path, list, own) => {
    if (from[field] && git("merge-base", "--is-ancestor", from[field], target) === null) {
      return `${target} is not a descendant of the current ${key} mark ${from[field]}: a mark never moves back (fix ${ZH_MARKER} by hand if the mark is wrong)`;
    }
    const newly = from[field] ? commitsSince(git, from[field], target, path) : [];
    const mine = commitsSince(git, branchBase, "HEAD", path);
    if (newly === null || mine === null) return `git log failed for ${path}`;
    plan[list] = newly.filter((entry) => entry.sha !== setters[field]);
    plan[own] = mine;
    marks[field] = target;
    return null;
  };
  const failed =
    (mode !== "zh" && move("english", "english", EN_README, "english", "ownEnglish")) ||
    (mode !== "english" && move("zhTw", "zh-tw", ZH_README, "zh", "ownZh"));
  if (failed) return { error: failed };
  // The check takes a mark's oldest recorder as its setter: if master already has one, the squash won't be it.
  const recorded = markSetters(git, resolve, { english: resolve(marks.english), zhTw: resolve(marks.zhTw) }, master);
  if (recorded === null) return { error: `git log failed for ${ZH_MARKER}` };
  for (const [field, own] of [["english", "ownEnglish"], ["zhTw", "ownZh"]]) {
    if (plan[own] !== null) plan.onMaster[field] = recorded[field] !== null;
  }
  return { text: formatMarker(marks), target, ...plan };
}

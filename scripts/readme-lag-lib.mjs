// What the Traditional Chinese readme lags behind. Git merges that UTF-16 file as binary, so pull requests
// leave it behind the English readme and release prep catches it up. A catch-up commit names the commit whose
// English readme it translates: a partial edit can't pass for one, and English changes merged meanwhile stay listed.
// Any later zh-TW edit is listed too: a binary merge that kept an older copy undoes a catch-up silently.

import { EN_README, ZH_README } from "./version-lib.mjs";

export { EN_README, ZH_README };

/** The catch-up commit's subject; a squash merge's " (#N)" may follow. Capture 1 is the translated-to commit. */
export const CATCH_UP_RE = /^docs\(readme\): catch up the Traditional Chinese readme to ([0-9a-f]{7,40})\b/;

// Release commits stamp every readme's version banner, which translates nothing.
const RELEASE_SUBJECT_RE = /^chore\(release\): v\d/;

// `git revert` and GitHub's Revert button quote the reverted subject; a squash merge's " (#N)" may follow.
const REVERT_RE = /^Revert "(.*)"(?: \(#\d+\))?$/;

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

/**
 * The newest zh-TW readme commit whose subject is a catch-up that no newer commit reverts, as
 * { sha, translatedTo, editedSince } with the non-release zh-TW commits after it; or null.
 */
export function lastCatchUp(zhLog) {
  const reverted = new Set();
  for (const [i, entry] of zhLog.entries()) {
    const r = REVERT_RE.exec(entry.subject);
    if (r) reverted.add(r[1]);
    const m = CATCH_UP_RE.exec(entry.subject);
    if (m && !reverted.has(entry.subject)) {
      return { sha: entry.sha, translatedTo: m[1], editedSince: zhLog.slice(0, i).filter((e) => !isReleaseCommit(e)) };
    }
  }
  return null;
}

/**
 * The English readme commits after the last catch-up's translated-to commit, newest first, as
 * { since, translatedTo, behind, editedSince }; { error } when git fails, no catch-up exists yet, or the
 * commit it names is not an ancestor of HEAD. The catch-up's own English edits count as translated.
 * `git(...args)` returns trimmed stdout, or null when the command fails.
 */
export function zhReadmeLag(git) {
  const zhLog = parseLog(git("log", "--format=%H%x09%s", "--", ZH_README));
  if (zhLog === null) return { error: `git log failed for ${ZH_README}` };
  const catchUp = lastCatchUp(zhLog);
  if (catchUp === null) {
    return { error: "no catch-up commit yet (subject: docs(readme): catch up the Traditional Chinese readme to <commit>)" };
  }
  const named = `${catchUp.sha.slice(0, 7)} names ${catchUp.translatedTo}`;
  const translatedTo = git("rev-parse", "--verify", "--quiet", `${catchUp.translatedTo}^{commit}`);
  if (translatedTo === null) return { error: `${named}, which is not a commit here` };
  if (git("merge-base", "--is-ancestor", translatedTo, "HEAD") === null) {
    return { error: `${named}, which is not an ancestor of HEAD` };
  }
  const enLog = parseLog(git("log", "--format=%H%x09%s", `${translatedTo}..HEAD`, "--", EN_README));
  if (enLog === null) return { error: `git log failed for ${EN_README}` };
  const behind = enLog.filter((entry) => !isReleaseCommit(entry) && entry.sha !== catchUp.sha);
  return { since: catchUp.sha, translatedTo, behind, editedSince: catchUp.editedSince };
}

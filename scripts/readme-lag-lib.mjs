// What the Traditional Chinese readme lags behind. Git merges that UTF-16 file as binary, so pull requests
// leave it behind the English readme and release prep catches it up. Only a catch-up commit, one that changes
// it and not the English readme, counts as translated: a feature that edits both translates its own section only.

export const EN_README = "game-files/PlugY_The_Survival_Kit_-_Readme.txt";
export const ZH_README = "game-files/PlugY_The_Survival_Kit_-_讀我檔案.txt";

// Release commits stamp every readme's version banner, which translates nothing.
const RELEASE_SUBJECT_RE = /^chore\(release\): v\d/;

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

/** The newest commit in the zh-TW log that is not a release commit and not in `englishShas`, or null. */
export const lastCatchUp = (zhLog, englishShas) =>
  zhLog.find((entry) => !isReleaseCommit(entry) && !englishShas.has(entry.sha))?.sha ?? null;

/**
 * The English readme commits since the zh-TW readme's last catch-up, newest first, as
 * { since, behind }; { error } when git fails or no catch-up commit exists yet.
 * `git(...args)` returns trimmed stdout, or null when the command fails.
 */
export function zhReadmeLag(git) {
  const zhLog = parseLog(git("log", "--format=%H%x09%s", "--", ZH_README));
  if (zhLog === null) return { error: `git log failed for ${ZH_README}` };
  const enLog = parseLog(git("log", "--format=%H%x09%s", "--", EN_README));
  if (enLog === null) return { error: `git log failed for ${EN_README}` };
  const since = lastCatchUp(zhLog, new Set(enLog.map((entry) => entry.sha)));
  if (since === null) return { error: "no commit has changed it without the English readme yet" };
  const behind = parseLog(git("log", "--format=%H%x09%s", `${since}..HEAD`, "--", EN_README));
  if (behind === null) return { error: `git log failed for ${EN_README}` };
  return { since, behind: behind.filter((entry) => !isReleaseCommit(entry)) };
}

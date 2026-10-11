#!/usr/bin/env node
// Interactive release driver for PlugY4Kolbot. One command drives the whole release
// runbook: checks preconditions, previews what's shipping, proposes the next
// version (patch/minor/major or a pre-release), then (on confirmation) runs
// changelog-release.mjs and commits + tags + pushes. Usage:
//   node scripts/release.mjs              (interactive)
//   node scripts/release.mjs --dry-run    (rehearse: prints every mutating step, writes nothing)
//   node scripts/release.mjs --no-merge   (passed through to changelog-release.mjs)
//   node scripts/release.mjs --no-fetch   (skip `git fetch` and the remote-tag lookup)
//
// Release tags are ANNOTATED (`git tag -a`) and pushed explicitly with master in
// one atomic push; the pushed tag triggers the release workflow, which builds a
// DRAFT GitHub release for review.

import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { createInterface } from "node:readline";
import { setTimeout as sleep } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import {
  findUnreleased,
  FRAGMENT_RE,
  readFragments,
  readLF,
  renderGroupBlocks,
  validateFragments,
} from "./changelog-lib.mjs";
import {
  assessCandidate,
  BANNER_FILES,
  commitBase,
  defaultStableKey,
  firstPrerelease,
  HEADER_PATH,
  headerProblem,
  isPrerelease,
  latestSemverTag,
  normalizeCandidate,
  parseHeader,
  prereleaseBaseMenu,
  prereleaseChannels,
  prereleaseMenu,
  readBannerLines,
  readHeaderFile,
  releasePaths,
  stableMenu,
  switchChannel,
  switchTargets,
  versionProblem,
} from "./version-lib.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

// The canonical repo; the release workflow names assets PlugY4Kolbot_v<V>*.
const REPO_SLUG = "theBGuy/PlugY4Kolbot";
const REPO_URL = `https://github.com/${REPO_SLUG}`;

// The review gate's text diff: everything a release rewrites that git can show
// as text (the UTF-16 banners are printed decoded instead). A stable release
// adds changelog.d, so the consumed fragments show as deletions.
const TEXT_DIFF_PATHS = [
  "CHANGELOG.md",
  HEADER_PATH,
  ...BANNER_FILES.filter((f) => f.encoding === "latin1").map((f) => f.path),
];

const USAGE = `usage: node scripts/release.mjs [--dry-run] [--no-merge] [--no-fetch]
  --dry-run   rehearse: print every mutating step, write nothing
  --no-merge  passed through to changelog-release.mjs
  --no-fetch  skip git fetch and the remote-tag lookup
  --help      show this message
`;

// Unknown arguments are refused before anything runs: a typo like --dryrun
// would otherwise run a REAL release.
const KNOWN_FLAGS = new Set(["--dry-run", "--no-merge", "--no-fetch"]);
const cliArgs = process.argv.slice(2);
if (cliArgs.includes("--help") || cliArgs.includes("-h")) {
  process.stdout.write(USAGE);
  process.exit(0);
}
const unknownArgs = cliArgs.filter((a) => !KNOWN_FLAGS.has(a));
if (unknownArgs.length > 0) {
  process.stderr.write(`error: unknown argument ${unknownArgs.join(", ")}\n${USAGE}`);
  process.exit(1);
}
const dryRun = cliArgs.includes("--dry-run");
const noMerge = cliArgs.includes("--no-merge");
const noFetch = cliArgs.includes("--no-fetch");

// --- tiny ANSI + emoji helpers (colors only when attached to a TTY) ---------
const tty = process.stdout.isTTY;
const paint = (code, s) => (tty ? `\x1b[${code}m${s}\x1b[0m` : s);
const bold = (s) => paint("1", s);
const dim = (s) => paint("2", s);
const green = (s) => paint("32", s);
const yellow = (s) => paint("33", s);
const red = (s) => paint("31", s);
const cyan = (s) => paint("36", s);
const rule = () => process.stdout.write(dim("─".repeat(60)) + "\n");
const say = (s) => process.stdout.write(`${s}\n`);
const ok = (s) => say(`${green("✓")} ${s}`);
const warn = (s) => say(`${yellow("⚠")}  ${s}`);
const bad = (s) => say(`${red("✗")} ${s}`);

const abort = (msg = "aborted") => {
  bad(msg);
  rl.close();
  process.exit(1);
};

// A precondition failure. In real mode ✗ aborts; in dry-run it downgrades to a
// warning so the runbook can be rehearsed against any working-tree state.
const fatal = (msg) => {
  if (dryRun) {
    warn(msg);
    return;
  }
  abort(msg);
};

// Read stdin line-by-line into a queue. `readline/promises`' `question()` hangs
// on EOF and (with piped input) delivers only the first buffered line before
// stalling, so we drive plain readline ourselves: buffer `line` events, hand
// them to waiting prompts, and resolve to null on `close` (EOF) so a prompt
// past the supplied input aborts cleanly instead of hanging.
const rl = createInterface({ input: process.stdin });
const lineQueue = [];
const waiters = [];
let stdinClosed = false;
rl.on("line", (line) => {
  const w = waiters.shift();
  if (w) w(line);
  else lineQueue.push(line);
});
rl.on("close", () => {
  stdinClosed = true;
  while (waiters.length) waiters.shift()(null);
});

// Ask a question; on EOF (piped input exhausted) exit cleanly rather than
// hanging or throwing an unhandled rejection.
async function ask(prompt) {
  process.stdout.write(prompt);
  let answer;
  if (lineQueue.length) answer = lineQueue.shift();
  else if (stdinClosed) answer = null;
  else answer = await new Promise((resolve) => waiters.push(resolve));
  if (answer === null) {
    say("");
    abort("aborted (stdin closed)");
  }
  return answer.trim();
}

async function confirm(prompt, defaultYes) {
  const suffix = defaultYes ? " [Y/n] " : " [y/N] ";
  const a = (await ask(prompt + suffix)).toLowerCase();
  if (a === "") return defaultYes;
  return a === "y" || a === "yes";
}

// Pick 1..count; a bare Enter takes `def`. Re-prompts on anything else.
async function choose(prompt, count, def) {
  while (true) {
    const raw = (await ask(prompt)) || String(def);
    const n = Number(raw);
    if (Number.isInteger(n) && n >= 1 && n <= count) return n;
    warn(`enter a number from 1 to ${count}`);
  }
}

// Pick a channel name from `allowed`; a bare Enter takes `def`.
async function chooseChannel(allowed, def) {
  while (true) {
    const raw = ((await ask(`Channel (${allowed.join("/")}) [${def}]: `)) || def)
      .toLowerCase();
    if (allowed.includes(raw)) return raw;
    warn(`enter one of: ${allowed.join(", ")}`);
  }
}

// --- process execution (array args, never shell:true — Windows quoting) -----

// Capture form: returns stdout (trimmed unless `raw`), or null when the
// command fails.
function capture(cmd, args, { raw = false } = {}) {
  const r = spawnSync(cmd, args, { cwd: root, encoding: "utf8" });
  if (r.status !== 0 || r.error) return null;
  return raw ? r.stdout || "" : (r.stdout || "").trim();
}

// Display-only quoting for printed commands (a path with spaces needs quotes).
const quoteForDisplay = (p) => (/\s/.test(p) ? `"${p}"` : p);

// Print recovery commands one per line: Windows PowerShell 5.1 can't parse `&&`.
// The intro is plain text; the caller has already printed the one ✗ line.
function sayCommands(intro, commands) {
  say(intro);
  for (const c of commands) say(`  ${c}`);
}

// Inherit form for visible mutating commands: streams to the terminal and
// hard-fails on nonzero exit, first printing `recovery` ([intro, ...commands])
// when given. In dry-run it only prints what it would run.
function run(cmd, args, recovery = null) {
  const printable = [cmd, ...args].join(" ");
  if (dryRun) {
    say(dim(`[dry-run] would run: ${printable}`));
    return;
  }
  const r = spawnSync(cmd, args, { cwd: root, stdio: "inherit" });
  if (r.error || r.status !== 0) {
    bad(`command failed: ${printable}`);
    if (recovery) sayCommands(recovery[0], recovery.slice(1));
    rl.close();
    process.exit(1);
  }
}

const git = (...args) => capture("git", args);

// `git status --porcelain -z`: NUL-separated `XY path` records, so paths come
// unquoted (the zh-TW readme's name would otherwise be octal-escaped). A
// rename/copy record, staged (X) or in the worktree (Y, from an intent-to-add
// entry), is followed by its source path, which is skipped.
function parseStatus(out) {
  const records = (out ?? "").split("\0");
  const entries = [];
  for (let i = 0; i < records.length; i++) {
    const rec = records[i];
    if (!rec) continue;
    const xy = rec.slice(0, 2);
    entries.push({ xy, path: rec.slice(3) });
    if ("RC".includes(xy[0]) || "RC".includes(xy[1])) i++;
  }
  return entries;
}

async function main() {
  say("");
  say(`🚀 ${bold("PlugY4Kolbot release")}${dryRun ? dim("  (dry run)") : ""}`);
  rule();

  // === 1. Preconditions =====================================================
  say(bold("Preconditions"));

  // git fetch first so freshness checks see the real origin state (a read, so
  // performed in dry-run too). An offline failure is a warning, not an abort.
  if (noFetch) {
    warn(
      "--no-fetch: skipped git fetch and the remote-tag lookup; freshness checks use the last fetched origin state",
    );
  } else {
    const fetch = spawnSync("git", ["fetch", "origin", "--tags"], {
      cwd: root,
      encoding: "utf8",
    });
    if (fetch.error || fetch.status !== 0) {
      warn("could not fetch; freshness checks may be stale");
    } else {
      ok("fetched origin --tags");
    }
  }

  const branch = git("rev-parse", "--abbrev-ref", "HEAD");
  if (branch === "master") {
    ok("on branch master");
  } else {
    fatal(`current branch is '${branch}', not master; releases ship only from master`);
  }

  // Clean tree: tracked changes abort; uncommitted changelog fragments abort
  // (a stable prepare would consume and DELETE them); other untracked → warn.
  const statusOut = capture(
    "git",
    ["status", "--porcelain", "-z", "--untracked-files=all"],
    { raw: true },
  );
  if (statusOut === null) abort("git status failed (is this a git checkout?)");
  const status = parseStatus(statusOut);
  const trackedDirty = status.filter((e) => e.xy !== "??");
  const untracked = status.filter((e) => e.xy === "??").map((e) => e.path);
  // Only what the assembler consumes counts: top-level FRAGMENT_RE matches.
  const untrackedFragments = untracked.filter((p) => {
    if (!p.startsWith("changelog.d/")) return false;
    const name = p.slice("changelog.d/".length);
    return !name.includes("/") && FRAGMENT_RE.test(name);
  });
  const otherUntracked = untracked.filter(
    (p) => !untrackedFragments.includes(p),
  );

  if (trackedDirty.length === 0) {
    ok("working tree has no tracked changes");
  } else {
    fatal(
      `working tree has ${trackedDirty.length} tracked change(s); the release commit must contain only release changes`,
    );
  }
  if (untrackedFragments.length > 0) {
    fatal(
      `uncommitted changelog fragment(s) present (${untrackedFragments.join(", ")}); a stable prepare would consume and DELETE them, so commit the feature first`,
    );
  } else {
    ok("no uncommitted changelog fragments");
  }
  if (otherUntracked.length > 0) {
    warn(
      `untracked files present (ignored by the release): ${otherUntracked.join(", ")}`,
    );
  }

  // master vs origin/master.
  const behindRaw = git("rev-list", "--count", "master..origin/master");
  const aheadRaw = git("rev-list", "--count", "origin/master..master");
  const behind = Number(behindRaw ?? "0");
  const ahead = Number(aheadRaw ?? "0");
  if (behindRaw === null || aheadRaw === null) {
    fatal("could not compare master with origin/master (missing ref?)");
  } else if (behind > 0) {
    fatal(`master is ${behind} commit(s) behind origin/master; pull first`);
  } else if (ahead > 0) {
    warn(
      `master is ${ahead} commit(s) ahead of origin/master; the release push will publish these:`,
    );
    const list = git("log", "origin/master..master", "--oneline") ?? "";
    for (const l of list.split("\n").filter(Boolean)) say(`    ${dim(l)}`);
    if (
      !dryRun &&
      !(await confirm("Continue and publish these commits?", false))
    ) {
      abort();
    }
  } else {
    ok("master is level with origin/master");
  }

  // gh availability (never fatal — the watch step degrades to printed URLs).
  const ghVersion = capture("gh", ["--version"]);
  const hasGh = ghVersion !== null;
  if (hasGh) {
    ok(`gh CLI available (${ghVersion.split("\n")[0]})`);
  } else {
    warn("gh CLI not found; release watch will degrade to printed URLs");
  }

  // Every stamp site must be findable now, not after the plan is confirmed.
  const banners = readBannerLines(root);
  const missing = banners.filter((b) => b.token === null);
  if (missing.length > 0) {
    fatal(
      `${missing.length} banner site(s) unusable: ${missing.map((b) => b.error).join("; ")}`,
    );
  } else {
    const tokens = [...new Set(banners.map((b) => b.token))].join(", ");
    ok(`${banners.length} banner sites found (currently ${tokens})`);
  }

  const fragmentProblems = validateFragments(join(root, "changelog.d"));
  if (fragmentProblems.length > 0) {
    for (const p of fragmentProblems) say(`    ${dim(`${p.file}: ${p.problem}`)}`);
    abort(
      `${fragmentProblems.length} invalid changelog fragment(s); run node scripts/changelog-draft.mjs --check`,
    );
  }
  ok("changelog fragments valid");

  // === 2. Context ===========================================================
  say("");
  say(bold("Context"));

  let header;
  try {
    header = parseHeader(readHeaderFile(root));
  } catch (err) {
    abort(`could not read the version: ${err.message}`);
  }
  const currentVersion = header.version;
  if (versionProblem(currentVersion)) {
    abort(`src/common/PlugYVersion.h: ${versionProblem(currentVersion)}`);
  }
  const headerIssue = headerProblem(header);
  if (headerIssue) {
    warn(`src/common/PlugYVersion.h: ${headerIssue} (the release rewrites all five lines)`);
  }

  const latestTag = latestSemverTag(git("tag", "--list", "v*"));

  say(
    `📦 Current version: ${bold(currentVersion)}   ${dim(`(latest release tag: ${latestTag ?? "none"})`)}`,
  );
  if (latestTag && `v${currentVersion}` !== latestTag) {
    warn(
      `src/common/PlugYVersion.h (v${currentVersion}) and the latest tag (${latestTag}) disagree; suggestions use the header version`,
    );
  }

  // Commits since the last STABLE release tag, for any target: that is what the
  // compare link and the notes cover. Counted to HEAD (master), so commits
  // ahead of origin that the push will publish are included. Before the first
  // release, the nearest non-pre-release tag: the inherited upstream v14.03.
  const sinceBase = commitBase({
    tagListOutput: git("tag", "--list", "v*"),
    describeTag: git("describe", "--tags", "--abbrev=0", "--exclude", "v*-*", "HEAD"),
  });
  const sinceRange = sinceBase ? `${sinceBase}..HEAD` : "HEAD";
  const sinceRaw = git("log", sinceRange, "--oneline", "--no-merges") ?? "";
  const sinceLines = sinceRaw.split("\n").filter(Boolean);
  const commitCount = sinceLines.length;
  say(
    `   ${commitCount} commit(s) since ${sinceBase ?? "the start of history"}:`,
  );
  for (const l of sinceLines.slice(0, 25)) say(`    ${dim(l)}`);
  if (sinceLines.length > 25)
    say(`    ${dim(`… and ${sinceLines.length - 25} more`)}`);

  // Pending fragments.
  const { groups } = readFragments(join(root, "changelog.d"));
  const fragmentBlocks = renderGroupBlocks(groups);
  const fragmentCount =
    groups.Added.length + groups.Changed.length + groups.Fixed.length;
  say("");
  say(`   ${bold("Pending changelog fragments:")} ${fragmentCount}`);
  if (fragmentBlocks.length) {
    for (const l of fragmentBlocks) say(`    ${l}`);
  }

  // Peek at CHANGELOG.md's [Unreleased] body (same locate logic as
  // changelog-release.mjs).
  const { body: unreleasedBody } = findUnreleased(
    readLF(join(root, "CHANGELOG.md")).split("\n"),
  );
  const unreleasedHasContent = unreleasedBody.some((l) => l.trim() !== "");
  if (unreleasedHasContent) {
    say(dim("   hand-written Unreleased notes present; will be merged"));
  }

  // === 3. Version selection =================================================
  say("");
  say(bold("Version"));
  say(
    `Current version: ${bold(currentVersion)}   ${dim(`(latest release tag: ${latestTag ?? "none"})`)}`,
  );

  let suggested;
  if (!isPrerelease(currentVersion)) {
    const hasAdded = groups.Added.length > 0;
    const menu = stableMenu(currentVersion);
    if (menu.length === 0) {
      abort(`no next version of ${currentVersion} fits the 13-character version limit`);
    }
    const defaultKey = defaultStableKey({ hasAdded, menu });
    const defaultIdx = menu.findIndex((o) => o.key === defaultKey) + 1;
    menu.forEach((o, i) =>
      say(
        `  ${i + 1}) ${o.label}${o.version ? ` → ${cyan(`v${o.version}`)}` : dim("  (an rc, beta or alpha of the next patch, minor or major)")}`,
      ),
    );
    if (menu.length < 4) {
      say(dim("   (options over the 13-character version limit are not offered)"));
    }
    if (defaultKey === "minor" && hasAdded) {
      say(dim("   (default Minor: an added-* fragment is pending)"));
    }
    const pick = menu[(await choose(`Choice [${defaultIdx}]: `, menu.length, defaultIdx)) - 1];
    if (pick.key === "prerelease") {
      // Only bases with a channel that fits are listed (the menu offered
      // Pre-release only because at least one exists), so the default always works.
      const { options: bases, defaultKind } = prereleaseBaseMenu(currentVersion, { hasAdded });
      const baseDefault = bases.findIndex((b) => b.kind === defaultKind) + 1;
      say("Pre-release of which bump?");
      bases.forEach((b, i) =>
        say(`  ${i + 1}) ${b.label} → ${cyan(`v${b.version}-<channel>.1`)}`),
      );
      const { kind } = bases[(await choose(`Base bump [${baseDefault}]: `, bases.length, baseDefault)) - 1];
      const channels = prereleaseChannels(currentVersion, kind);
      const dropped = ["rc", "beta", "alpha"].filter((c) => !channels.includes(c));
      if (dropped.length > 0) {
        say(
          dim(
            `   (${dropped.join(", ")} not offered: ${firstPrerelease(currentVersion, kind, dropped[0])} exceeds the 13-character version limit)`,
          ),
        );
      }
      const channel = await chooseChannel(channels, channels[0]);
      suggested = firstPrerelease(currentVersion, kind, channel);
    } else {
      suggested = pick.version;
    }
  } else {
    const { options, defaultKey } = prereleaseMenu(currentVersion);
    const targets = switchTargets(currentVersion);
    const defaultIdx = options.findIndex((o) => o.key === defaultKey) + 1;
    options.forEach((o, i) =>
      say(
        `  ${i + 1}) ${o.label}${o.version ? ` → ${cyan(`v${o.version}`)}` : dim(`  (${targets.join(" / ")})`)}`,
      ),
    );
    if (!options.some((o) => o.key === "next")) {
      say(dim("   (no next pre-release on this channel: it would exceed the 13-character version limit)"));
    }
    const pick = options[(await choose(`Choice [${defaultIdx}]: `, options.length, defaultIdx)) - 1];
    if (pick.key === "channel") {
      const channel = await chooseChannel(targets, targets[0]);
      suggested = switchChannel(currentVersion, channel);
    } else {
      suggested = pick.version;
    }
  }

  // The free-text override re-prompts on garbage / an existing tag without
  // losing the chosen suggestion.
  let chosen;
  while (true) {
    const candidate = normalizeCandidate(
      await ask(`Version [${suggested}]: `),
      suggested,
    );
    const formatProblem = versionProblem(candidate);
    if (formatProblem) {
      warn(formatProblem);
      continue;
    }
    const localTagExists = Boolean(git("tag", "--list", `v${candidate}`));
    let remoteTagExists = null;
    if (!noFetch) {
      const remote = git("ls-remote", "--tags", "origin", `refs/tags/v${candidate}`);
      if (remote === null) {
        warn("could not query origin's tags; the atomic push will still reject a duplicate");
      } else {
        remoteTagExists = remote.length > 0;
      }
    }
    const { problem, confirm: question } = assessCandidate({
      candidate,
      current: currentVersion,
      localTagExists,
      remoteTagExists,
    });
    if (problem) {
      warn(problem);
      continue;
    }
    if (question && !(await confirm(question, false))) continue;
    chosen = candidate;
    break;
  }
  const tag = `v${chosen}`;
  const prerelease = isPrerelease(chosen);
  const paths = releasePaths(prerelease);

  // A pre-release's notes come from the pending fragments, so only a stable
  // release can end up with empty notes in CHANGELOG.md.
  if (!prerelease && fragmentCount === 0 && !unreleasedHasContent) {
    warn("release notes will be empty (no fragments, no Unreleased body)");
    if (!(await confirm("Release with empty notes?", false))) abort();
  }

  // === 4. Plan + confirm ====================================================
  say("");
  rule();
  say(bold("Release plan"));
  const perCat = ["Added", "Changed", "Fixed"]
    .filter((c) => groups[c].length)
    .map((c) => `${groups[c].length} ${c}`)
    .join(", ");
  say(
    `  Version:   ${bold(chosen)}  (tag ${cyan(tag)}, ${prerelease ? yellow("pre-release") : "stable"})`,
  );
  say(
    `  Fragments: ${fragmentCount}${perCat ? ` (${perCat})` : ""}${prerelease ? dim("  (kept: a pre-release does not consume fragments; its notes render them)") : ""}`,
  );
  say(`  Commits:   ${commitCount} since ${sinceBase ?? "start"}`);
  say("  Steps:");
  say(
    prerelease
      ? `    1. node scripts/changelog-release.mjs ${chosen} (stamp the header + 8 banners)`
      : `    1. node scripts/changelog-release.mjs ${chosen} (assemble CHANGELOG.md, delete fragments, stamp the header + 8 banners)`,
  );
  say("    2. review the diff");
  say(
    `    3. commit "chore(release): ${tag}": the header and the 8 game-files banners${prerelease ? "" : ", CHANGELOG.md and the consumed fragments"}`,
  );
  say(`    4. annotated tag ${tag}`);
  say("    5. push master + tag atomically");
  rule();
  if (!(await confirm("Proceed?", true))) abort();

  // === 5. Prepare ===========================================================
  say("");
  say(bold("Preparing release…"));
  const prepareArgs = ["scripts/changelog-release.mjs", chosen];
  if (noMerge) prepareArgs.push("--no-merge");
  // In dry-run we must NOT invoke changelog-release.mjs: it rewrites the
  // stamped files and (stable) deletes fragments. The clean-tree gate passed,
  // so restoring exactly these paths undoes a partial prepare safely.
  const restoreCmd = `git restore -- ${paths.map(quoteForDisplay).join(" ")}`;
  run("node", prepareArgs, [
    "Nothing was committed. Undo whatever the prepare wrote with:",
    restoreCmd,
  ]);

  // === 6. Review gate =======================================================
  say("");
  say(bold("Review"));
  if (dryRun) {
    // Nothing was written, so there is no diff to gate on: skip the review
    // prompt entirely and preview the commit/tag/push steps below.
    say(dim("[dry-run] would run: git --no-pager diff --stat"));
    say(dim("[dry-run] review gate skipped (no changes to review)"));
  } else {
    run("git", ["--no-pager", "diff", "--stat"]);
  }

  while (!dryRun) {
    const a = (
      await ask("(y) commit + tag + push · (d) show the full diff · (n) abort: ")
    ).toLowerCase();
    if (a === "y" || a === "yes") break;
    if (a === "d") {
      // git shows the UTF-16 banners as binary, so their stamped lines are
      // decoded and printed after the text diff.
      const diffPaths = prerelease ? TEXT_DIFF_PATHS : [...TEXT_DIFF_PATHS, "changelog.d"];
      spawnSync("git", ["--no-pager", "diff", "--", ...diffPaths], {
        cwd: root,
        stdio: "inherit",
      });
      for (const b of readBannerLines(root).filter((x) => x.encoding === "utf16le")) {
        say(`${bold(b.path)} (UTF-16, shown decoded):`);
        say(`  ${b.line ?? dim(b.error)}`);
      }
      continue;
    }
    if (a === "n" || a === "no") {
      // Safe because the clean-tree gate passed: everything restored was clean
      // at start, and restoring changelog.d resurrects the deleted fragments.
      if (await confirm("Revert the prepared changes?", false)) {
        run("git", ["restore", "--", ...paths]);
      } else {
        say("To revert manually, run:");
        say(`  ${restoreCmd}`);
      }
      abort();
    }
    warn("enter y, d, or n");
  }

  // === 7. Commit + tag + push ===============================================
  say("");
  say(bold("Committing, tagging, pushing…"));
  const unstageAndRestore = [
    "Nothing was committed. Undo the staged release changes with:",
    `git restore --staged --worktree -- ${paths.map(quoteForDisplay).join(" ")}`,
  ];
  run("git", ["add", "--", ...paths], unstageAndRestore);
  run("git", ["commit", "-m", `chore(release): ${tag}`], unstageAndRestore);
  run("git", ["tag", "-a", tag, "-m", `PlugY4Kolbot ${tag}`], [
    `The release commit exists but tag ${tag} was not created. Undo the commit${prerelease ? "" : " (restores the fragments)"} with:`,
    "git reset --hard HEAD~1",
  ]);
  if (dryRun) {
    say(dim(`[dry-run] would run: git push --atomic origin master ${tag}`));
  } else {
    // One atomic push: both refs land or neither does. The tag is named
    // explicitly so the push never depends on --follow-tags or push.followTags.
    const push = spawnSync(
      "git",
      ["push", "--atomic", "origin", "master", tag],
      { cwd: root, stdio: "inherit" },
    );
    if (push.error || push.status !== 0) {
      say("");
      // Re-running beats rebasing the release commit: a rebased commit skips
      // prepare and review, and fragments arriving with the pull miss the notes.
      bad(`push failed (atomic, so neither master nor ${tag} reached origin).`);
      sayCommands(
        `Undo the local release${prerelease ? "" : " (restores the fragments)"}, update, and release again:`,
        [
          "git reset --hard HEAD~1",
          `git tag -d ${tag}`,
          "git pull origin master",
          "node scripts/release.mjs",
        ],
      );
      say(
        dim(
          `  (If origin rejected the tag because ${tag} already exists there, that version is taken: after the undo, run git fetch origin --tags and choose another version.)`,
        ),
      );
      say(
        dim(
          "  (Rejected by branch protection instead? Pulling won't help; check master's protection rules.)",
        ),
      );
      rl.close();
      process.exit(1);
    }
    ok(`pushed master + ${tag}`);
  }

  // === 8. Post-push =========================================================
  // The checklist always names the canonical repo. An origin still on the
  // pre-rename URL works through GitHub's redirect; the note says how to update it.
  const origin = parseOriginRemote(git("remote", "get-url", "origin"));
  if (origin && `${origin.owner}/${origin.repo}`.toLowerCase() !== REPO_SLUG.toLowerCase()) {
    say(
      dim(
        `   (origin is github.com/${origin.owner}/${origin.repo}; to point it at ${REPO_URL}: git remote set-url origin ${REPO_URL}.git)`,
      ),
    );
  }

  if (hasGh && !dryRun) {
    if (await confirm("Watch the release build now?", true)) {
      const runId = await findReleaseRun(tag);
      if (runId) {
        // Soft call (not the hard-failing run()): watching is long-running and
        // the user may Ctrl+C, so a nonzero exit (an interrupt or a red run)
        // must not suppress the closing checklist printed below. Ctrl+C reaches
        // node too, so SIGINT is ignored here while the watch child runs; the
        // handler stays one tick past the child so a queued signal is absorbed.
        const ignoreSigint = () => {};
        process.on("SIGINT", ignoreSigint);
        spawnSync("gh", ["run", "watch", String(runId)], {
          cwd: root,
          stdio: "inherit",
        });
        await new Promise((resolve) => setImmediate(resolve));
        process.off("SIGINT", ignoreSigint);
      } else {
        warn("release run did not appear yet; check the Actions page below");
      }
    }
  }

  say("");
  rule();
  say(green(`✅ ${dryRun ? "Dry run complete" : "Release pushed"}: ${tag}`));
  printChecklist(chosen, prerelease);
  rl.close();
  process.exit(0);
}

// Derive owner/repo from a GitHub origin URL (https or ssh, optional .git);
// null for any other remote.
function parseOriginRemote(url) {
  const m = url?.match(/github\.com[:/]([^/]+)\/(.+?)(?:\.git)?$/);
  return m ? { owner: m[1], repo: m[2] } : null;
}

// Poll gh for the release run whose headBranch is the tag (tag pushes surface
// the tag name there). Newest createdAt wins if several match.
async function findReleaseRun(tag) {
  for (let i = 0; i < 12; i++) {
    const json = capture("gh", [
      "run",
      "list",
      "--workflow=release.yml",
      "--limit",
      "10",
      "--json",
      "databaseId,headBranch,status,createdAt",
    ]);
    if (json) {
      try {
        const runs = JSON.parse(json)
          .filter((r) => r.headBranch === tag)
          .sort((a, b) => (a.createdAt < b.createdAt ? 1 : -1));
        if (runs.length) return runs[0].databaseId;
      } catch {
        // malformed output — fall through to retry / URL
      }
    }
    await sleep(5000);
  }
  return null;
}

function printChecklist(version, prerelease) {
  const tag = `v${version}`;
  const asset = `PlugY4Kolbot_${tag}`;
  say("");
  say(bold("Next steps:"));
  say("  1. Wait for the release workflow to go green:");
  say(`     ${REPO_URL}/actions/workflows/release.yml`);
  say(`  2. Review the DRAFT release "PlugY4Kolbot ${tag}": ${REPO_URL}/releases`);
  say(
    `     assets: ${asset}.zip, ${asset}_D2FilePatcher.zip, ${asset}_Installer.exe, ${asset}_symbols.zip, SHA256SUMS.txt`,
  );
  say("  3. Smoke-test the build on the C:\\D2Test rig.");
  if (prerelease) {
    say("  4. Publish the pre-release:");
    say(`     gh release edit ${tag} --draft=false`);
    say(dim("     (CI already marks it a pre-release, so it never takes Latest)"));
  } else {
    say("  4. Publish the release and mark it Latest:");
    say(`     gh release edit ${tag} --draft=false --latest`);
    say(dim("     (never pass --latest=false here: it strips Latest)"));
  }
}

main().catch((err) => {
  bad(`unexpected error: ${err?.message ?? err}`);
  rl.close();
  process.exit(1);
});

#!/usr/bin/env node
// Records what the Traditional Chinese readme covers in scripts/zh-readme-translated-to.txt, which release.mjs
// checks before a release. Run it in the pull request that catches the readme up, after translating:
//   node scripts/zh-readme-mark.mjs                  both marks: English translated, zh-TW readme checked
//   node scripts/zh-readme-mark.mjs --english-only   an English change that needs no translation
//   node scripts/zh-readme-mark.mjs --zh-only        zh-TW readme edits checked, nothing translated
// Each mark moves to --commit <rev>, by default the master commit the branch is based on
// (git merge-base HEAD origin/master). It lists the commits it is about to cover and asks first.

import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import { EN_README, planMark, ZH_MARKER, ZH_README } from "./readme-lag-lib.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

const USAGE = `usage: node scripts/zh-readme-mark.mjs [--english-only | --zh-only] [--commit <rev>] [--yes]
  --english-only  move only the english mark (an English change that needs no translation)
  --zh-only       move only the zh-tw mark (zh-TW readme edits checked, nothing translated)
  --commit <rev>  the master commit to record (default: git merge-base HEAD origin/master)
  --yes           write without asking
  --help          show this message
`;

function parseArgs(argv) {
  const opts = { mode: "both", commit: null, yes: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--help" || a === "-h") return { help: true };
    if (a === "--english-only" || a === "--zh-only") {
      if (opts.mode !== "both") return { error: "--english-only and --zh-only exclude each other" };
      opts.mode = a === "--english-only" ? "english" : "zh";
    } else if (a === "--commit") {
      // A value starting with "-" would reach git as an option.
      if (i + 1 >= argv.length || argv[i + 1].startsWith("-")) return { error: "--commit needs a commit" };
      opts.commit = argv[++i];
    } else if (a === "--yes") {
      opts.yes = true;
    } else {
      return { error: `unknown argument ${a}` };
    }
  }
  return opts;
}

const opts = parseArgs(process.argv.slice(2));
if (opts.help) {
  process.stdout.write(USAGE);
  process.exit(0);
}
if (opts.error) {
  process.stderr.write(`error: ${opts.error}\n${USAGE}`);
  process.exit(1);
}

const say = (s) => process.stdout.write(`${s}\n`);
const git = (...args) => {
  const r = spawnSync("git", args, { cwd: root, encoding: "utf8" });
  return r.status !== 0 || r.error ? null : (r.stdout || "").trim();
};
// git diff --quiet exits 1 when the path differs from HEAD.
const uncommitted = (path) => spawnSync("git", ["diff", "--quiet", "HEAD", "--", path], { cwd: root }).status === 1;

// One answer from stdin; EOF counts as no, so a piped or closed stdin never writes.
function askYes(prompt) {
  process.stdout.write(prompt);
  return new Promise((resolve) => {
    const rl = createInterface({ input: process.stdin });
    let answered = false;
    rl.once("line", (line) => {
      answered = true;
      rl.close();
      resolve(/^\s*y(es)?\s*$/i.test(line));
    });
    rl.once("close", () => {
      if (!answered) resolve(false);
    });
  });
}

const file = join(root, ZH_MARKER);
const current = existsSync(file) ? readFileSync(file, "utf8") : null;
const plan = planMark(git, { current, mode: opts.mode, commit: opts.commit });
if (plan.error) {
  process.stderr.write(`error: ${plan.error}\n`);
  process.exit(1);
}

const base = opts.commit ? "" : " (the branch's base on origin/master as of your last fetch)";
say(`Recording ${git("log", "-1", "--format=%h %s", plan.target)}${base}`);
const stillListed = [];
const show = (title, list, own, onMaster, path, what) => {
  if (list === null) return;
  say(`${title}: ${list.length === 0 ? "none" : list.length}`);
  for (const c of list) say(`  ${c.sha.slice(0, 7)} ${c.subject}`);
  const counted = onMaster ? "which the check will still list (see the note below)" : "which its squash merge counts too";
  if (own.length > 0) {
    say(`  plus this branch's own ${own.length} commit(s), ${counted}:`);
    for (const c of own) say(`    ${c.sha.slice(0, 7)} ${c.subject}`);
  }
  const dirty = uncommitted(path);
  if (dirty) say(`  plus the uncommitted ${what} edits in your working tree`);
  if (onMaster && (own.length > 0 || dirty)) stillListed.push(what);
};
show("English readme commits this marks as translated", plan.english, plan.ownEnglish, plan.onMaster.english, EN_README, "English readme");
show("Traditional Chinese readme commits this marks as checked", plan.zh, plan.ownZh, plan.onMaster.zhTw, ZH_README, "Traditional Chinese readme");
if (stillListed.length > 0) {
  say(
    `Note: origin/master already records this mark for the ${stillListed.join(" and ")}, so this pull request's squash merge won't record it and its own edits there stay listed. Merge origin/master into the branch, commit the merge, and run this again.`,
  );
}

if (current !== null && current.split("\r\n").join("\n") === plan.text) {
  say(`${ZH_MARKER} already says this; nothing to write.`);
  process.exit(0);
}
if (!opts.yes && !(await askYes(`Write ${ZH_MARKER}? [y/N] `))) {
  say("Not written.");
  process.exit(1);
}
writeFileSync(file, plan.text);
say(`Wrote ${ZH_MARKER}; commit it with the catch-up.`);

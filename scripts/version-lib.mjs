// PlugY's version model and the files that carry it. Commons/PlugYVersion.h is
// the single source for the binaries; the 8 installer text files repeat the
// version in their `;...;` banner box. changelog-release.mjs stamps both, and
// release.mjs commits exactly STAMPED_FILES (+ the changelog on a stable
// release), so the two can never drift. Node built-ins only (no deps).
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

// --- semver -----------------------------------------------------------------

// The pre-release channels, lowest precedence first.
export const CHANNELS = ["alpha", "beta", "rc"];

export const SEMVER_RE =
  /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(alpha|beta|rc)\.(0|[1-9]\d*))?$/;

// The main menu prints the version on its own line of a fixed-width text box, under
// "PlugY4Kolbot", and a static_assert in PlugY/MainScreen.cpp enforces the same bound at build time.
export const MAX_VERSION_LENGTH = 13;

/** Why `v` can't be a release version, or null when it can. */
export function versionProblem(v) {
  if (typeof v !== "string" || !SEMVER_RE.test(v)) {
    return `'${v}' is not a valid version: use X.Y.Z or X.Y.Z-alpha.N, -beta.N, -rc.N`;
  }
  if (v.length > MAX_VERSION_LENGTH) {
    return `'${v}' is ${v.length} characters, over the ${MAX_VERSION_LENGTH}-character limit: the main menu prints the version on its own line of a fixed-width text box, and a static_assert in PlugY/MainScreen.cpp enforces the same limit`;
  }
  return null;
}

const fits = (v) => versionProblem(v) === null;

/**
 * Parse a version into `{ major, minor, patch, channel, num, base }` (`channel`
 * and `num` are null for a stable version; `base` is always X.Y.Z), or null
 * when `v` doesn't match SEMVER_RE. Shape only: versionProblem adds the length
 * limit, so precedence still works for a version that is too long to ship.
 */
export function parseVersion(v) {
  if (typeof v !== "string" || !SEMVER_RE.test(v)) return null;
  const [, major, minor, patch, channel, num] = v.match(SEMVER_RE);
  return {
    major: Number(major),
    minor: Number(minor),
    patch: Number(patch),
    channel: channel ?? null,
    num: num === undefined ? null : Number(num),
    base: `${major}.${minor}.${patch}`,
  };
}

const mustParse = (v) => {
  const p = parseVersion(v);
  if (!p) throw new Error(versionProblem(v));
  return p;
};

export const isPrerelease = (v) => mustParse(v).channel !== null;

/**
 * SemVer 2.0.0 precedence: numeric per-part compare, a pre-release sorts below
 * its release, alpha < beta < rc, and N compares numerically. Returns <0, 0, >0.
 */
export function compareVersions(a, b) {
  const pa = mustParse(a);
  const pb = mustParse(b);
  for (const k of ["major", "minor", "patch"]) {
    if (pa[k] !== pb[k]) return pa[k] - pb[k];
  }
  if (pa.channel === pb.channel) return (pa.num ?? 0) - (pb.num ?? 0);
  if (pa.channel === null) return 1;
  if (pb.channel === null) return -1;
  return CHANNELS.indexOf(pa.channel) - CHANNELS.indexOf(pb.channel);
}

/** Bump the X.Y.Z part of `v` (any pre-release suffix is dropped). */
export function bump(v, kind) {
  const { major, minor, patch } = mustParse(v);
  if (kind === "major") return `${major + 1}.0.0`;
  if (kind === "minor") return `${major}.${minor + 1}.0`;
  if (kind === "patch") return `${major}.${minor}.${patch + 1}`;
  throw new Error(`unknown bump kind '${kind}'`);
}

/** The first pre-release of the bumped version, e.g. 14.3.0 + minor + rc → 14.4.0-rc.1. */
export function firstPrerelease(v, kind, channel) {
  if (!CHANNELS.includes(channel)) throw new Error(`unknown channel '${channel}'`);
  return `${bump(v, kind)}-${channel}.1`;
}

/** Channels that sort above `channel` (a pre-release only moves forward). */
export function laterChannels(channel) {
  const i = CHANNELS.indexOf(channel);
  if (i === -1) throw new Error(`unknown channel '${channel}'`);
  return CHANNELS.slice(i + 1);
}

// --- release-menu decisions (pure, so tests cover them without a TTY) -------
// Menus only offer versions within MAX_VERSION_LENGTH: at 14.x, every
// -alpha.N is 14 characters, so alpha drops out until a shorter base.

/**
 * The channels offered for a first pre-release of `kind`, in prompt order
 * (rc first, the default), keeping only those whose version fits.
 */
export const prereleaseChannels = (current, kind) =>
  [...CHANNELS]
    .reverse()
    .filter((ch) => fits(firstPrerelease(current, kind, ch)));

/** Later channels a pre-release can switch to, keeping only those that fit. */
export function switchTargets(current) {
  const { base, channel } = mustParse(current);
  if (channel === null) throw new Error(`${current} is not a pre-release`);
  return laterChannels(channel).filter((ch) => fits(`${base}-${ch}.1`));
}

const BUMP_KINDS = [
  ["patch", "Patch"],
  ["minor", "Minor"],
  ["major", "Major"],
];

/** The bump kinds that have at least one pre-release channel that fits. */
export const prereleaseBases = (current) =>
  BUMP_KINDS.map(([kind]) => kind).filter(
    (kind) => prereleaseChannels(current, kind).length > 0,
  );

/**
 * The base-bump prompt of a first pre-release: only the bases from
 * prereleaseBases, as `{ kind, label, version }`. The default is the stable
 * menu's preference (minor with an added-* fragment, else patch) when it is
 * listed, else the first listed base, so the default can always be accepted.
 */
export function prereleaseBaseMenu(current, { hasAdded }) {
  const listed = prereleaseBases(current);
  const options = BUMP_KINDS.filter(([kind]) => listed.includes(kind)).map(
    ([kind, label]) => ({ kind, label, version: bump(current, kind) }),
  );
  const preferred = hasAdded ? "minor" : "patch";
  const defaultKind = listed.includes(preferred) ? preferred : (listed[0] ?? null);
  return { options, defaultKind };
}

/**
 * Menu for a stable current version: Patch / Minor / Major / Pre-release, each
 * only when it fits. The pre-release entry carries no version: release.mjs asks
 * for its base bump and a channel from prereleaseChannels, then calls
 * firstPrerelease.
 */
export function stableMenu(current) {
  if (isPrerelease(current)) throw new Error(`${current} is a pre-release`);
  const options = BUMP_KINDS.map(([key, label]) => ({
    key,
    label,
    version: bump(current, key),
  })).filter((o) => fits(o.version));
  if (prereleaseBases(current).length > 0) {
    options.push({ key: "prerelease", label: "Pre-release", version: null });
  }
  return options;
}

/**
 * The stable menu's default: a wave carrying an added-* fragment is a feature
 * wave, so minor; otherwise patch. When that option was filtered out, the
 * first offered option.
 */
export function defaultStableKey({ hasAdded, menu }) {
  const preferred = hasAdded ? "minor" : "patch";
  if (!menu || menu.some((o) => o.key === preferred)) return preferred;
  return menu[0]?.key ?? null;
}

/**
 * Menu for a pre-release current version X.Y.Z-ch.N: release X.Y.Z, the next
 * pre-release on the same channel, or a later channel (each only when it fits).
 * Defaults to the next pre-release, else the channel switch, and only then the
 * release, so Enter consumes the fragments only when nothing else is offered.
 */
export function prereleaseMenu(current) {
  const { base, channel, num } = mustParse(current);
  if (channel === null) throw new Error(`${current} is not a pre-release`);
  const options = [{ key: "release", label: `Release ${base}`, version: base }];
  const next = `${base}-${channel}.${num + 1}`;
  if (fits(next)) {
    options.push({ key: "next", label: "Next pre-release", version: next });
  }
  if (switchTargets(current).length > 0) {
    options.push({
      key: "channel",
      label: "Switch to a later channel",
      version: null,
    });
  }
  const defaultKey = ["next", "channel", "release"].find((k) =>
    options.some((o) => o.key === k),
  );
  return { options, defaultKey };
}

/** Move X.Y.Z-ch.N to X.Y.Z-<channel>.1; only later channels that fit are allowed. */
export function switchChannel(current, channel) {
  const { base, channel: from } = mustParse(current);
  if (from === null) throw new Error(`${current} is not a pre-release`);
  if (!laterChannels(from).includes(channel)) {
    throw new Error(`'${channel}' is not a later channel than '${from}'`);
  }
  const target = `${base}-${channel}.1`;
  const problem = versionProblem(target);
  if (problem) throw new Error(problem);
  return target;
}

/** The typed override (or the suggestion on a bare Enter), without a leading v. */
export const normalizeCandidate = (raw, suggested) =>
  (raw || suggested).trim().replace(/^v/, "");

/**
 * Judge an override candidate. `problem` rejects it outright (re-prompt);
 * `confirm` is a question to put before accepting it. `remoteTagExists` is
 * null when the remote was not queried (--no-fetch, or offline).
 */
export function assessCandidate({
  candidate,
  current,
  localTagExists,
  remoteTagExists,
}) {
  const problem = versionProblem(candidate);
  if (problem) return { problem, confirm: null };
  if (localTagExists) {
    return {
      problem: `tag v${candidate} already exists locally; choose another version`,
      confirm: null,
    };
  }
  if (remoteTagExists) {
    return {
      problem: `tag v${candidate} already exists on origin; choose another version`,
      confirm: null,
    };
  }
  if (compareVersions(candidate, current) <= 0) {
    return {
      problem: null,
      confirm: `v${candidate} is not greater than the current v${current}; proceed anyway?`,
    };
  }
  return { problem: null, confirm: null };
}

/**
 * The newest release tag from `git tag --list` output. Only semver tags count,
 * so the inherited upstream tags (v14.03 and older) never match. Sorted here
 * rather than by git's -v:refname, which ranks v1.0.0-rc.1 above v1.0.0.
 */
export function latestSemverTag(tagListOutput, { stableOnly = false } = {}) {
  const tags = (tagListOutput ?? "")
    .split("\n")
    .map((t) => t.trim())
    .filter((t) => {
      const p = t.startsWith("v") ? parseVersion(t.slice(1)) : null;
      return p !== null && (!stableOnly || p.channel === null);
    });
  tags.sort((a, b) => compareVersions(b.slice(1), a.slice(1)));
  return tags[0] ?? null;
}

/**
 * Where "commits since" starts: the last STABLE release tag, whatever the
 * target, matching the compare link and the notes (a pre-release's notes
 * cover everything since the last stable release). Before the first one,
 * `describeTag`: the nearest NON-pre-release tag (callers pass
 * `git describe --exclude "v*-*"`), today the inherited upstream v14.03.
 */
export const commitBase = ({ tagListOutput, describeTag }) =>
  latestSemverTag(tagListOutput, { stableOnly: true }) ?? describeTag ?? null;

// --- Commons/PlugYVersion.h ---------------------------------------------------

export const HEADER_PATH = "Commons/PlugYVersion.h";

// Line-anchored (m flag), so `$` stops before the CRLF's \r and it survives.
const HEADER_LINES = {
  major: /^(#define PLUGY_VERSION_MAJOR )(\d+)$/,
  minor: /^(#define PLUGY_VERSION_MINOR )(\d+)$/,
  patch: /^(#define PLUGY_VERSION_PATCH )(\d+)$/,
  version: /^(#define PLUGY_VERSION ")([^"\r\n]*)(")$/,
  fileVersion: /^(#define PLUGY_VERSION_FILE ")([^"\r\n]*)(")$/,
};

const globalOf = (re) => new RegExp(re.source, "gm");

function exactlyOne(text, re, what) {
  const matches = [...text.matchAll(globalOf(re))];
  if (matches.length !== 1) {
    throw new Error(
      `${HEADER_PATH}: expected exactly one ${what} line, found ${matches.length}`,
    );
  }
  return matches[0];
}

/** Parse the five version lines: `{ major, minor, patch, version, fileVersion }`. */
export function parseHeader(text) {
  const out = {};
  for (const [key, re] of Object.entries(HEADER_LINES)) {
    const value = exactlyOne(text, re, key)[2];
    out[key] = re.source.includes('"') ? value : Number(value);
  }
  return out;
}

/** Why the header's five lines disagree, or null when they are consistent. */
export function headerProblem(h) {
  const problem = versionProblem(h.version);
  if (problem) return `PLUGY_VERSION: ${problem}`;
  const p = parseVersion(h.version);
  if (h.major !== p.major || h.minor !== p.minor || h.patch !== p.patch) {
    return `MAJOR/MINOR/PATCH (${h.major}.${h.minor}.${h.patch}) disagree with PLUGY_VERSION "${h.version}"`;
  }
  if (h.fileVersion !== `${p.base}.0`) {
    return `PLUGY_VERSION_FILE "${h.fileVersion}" should be "${p.base}.0"`;
  }
  return null;
}

/**
 * Rewrite the five version lines for `version`; every other byte is kept.
 * PLUGY_VERSION_FILE is X.Y.Z.0 even for a pre-release: Windows file versions
 * are four numbers and cannot carry one.
 */
export function renderHeader(text, version) {
  const problem = versionProblem(version);
  if (problem) throw new Error(problem);
  const p = parseVersion(version);
  const values = {
    major: String(p.major),
    minor: String(p.minor),
    patch: String(p.patch),
    version,
    fileVersion: `${p.base}.0`,
  };
  let out = text;
  for (const [key, re] of Object.entries(HEADER_LINES)) {
    const m = exactlyOne(out, re, key);
    out =
      out.slice(0, m.index) +
      m[1] +
      values[key] +
      (m[3] ?? "") +
      out.slice(m.index + m[0].length);
  }
  return out;
}

// latin1 maps every byte to one code unit and back, so the read/write is byte-exact.
export const readHeaderFile = (root) =>
  readFileSync(join(root, HEADER_PATH), "latin1");

export function writeHeaderFile(root, text) {
  writeFileSync(join(root, HEADER_PATH), text, "latin1");
}

// --- installer banner sites ---------------------------------------------------

// latin1 keeps the cp1252/ASCII files byte-exact; utf16le files open with FF FE.
export const BANNER_FILES = [
  { path: "PlugYInstaller/PlugY.ini", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY/PlugYDefault.ini", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY/PlugYFixed.ini", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY_The_Survival_Kit_-_Readme.txt", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY_The_Survival_Kit_-_LisezMoi.txt", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY_The_Survival_Kit_-_Liesmich.txt", encoding: "latin1" },
  { path: "PlugYInstaller/PlugY/LocalizedStrings.ini", encoding: "utf16le" },
  { path: "PlugYInstaller/PlugY_The_Survival_Kit_-_讀我檔案.txt", encoding: "utf16le" },
];

/** Every file a release stamps: changelog-release.mjs writes these, release.mjs commits them. */
export const STAMPED_FILES = [HEADER_PATH, ...BANNER_FILES.map((f) => f.path)];

/** The release commit's paths: a stable release also consumes the changelog. */
export const releasePaths = (prerelease) =>
  prerelease
    ? [...STAMPED_FILES]
    : [...STAMPED_FILES, "CHANGELOG.md", "changelog.d"];

// The version line of the 88-column `;...;` box; the zh-TW readme says 版本.
const BANNER_RE = /^;( +)(version|版本) (\S+)( +);$/gm;

const BOM = Buffer.from([0xff, 0xfe]);

function decodeBanner(buf, encoding, path) {
  if (encoding === "latin1") return buf.toString("latin1");
  if (buf.length < 2 || buf[0] !== 0xff || buf[1] !== 0xfe) {
    throw new Error(`${path}: expected a UTF-16LE file starting with an FF FE BOM`);
  }
  if (buf.length % 2 !== 0) {
    throw new Error(`${path}: odd byte count for a UTF-16LE file`);
  }
  return buf.subarray(2).toString("utf16le");
}

const encodeBanner = (text, encoding) =>
  encoding === "latin1"
    ? Buffer.from(text, "latin1")
    : Buffer.concat([BOM, Buffer.from(text, "utf16le")]);

/**
 * Read each banner file's version line: `[{ path, encoding, line, token, error }]`.
 * Never throws: an unreadable file, a missing BOM, or anything but exactly one
 * banner line comes back as `token: null` with `error` saying why.
 */
export function readBannerLines(root) {
  return BANNER_FILES.map(({ path, encoding }) => {
    try {
      const text = decodeBanner(readFileSync(join(root, path)), encoding, path);
      const matches = [...text.matchAll(BANNER_RE)];
      if (matches.length !== 1) {
        return {
          path,
          encoding,
          line: null,
          token: null,
          error: `${path}: expected exactly one banner line, found ${matches.length}`,
        };
      }
      return { path, encoding, line: matches[0][0], token: matches[0][3], error: null };
    } catch (err) {
      return { path, encoding, line: null, token: null, error: err.message };
    }
  });
}

/** Each banner file's current version token: `[{ path, token }]` (null when unreadable). */
export const readBannerTokens = (root) =>
  readBannerLines(root).map(({ path, token }) => ({ path, token }));

/**
 * Compute the stamped bytes of all 8 banner files without writing any. Throws
 * one error naming every bad file (not exactly one banner line, the token not
 * fitting the box, a decode that would not round-trip), so a release never
 * stamps some files and not others.
 */
export function planBannerStamp(root, version) {
  const problem = versionProblem(version);
  if (problem) throw new Error(problem);
  const plans = [];
  const problems = [];
  for (const { path, encoding } of BANNER_FILES) {
    const abs = join(root, path);
    let buf;
    let text;
    try {
      buf = readFileSync(abs);
      text = decodeBanner(buf, encoding, path);
    } catch (err) {
      problems.push(err.message);
      continue;
    }
    if (!encodeBanner(text, encoding).equals(buf)) {
      problems.push(`${path}: does not round-trip as ${encoding}; refusing to rewrite it`);
      continue;
    }
    const matches = [...text.matchAll(BANNER_RE)];
    if (matches.length !== 1) {
      problems.push(
        `${path}: expected exactly one ';  version <token>  ;' banner line, found ${matches.length}`,
      );
      continue;
    }
    const m = matches[0];
    const [line, left, label, token, right] = m;
    const pad = right.length + token.length - version.length;
    if (pad < 1) {
      problems.push(`${path}: '${version}' does not fit the banner box`);
      continue;
    }
    const next = `;${left}${label} ${version}${" ".repeat(pad)};`;
    const stamped = text.slice(0, m.index) + next + text.slice(m.index + line.length);
    plans.push({
      path,
      abs,
      from: token,
      to: version,
      bytes: encodeBanner(stamped, encoding),
    });
  }
  if (problems.length > 0) {
    throw new Error(`banner stamp refused, no file written:\n  ${problems.join("\n  ")}`);
  }
  return plans;
}

/**
 * Write the bytes computed by planBannerStamp. A write failing midway (a
 * read-only or locked file) throws an error naming the failed file and the
 * ones already stamped, so the caller can say what to restore.
 */
export function writeBannerPlans(plans) {
  const written = [];
  for (const p of plans) {
    try {
      writeFileSync(p.abs, p.bytes);
    } catch (err) {
      const error = new Error(
        `could not write ${p.path} (${err.code ?? err.message}); already stamped: ${written.length ? written.join(", ") : "none"}`,
      );
      error.written = written;
      throw error;
    }
    written.push(p.path);
  }
}

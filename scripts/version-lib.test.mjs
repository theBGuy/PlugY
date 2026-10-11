// version-lib: semver precedence, the release-menu decisions, header I/O and
// banner stamping. Banner tests run on byte-exact COPIES of the 8 real files
// in a throwaway mkdtemp dir, never on the tree itself.
import assert from "node:assert/strict";
import {
  chmodSync,
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import {
  assessCandidate,
  BANNER_FILES,
  bump,
  commitBase,
  compareVersions,
  defaultStableKey,
  firstPrerelease,
  HEADER_PATH,
  headerProblem,
  isPrerelease,
  laterChannels,
  latestSemverTag,
  normalizeCandidate,
  parseHeader,
  parseVersion,
  planBannerStamp,
  prereleaseBaseMenu,
  prereleaseBases,
  prereleaseChannels,
  prereleaseMenu,
  readBannerLines,
  readBannerTokens,
  releasePaths,
  renderHeader,
  STAMPED_FILES,
  stableMenu,
  switchChannel,
  switchTargets,
  versionProblem,
  writeBannerPlans,
} from "./version-lib.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

// --- semver -----------------------------------------------------------------

test("valid versions parse; malformed ones are refused", () => {
  assert.deepEqual(parseVersion("14.3.0"), {
    major: 14,
    minor: 3,
    patch: 0,
    channel: null,
    num: null,
    base: "14.3.0",
  });
  assert.equal(parseVersion("14.4.0-rc.2").channel, "rc");
  assert.equal(parseVersion("14.4.0-rc.2").num, 2);
  for (const bad of [
    "14.03",
    "v14.3.0",
    "14.3",
    "01.3.0",
    "14.3.0-rc",
    "14.3.0-rc.01",
    "14.3.0-RC.1",
    "14.3.0-preview.1",
    "14.3.0+build",
    "",
  ]) {
    assert.equal(parseVersion(bad), null, bad);
    assert.match(versionProblem(bad), /not a valid version/, bad);
  }
});

test("the 13-character limit names the main-menu text box and the static_assert", () => {
  assert.equal(versionProblem("14.10.0-rc.10"), null); // exactly 13
  const problem = versionProblem("14.10.0-beta.1"); // 14
  assert.match(problem, /14 characters/);
  assert.match(problem, /version on its own line of a fixed-width text box/);
  assert.match(problem, /static_assert/);
  // At 14.x every alpha is over the limit.
  assert.match(versionProblem("14.4.0-alpha.1"), /13-character limit/);
  // parseVersion is shape-only, so precedence still works on an over-long version.
  assert.equal(parseVersion("14.10.0-beta.1").channel, "beta");
});

test("precedence follows SemVer 2.0.0", () => {
  const ordered = [
    "14.3.0",
    "14.3.1",
    "14.4.0-alpha.1",
    "14.4.0-alpha.2",
    "14.4.0-alpha.10",
    "14.4.0-beta.1",
    "14.4.0-rc.1",
    "14.4.0-rc.2",
    "14.4.0",
    "14.10.0",
    "15.0.0",
  ];
  for (let i = 0; i < ordered.length; i++) {
    for (let j = 0; j < ordered.length; j++) {
      const c = Math.sign(compareVersions(ordered[i], ordered[j]));
      assert.equal(c, Math.sign(i - j), `${ordered[i]} vs ${ordered[j]}`);
    }
  }
});

test("bumps and first pre-releases", () => {
  assert.equal(bump("14.3.0", "patch"), "14.3.1");
  assert.equal(bump("14.3.0", "minor"), "14.4.0");
  assert.equal(bump("14.3.0", "major"), "15.0.0");
  assert.equal(firstPrerelease("14.3.0", "minor", "rc"), "14.4.0-rc.1");
  assert.equal(firstPrerelease("14.3.0", "patch", "alpha"), "14.3.1-alpha.1");
  assert.equal(firstPrerelease("14.3.0", "major", "beta"), "15.0.0-beta.1");
  assert.throws(() => firstPrerelease("14.3.0", "minor", "preview"));
  assert.equal(isPrerelease("14.4.0-beta.3"), true);
  assert.equal(isPrerelease("14.4.0"), false);
});

// --- menu decisions -----------------------------------------------------------

test("stable menu: patch/minor/major/pre-release; default minor only with an added fragment", () => {
  assert.deepEqual(
    stableMenu("14.3.0").map((o) => [o.key, o.version]),
    [
      ["patch", "14.3.1"],
      ["minor", "14.4.0"],
      ["major", "15.0.0"],
      ["prerelease", null],
    ],
  );
  assert.equal(defaultStableKey({ hasAdded: true }), "minor");
  assert.equal(defaultStableKey({ hasAdded: false }), "patch");
  assert.throws(() => stableMenu("14.4.0-rc.1"), /pre-release/);
});

test("pre-release menu covers every transition", () => {
  // alpha only fits a shorter base than 14.x.
  const alpha = prereleaseMenu("1.4.0-alpha.2");
  assert.equal(alpha.defaultKey, "next");
  assert.deepEqual(
    alpha.options.map((o) => [o.key, o.version]),
    [
      ["release", "1.4.0"],
      ["next", "1.4.0-alpha.3"],
      ["channel", null],
    ],
  );
  assert.deepEqual(laterChannels("alpha"), ["beta", "rc"]);
  assert.deepEqual(switchTargets("1.4.0-alpha.2"), ["beta", "rc"]);
  assert.equal(switchChannel("1.4.0-alpha.2", "beta"), "1.4.0-beta.1");
  assert.equal(switchChannel("1.4.0-alpha.2", "rc"), "1.4.0-rc.1");

  const beta = prereleaseMenu("14.4.0-beta.1");
  assert.equal(beta.defaultKey, "next");
  assert.deepEqual(
    beta.options.map((o) => [o.key, o.version]),
    [
      ["release", "14.4.0"],
      ["next", "14.4.0-beta.2"],
      ["channel", null],
    ],
  );
  assert.deepEqual(laterChannels("beta"), ["rc"]);
  assert.deepEqual(switchTargets("14.4.0-beta.1"), ["rc"]);
  assert.equal(switchChannel("14.4.0-beta.1", "rc"), "14.4.0-rc.1");
  assert.throws(() => switchChannel("14.4.0-beta.1", "alpha"), /later channel/);
  assert.throws(() => switchChannel("14.4.0-beta.1", "beta"), /later channel/);

  // rc has no later channel, so the switch option is not offered.
  const rc = prereleaseMenu("14.4.0-rc.9");
  assert.equal(rc.defaultKey, "next");
  assert.deepEqual(
    rc.options.map((o) => [o.key, o.version]),
    [
      ["release", "14.4.0"],
      ["next", "14.4.0-rc.10"],
    ],
  );
  assert.deepEqual(laterChannels("rc"), []);
  assert.deepEqual(switchTargets("14.4.0-rc.9"), []);
  assert.throws(() => prereleaseMenu("14.4.0"), /not a pre-release/);
});

test("menus never offer a version over the 13-character limit", () => {
  // 14.4.0-beta.10 is 14 characters: no next beta, so the default is the channel
  // switch, never the release (which would consume the fragments).
  const beta9 = prereleaseMenu("14.4.0-beta.9");
  assert.equal(beta9.defaultKey, "channel");
  assert.deepEqual(
    beta9.options.map((o) => [o.key, o.version]),
    [
      ["release", "14.4.0"],
      ["channel", null],
    ],
  );
  // 14.10.0-rc.1 fits and 14.10.0-beta.1 does not.
  assert.deepEqual(switchTargets("14.10.0-beta.1"), ["rc"]);
  assert.deepEqual(
    prereleaseMenu("14.10.0-rc.9").options.map((o) => o.key),
    ["release", "next"],
  );
  // Only when nothing else fits does the release become the default.
  const rc99 = prereleaseMenu("14.10.0-rc.99");
  assert.deepEqual(rc99.options.map((o) => o.key), ["release"]);
  assert.equal(rc99.defaultKey, "release");
  assert.throws(() => switchChannel("14.10.0-alpha.1", "beta"), /13-character limit/);

  assert.deepEqual(prereleaseChannels("14.3.0", "minor"), ["rc", "beta"]);
  assert.deepEqual(prereleaseChannels("14.3.0", "patch"), ["rc", "beta"]);
  assert.deepEqual(prereleaseChannels("1.2.0", "minor"), ["rc", "beta", "alpha"]);
  assert.deepEqual(prereleaseChannels("14.9.0", "minor"), ["rc"]); // 14.10.0-beta.1 is 14
  for (const kind of ["patch", "minor", "major"]) {
    for (const ch of prereleaseChannels("14.3.0", kind)) {
      assert.equal(versionProblem(firstPrerelease("14.3.0", kind, ch)), null);
    }
  }
});

test("the stable menu omits options over the limit and the default follows", () => {
  // 1234.5678.1000 is 14 characters; only Major has a pre-release that fits.
  const menu = stableMenu("1234.5678.999");
  assert.deepEqual(
    menu.map((o) => [o.key, o.version]),
    [
      ["minor", "1234.5679.0"],
      ["major", "1235.0.0"],
      ["prerelease", null],
    ],
  );
  assert.deepEqual(prereleaseBases("1234.5678.999"), ["major"]);
  assert.equal(defaultStableKey({ hasAdded: false, menu }), "minor");
  assert.equal(defaultStableKey({ hasAdded: true, menu }), "minor");

  // No pre-release of any bump fits, so the Pre-release entry is not offered.
  const tight = stableMenu("9999.9999.99");
  assert.deepEqual(tight.map((o) => o.key), ["patch", "minor", "major"]);
  assert.deepEqual(prereleaseBases("9999.9999.99"), []);
  assert.equal(defaultStableKey({ hasAdded: true, menu: tight }), "minor");

  const full = stableMenu("14.3.0");
  assert.equal(defaultStableKey({ hasAdded: true, menu: full }), "minor");
  assert.equal(defaultStableKey({ hasAdded: false, menu: full }), "patch");
  assert.deepEqual(prereleaseBases("14.9.0"), ["patch", "minor", "major"]);
  for (const current of ["14.3.0", "1234.5678.999", "9999.9999.99"]) {
    for (const o of stableMenu(current)) {
      if (o.version) assert.equal(versionProblem(o.version), null, o.version);
    }
  }
});

test("the pre-release base prompt lists only bases that fit, and its default is listed", () => {
  const keys = (m) => m.options.map((o) => o.kind);
  const full = prereleaseBaseMenu("14.3.0", { hasAdded: true });
  assert.deepEqual(keys(full), ["patch", "minor", "major"]);
  assert.equal(full.defaultKind, "minor");
  assert.equal(full.options[1].version, "14.4.0");
  assert.equal(prereleaseBaseMenu("14.3.0", { hasAdded: false }).defaultKind, "patch");
  // 14.10.101-rc.1 and 99.99.100-rc.1 are 14 characters: patch drops out, and
  // the patch preference falls back to the first listed base.
  for (const current of ["14.10.100", "99.99.99"]) {
    for (const hasAdded of [false, true]) {
      const m = prereleaseBaseMenu(current, { hasAdded });
      assert.deepEqual(keys(m), ["minor", "major"], current);
      assert.equal(m.defaultKind, "minor", current);
      assert.ok(keys(m).includes(m.defaultKind));
    }
  }
  const majorOnly = prereleaseBaseMenu("1234.5678.999", { hasAdded: true });
  assert.deepEqual(keys(majorOnly), ["major"]);
  assert.equal(majorOnly.defaultKind, "major");
  assert.deepEqual(prereleaseBaseMenu("9999.9999.99", { hasAdded: false }), {
    options: [],
    defaultKind: null,
  });
});

test("commits are counted from the last stable tag, whatever the target", () => {
  assert.equal(commitBase({ tagListOutput: "v14.03\nv14.02", describeTag: "v14.03" }), "v14.03");
  // A pre-release tag never becomes the base: its notes cover everything since
  // the last stable release.
  assert.equal(
    commitBase({ tagListOutput: "v14.03\nv14.4.0-rc.1\nv14.4.0-rc.2", describeTag: "v14.03" }),
    "v14.03",
  );
  assert.equal(
    commitBase({ tagListOutput: "v14.4.0-rc.1\nv14.4.0\nv14.5.0-rc.2\nv14.03", describeTag: "v14.03" }),
    "v14.4.0",
  );
  assert.equal(commitBase({ tagListOutput: "v14.4.0\nv14.10.0\nv14.9.0", describeTag: null }), "v14.10.0");
  assert.equal(commitBase({ tagListOutput: "", describeTag: null }), null);
  assert.equal(commitBase({ tagListOutput: null, describeTag: "v14.03" }), "v14.03");
  // The "latest release tag" display still sees pre-releases.
  assert.equal(latestSemverTag("v14.4.0\nv14.5.0-rc.2"), "v14.5.0-rc.2");
  assert.equal(latestSemverTag("v14.4.0\nv14.5.0-rc.2", { stableOnly: true }), "v14.4.0");
});

test("every menu suggestion sorts above the current version", () => {
  for (const current of ["14.3.0", "1.4.0-alpha.1", "14.4.0-beta.4", "14.4.0-rc.1"]) {
    const versions = isPrerelease(current)
      ? prereleaseMenu(current).options.map((o) => o.version).filter(Boolean)
      : stableMenu(current).map((o) => o.version).filter(Boolean);
    for (const v of versions) assert.ok(compareVersions(v, current) > 0, `${v} > ${current}`);
  }
});

test("override candidates: format, tags, and the not-greater confirm", () => {
  assert.equal(normalizeCandidate("", "14.4.0"), "14.4.0");
  assert.equal(normalizeCandidate("v14.5.0", "14.4.0"), "14.5.0");
  const base = { current: "14.3.0", localTagExists: false, remoteTagExists: null };
  assert.deepEqual(assessCandidate({ ...base, candidate: "14.4.0" }), {
    problem: null,
    confirm: null,
  });
  assert.match(assessCandidate({ ...base, candidate: "14.4" }).problem, /not a valid/);
  assert.match(
    assessCandidate({ ...base, candidate: "14.10.0-beta.1" }).problem,
    /13-character limit/,
  );
  assert.match(
    assessCandidate({ ...base, candidate: "14.4.0", localTagExists: true }).problem,
    /already exists locally/,
  );
  assert.match(
    assessCandidate({ ...base, candidate: "14.4.0", remoteTagExists: true }).problem,
    /already exists on origin/,
  );
  assert.equal(
    assessCandidate({ ...base, candidate: "14.4.0", remoteTagExists: false }).problem,
    null,
  );
  const same = assessCandidate({ ...base, candidate: "14.3.0" });
  assert.equal(same.problem, null);
  assert.match(same.confirm, /not greater/);
  // A pre-release of the current version sorts below it.
  assert.match(assessCandidate({ ...base, candidate: "14.3.0-rc.1" }).confirm, /not greater/);
});

test("latest release tag ignores the upstream tags and ranks pre-releases below releases", () => {
  assert.equal(latestSemverTag("v14.03\nv14.02\nv13.03\n"), null);
  assert.equal(latestSemverTag(""), null);
  assert.equal(latestSemverTag(null), null);
  assert.equal(
    latestSemverTag("v14.03\nv14.4.0-rc.1\nv14.4.0\nv14.4.0-rc.2\nv14.10.0-alpha.1"),
    "v14.10.0-alpha.1",
  );
  assert.equal(latestSemverTag("v14.4.0-rc.1\nv14.4.0\nv14.4.0-rc.2"), "v14.4.0");
});

test("the release path list is one constant shared by prepare and commit", () => {
  assert.equal(STAMPED_FILES.length, 9);
  assert.equal(STAMPED_FILES[0], HEADER_PATH);
  assert.deepEqual(releasePaths(true), STAMPED_FILES);
  assert.deepEqual(releasePaths(false), [...STAMPED_FILES, "CHANGELOG.md", "changelog.d"]);
});

// --- header -----------------------------------------------------------------

const realHeader = () => readFileSync(join(repoRoot, HEADER_PATH), "latin1");

// The checkout decides the header's line endings (LF in git and on CI's Ubuntu
// runner, CRLF on Windows checkouts), so each header test runs on both styles,
// built from the real file.
const HEADER_EOLS = { LF: "\n", CRLF: "\r\n" };
const headerWith = (eol) => realHeader().replace(/\r?\n/g, eol);
const crlfCount = (s) => (s.match(/\r\n/g) || []).length;
const loneLfCount = (s) => (s.match(/(?<!\r)\n/g) || []).length;

// A 13-character version no real release uses, so all five lines change
// whatever the header holds today.
const PROBE = "99.98.97-rc.1";

for (const [name, eol] of Object.entries(HEADER_EOLS)) {
  test(`header round trip keeps ${name} and every other byte`, () => {
    const original = headerWith(eol);
    const h = parseHeader(original);
    assert.equal(headerProblem(h), null);

    const pre = renderHeader(original, PROBE);
    assert.deepEqual(parseHeader(pre), {
      major: 99,
      minor: 98,
      patch: 97,
      version: PROBE,
      fileVersion: "99.98.97.0",
    });
    assert.equal(headerProblem(parseHeader(pre)), null);
    assert.equal(crlfCount(pre), crlfCount(original));
    assert.equal(loneLfCount(pre), loneLfCount(original));
    // Only the five version lines differ.
    const a = original.split(eol);
    const b = pre.split(eol);
    assert.equal(a.length, b.length);
    const changed = a.flatMap((l, i) => (l === b[i] ? [] : [l]));
    assert.equal(changed.length, 5);
    assert.ok(changed.every((l) => l.startsWith("#define PLUGY_VERSION")));

    assert.equal(renderHeader(pre, h.version), original);
  });

  test(`header writes refuse a bad version and a missing or doubled line (${name})`, () => {
    const original = headerWith(eol);
    assert.throws(() => renderHeader(original, "14.10.0-beta.1"), /13-character limit/);
    const missing = original.replace(/^#define PLUGY_VERSION_PATCH .*\r?\n/m, "");
    assert.notEqual(missing, original);
    assert.throws(() => renderHeader(missing, "14.4.0"), /exactly one patch line, found 0/);
    const doubled = original.replace(/^#define PLUGY_VERSION ".*\r?\n/m, (line) => line + line);
    assert.notEqual(doubled, original);
    assert.throws(() => parseHeader(doubled), /exactly one version line, found 2/);
  });
}

test("headerProblem flags disagreeing lines", () => {
  const ok = { major: 14, minor: 4, patch: 0, version: "14.4.0-rc.1", fileVersion: "14.4.0.0" };
  assert.equal(headerProblem(ok), null);
  assert.match(headerProblem({ ...ok, minor: 3 }), /disagree/);
  assert.match(headerProblem({ ...ok, fileVersion: "14.4.0.1" }), /should be "14.4.0.0"/);
  assert.match(headerProblem({ ...ok, version: "14.4" }), /not a valid version/);
});

// --- banners ----------------------------------------------------------------

function copyBanners() {
  const dir = mkdtempSync(join(tmpdir(), "plugy-banners-"));
  for (const { path } of BANNER_FILES) {
    mkdirSync(dirname(join(dir, path)), { recursive: true });
    copyFileSync(join(repoRoot, path), join(dir, path));
  }
  return dir;
}

const snapshot = (dir) =>
  Object.fromEntries(BANNER_FILES.map(({ path }) => [path, readFileSync(join(dir, path))]));

// Split raw bytes into lines on the encoded "\n", keeping each terminator, so
// a comparison sees every byte (including a lone LF or a missing final CRLF).
function byteLines(buf, encoding) {
  const nl = encoding === "latin1" ? [0x0a] : [0x0a, 0x00];
  const start = encoding === "latin1" ? 0 : 2;
  const lines = [];
  let from = start;
  for (let i = start; i < buf.length; i += nl.length) {
    if (buf[i] === nl[0] && (nl.length === 1 || buf[i + 1] === nl[1])) {
      lines.push(buf.subarray(from, i + nl.length));
      from = i + nl.length;
    }
  }
  lines.push(buf.subarray(from));
  return lines;
}

test("stamping changes only the token and right padding of the one banner line", () => {
  const dir = copyBanners();
  try {
    const before = snapshot(dir);
    // Every site carries the same token (14.03 until the first release).
    const tokens = readBannerTokens(dir).map((b) => b.token);
    assert.equal(tokens.length, 8);
    assert.ok(tokens[0] !== null && tokens.every((tok) => tok === tokens[0]), JSON.stringify(tokens));
    const plans = planBannerStamp(dir, PROBE);
    assert.equal(plans.length, 8);
    // Planning writes nothing.
    assert.deepEqual(snapshot(dir), before);
    writeBannerPlans(plans);
    const after = snapshot(dir);

    for (const { path, encoding } of BANNER_FILES) {
      const a = before[path];
      const b = after[path];
      assert.equal(b.length, a.length, `${path}: same byte length`);
      if (encoding === "utf16le") {
        assert.deepEqual([...b.subarray(0, 2)], [0xff, 0xfe], `${path}: BOM kept`);
      }
      const la = byteLines(a, encoding);
      const lb = byteLines(b, encoding);
      assert.equal(lb.length, la.length, `${path}: same line count`);
      const diff = la.flatMap((l, i) => (l.equals(lb[i]) ? [] : [i]));
      assert.equal(diff.length, 1, `${path}: exactly one line differs`);
      const decode = (x) =>
        encoding === "latin1" ? x.toString("latin1") : x.toString("utf16le");
      const oldLine = decode(la[diff[0]]);
      const newLine = decode(lb[diff[0]]);
      assert.ok(oldLine.endsWith(";\r\n"), `${path}: banner line keeps CRLF`);
      assert.ok(newLine.endsWith(";\r\n"), `${path}: stamped line keeps CRLF`);
      assert.equal(newLine.length, oldLine.length, `${path}: line length kept`);
      const re = /^;( +)(version|版本) (\S+)( +);\r\n$/;
      const [, leftA, labelA, tokenA, rightA] = oldLine.match(re);
      const [, leftB, labelB, tokenB, rightB] = newLine.match(re);
      assert.equal(leftB, leftA, `${path}: left padding kept`);
      assert.equal(labelB, labelA);
      assert.equal(tokenA, tokens[0]);
      assert.equal(tokenB, PROBE);
      assert.equal(rightB.length, rightA.length - (tokenB.length - tokenA.length));
    }
    assert.ok(
      decodeLabel(after, "game-files/PlugY_The_Survival_Kit_-_讀我檔案.txt").includes(`版本 ${PROBE}`),
    );

    // Restamping (the stable run after an rc) equals stamping the originals directly.
    writeBannerPlans(planBannerStamp(dir, "99.98.97"));
    const fresh = copyBanners();
    try {
      writeBannerPlans(planBannerStamp(fresh, "99.98.97"));
      assert.deepEqual(snapshot(dir), snapshot(fresh));
    } finally {
      rmSync(fresh, { recursive: true, force: true });
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

function decodeLabel(snap, path) {
  return snap[path].subarray(2).toString("utf16le");
}

test("stamping fails on 0 or 2 banner lines and writes nothing", () => {
  const last = BANNER_FILES[BANNER_FILES.length - 1];
  const first = BANNER_FILES[0];
  for (const [name, mutate] of [
    ["0 matches", (t) => t.replace(/^(;( +)(?:version|版本)) /m, "$1  ")],
    [
      "2 matches",
      (t) => {
        const line = t.match(/^;( +)(version|版本) (\S+)( +);$/m)[0];
        return t.replace(line, `${line}\r\n${line}`);
      },
    ],
  ]) {
    for (const target of [first, last]) {
      const dir = copyBanners();
      try {
        const abs = join(dir, target.path);
        const raw = readFileSync(abs);
        const text =
          target.encoding === "latin1"
            ? raw.toString("latin1")
            : raw.subarray(2).toString("utf16le");
        const mutated = mutate(text);
        assert.notEqual(mutated, text);
        writeFileSync(
          abs,
          target.encoding === "latin1"
            ? Buffer.from(mutated, "latin1")
            : Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(mutated, "utf16le")]),
        );
        const before = snapshot(dir);
        assert.throws(
          () => planBannerStamp(dir, "14.4.0"),
          new RegExp(`no file written[\\s\\S]*${name === "0 matches" ? "found 0" : "found 2"}`),
          `${name} in ${target.path}`,
        );
        assert.deepEqual(snapshot(dir), before, `${name}: nothing written`);
      } finally {
        rmSync(dir, { recursive: true, force: true });
      }
    }
  }
});

test("stamping refuses an invalid or over-long version", () => {
  const dir = copyBanners();
  try {
    const before = snapshot(dir);
    assert.throws(() => planBannerStamp(dir, "14.10.0-beta.1"), /13-character limit/);
    assert.throws(() => planBannerStamp(dir, "v14.4.0"), /not a valid version/);
    assert.deepEqual(snapshot(dir), before);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a UTF-16 banner file without its BOM is refused", () => {
  const dir = copyBanners();
  try {
    const target = BANNER_FILES.find((f) => f.encoding === "utf16le");
    const abs = join(dir, target.path);
    writeFileSync(abs, readFileSync(abs).subarray(2));
    const before = snapshot(dir);
    assert.throws(() => planBannerStamp(dir, "14.4.0"), /FF FE BOM/);
    assert.deepEqual(snapshot(dir), before);
    // The read side reports it instead of throwing, so release.mjs can route it through fatal().
    const lines = readBannerLines(dir);
    const bad = lines.find((l) => l.path === target.path);
    assert.equal(bad.token, null);
    assert.match(bad.error, /FF FE BOM/);
    assert.equal(lines.filter((l) => l.token !== null).length, 7);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a missing banner file is reported, not thrown", () => {
  const dir = copyBanners();
  try {
    rmSync(join(dir, BANNER_FILES[2].path));
    const bad = readBannerLines(dir)[2];
    assert.equal(bad.token, null);
    assert.match(bad.error, /ENOENT/);
    assert.throws(() => planBannerStamp(dir, "14.4.0"), /no file written[\s\S]*ENOENT/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// root ignores a read-only mode on POSIX, so the failure can't be provoked there.
test("a write failing midway names the failed file and the ones already stamped", {
  skip: process.getuid?.() === 0 ? "running as root" : false,
}, () => {
  const dir = copyBanners();
  const target = join(dir, BANNER_FILES[2].path);
  try {
    const plans = planBannerStamp(dir, PROBE);
    chmodSync(target, 0o444); // read-only: the write must fail
    let error;
    try {
      writeBannerPlans(plans);
    } catch (err) {
      error = err;
    }
    assert.ok(error, "writeBannerPlans must throw");
    assert.match(error.message, new RegExp(`could not write ${BANNER_FILES[2].path.replace(/[.]/g, "\\.")}`));
    assert.match(error.message, /already stamped: game-files\/PlugY\.ini, game-files\/PlugY\/PlugYDefault\.ini$/);
    assert.deepEqual(error.written, [BANNER_FILES[0].path, BANNER_FILES[1].path]);
  } finally {
    chmodSync(target, 0o666);
    rmSync(dir, { recursive: true, force: true });
  }
});

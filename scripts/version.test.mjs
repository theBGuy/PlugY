// version.mjs CLI: the exact exit codes and output the release workflow parses.
// Each case runs the real script from a throwaway tree holding a copy of the
// scripts and a header rendered for that case; no git involved.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
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

import { HEADER_PATH, renderHeader } from "./version-lib.mjs";

const scriptsDir = dirname(fileURLToPath(import.meta.url));
const realHeader = readFileSync(join(scriptsDir, "..", HEADER_PATH), "latin1");

/** Run version.mjs against a header for `version` (or raw `headerText`). */
function runVersion(args, { version, headerText } = {}) {
  const dir = mkdtempSync(join(tmpdir(), "plugy-version-"));
  try {
    mkdirSync(join(dir, "scripts"));
    mkdirSync(join(dir, dirname(HEADER_PATH)), { recursive: true });
    for (const f of ["version.mjs", "version-lib.mjs"]) {
      copyFileSync(join(scriptsDir, f), join(dir, "scripts", f));
    }
    writeFileSync(
      join(dir, HEADER_PATH),
      headerText ?? renderHeader(realHeader, version),
      "latin1",
    );
    return spawnSync(process.execPath, [join(dir, "scripts", "version.mjs"), ...args], {
      encoding: "utf8",
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test("no arguments prints PLUGY_VERSION", () => {
  const r = runVersion([], { version: "14.4.0-rc.1" });
  assert.equal(r.status, 0);
  assert.equal(r.stdout, "14.4.0-rc.1\n");
});

test("--check-tag on a stable match prints exactly the two output lines", () => {
  const r = runVersion(["--check-tag", "v14.4.0"], { version: "14.4.0" });
  assert.equal(r.status, 0);
  assert.equal(r.stdout, "version=14.4.0\nprerelease=false\n");
  assert.equal(r.stderr, "");
});

test("--check-tag on a pre-release match reports prerelease=true", () => {
  const r = runVersion(["--check-tag", "v14.4.0-rc.2"], { version: "14.4.0-rc.2" });
  assert.equal(r.status, 0);
  assert.equal(r.stdout, "version=14.4.0-rc.2\nprerelease=true\n");
});

test("--check-tag refuses a mismatch, a missing v, and a missing tag with nothing on stdout", () => {
  for (const args of [["--check-tag", "v14.4.0"], ["--check-tag", "14.3.0"], ["--check-tag"]]) {
    const r = runVersion(args, { version: "14.3.0" });
    assert.equal(r.status, 1, args.join(" "));
    assert.equal(r.stdout, "", args.join(" "));
    assert.match(r.stderr, /^error: /, args.join(" "));
  }
  const pre = runVersion(["--check-tag", "v14.4.0"], { version: "14.4.0-rc.1" });
  assert.equal(pre.status, 1);
  assert.match(pre.stderr, /expected v14\.4\.0-rc\.1/);
});

test("--check-tag refuses a header whose five lines disagree", () => {
  const skewed = renderHeader(realHeader, "14.4.0").replace(
    /^(#define PLUGY_VERSION_MINOR )\d+/m,
    (_m, pre) => `${pre}9`,
  );
  const r = runVersion(["--check-tag", "v14.4.0"], { headerText: skewed });
  assert.equal(r.status, 1);
  assert.equal(r.stdout, "");
  assert.match(r.stderr, /disagree/);
});

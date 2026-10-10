#!/usr/bin/env node
// Reads PlugY's version from src/common/PlugYVersion.h. Usage:
//   node scripts/version.mjs                    prints PLUGY_VERSION
//   node scripts/version.mjs --check-tag <tag>  exits 0 only when <tag> is
//       v<PLUGY_VERSION>, the version is valid semver, and the header's five
//       lines agree; prints `version=<V>` and `prerelease=<true|false>`
//       (GitHub-output format), otherwise exits 1 with the reason on stderr.
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  headerProblem,
  isPrerelease,
  parseHeader,
  readHeaderFile,
} from "./version-lib.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

const fail = (msg) => {
  process.stderr.write(`error: ${msg}\n`);
  process.exit(1);
};

let header;
try {
  header = parseHeader(readHeaderFile(root));
} catch (err) {
  fail(err.message);
}

const checkIdx = process.argv.indexOf("--check-tag");
if (checkIdx === -1) {
  process.stdout.write(`${header.version}\n`);
  process.exit(0);
}

const tag = process.argv[checkIdx + 1];
if (!tag) fail("usage: node scripts/version.mjs --check-tag <tag>");
const problem = headerProblem(header);
if (problem) fail(`src/common/PlugYVersion.h: ${problem}`);
if (tag !== `v${header.version}`) {
  fail(
    `tag '${tag}' does not match src/common/PlugYVersion.h (expected v${header.version})`,
  );
}
process.stdout.write(
  `version=${header.version}\nprerelease=${isPrerelease(header.version)}\n`,
);

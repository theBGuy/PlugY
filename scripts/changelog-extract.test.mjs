// changelog-extract: release bodies for stable and pre-release tags, the
// pointer fallbacks, and the size cap. buildReleaseNotes is pure, so no file
// or git state is involved.
import assert from "node:assert/strict";
import { test } from "node:test";

import { buildReleaseNotes, MAX_BYTES, unwrapMarkdown } from "./changelog-extract.mjs";
import { PRERELEASE_LOGS } from "./changelog-lib.mjs";

const intro = (line) => `${line} ${PRERELEASE_LOGS}`;

const empty = { Added: [], Changed: [], Fixed: [] };

const CHANGELOG = [
  "# Changelog",
  "",
  "## [Unreleased]",
  "",
  "### Fixed",
  "",
  "- Hand-written fix.",
  "",
  "## [14.4.0] - 2026-10-08",
  "",
  "### Added",
  "",
  "- Released feature.",
  "",
  "## [14.3.0] - 2021-09-01",
  "",
  "Upstream baseline.",
  "",
  "[Unreleased]: https://github.com/theBGuy/PlugY4Kolbot/compare/v14.4.0...HEAD",
  "",
].join("\n");

test("a stable tag prints its section body", () => {
  const out = buildReleaseNotes({ tag: "v14.4.0", changelogText: CHANGELOG, groups: null });
  assert.equal(out, "### Added\n\n- Released feature.\n");
  assert.equal(
    buildReleaseNotes({ tag: "v14.3.0", changelogText: CHANGELOG.replace(/\n/g, "\r\n"), groups: null }),
    "Upstream baseline.\n",
  );
});

test("a pre-release tag prints the intro plus Unreleased merged with fragments", () => {
  const out = buildReleaseNotes({
    tag: "v14.5.0-rc.1",
    changelogText: CHANGELOG,
    groups: { Added: ["- Pending feature."], Changed: [], Fixed: ["- Pending fix."] },
  });
  assert.equal(
    out,
    [
      intro("Pre-release of 14.5.0 for testing; these notes cover everything since 14.4.0."),
      "",
      "### Added",
      "",
      "- Pending feature.",
      "",
      "### Fixed",
      "",
      "- Hand-written fix.",
      "- Pending fix.",
      "",
    ].join("\n"),
  );
});

test("a pre-release with an empty Unreleased body renders the fragments alone", () => {
  const text = CHANGELOG.replace("### Fixed\n\n- Hand-written fix.\n\n", "");
  const out = buildReleaseNotes({
    tag: "v14.5.0-beta.2",
    changelogText: text,
    groups: { Added: [], Changed: ["- Pending change,\n  wrapped."], Fixed: [] },
  });
  assert.equal(
    out,
    intro("Pre-release of 14.5.0 for testing; these notes cover everything since 14.4.0.") +
      "\n\n### Changed\n\n- Pending change, wrapped.\n",
  );
});

test("the pre-release intro counts from the newest stable section", () => {
  const groups = { Added: [], Changed: [], Fixed: ["- A fix."] };
  // A pre-release section (only packaged files have one) and a non-semver heading are skipped.
  const withRc = CHANGELOG.replace(
    "## [14.4.0] - 2026-10-08",
    "## [14.5.0-rc.1] - 2026-10-09\n\n## [Old stuff]\n\n## [14.4.0] - 2026-10-08",
  );
  assert.match(
    buildReleaseNotes({ tag: "v14.5.0-rc.2", changelogText: withRc, groups }),
    /^Pre-release of 14\.5\.0 for testing; these notes cover everything since 14\.4\.0\. Its installer /,
  );
  const noStable = "# Changelog\n\n## [Unreleased]\n";
  assert.equal(
    buildReleaseNotes({ tag: "v1.0.0-rc.1", changelogText: noStable, groups }),
    intro("Pre-release of 1.0.0 for testing; these notes cover everything since the last stable release.") +
      "\n\n### Fixed\n\n- A fix.\n",
  );
  assert.equal(
    buildReleaseNotes({ tag: "v1.0.0-rc.1", changelogText: noStable, groups: empty }),
    intro("Pre-release of 1.0.0 for testing; no changes are recorded since the last stable release.") + "\n",
  );
});

test("release bodies put each wrapped bullet and paragraph on one line", () => {
  const text = CHANGELOG.replace(
    "- Released feature.",
    "- Released feature,\n  wrapped once\n  and twice.\n- Second.\n\nA paragraph\nwrapped.",
  );
  assert.equal(
    buildReleaseNotes({ tag: "v14.4.0", changelogText: text, groups: null }),
    "### Added\n\n- Released feature, wrapped once and twice.\n- Second.\n\nA paragraph wrapped.\n",
  );
});

test("unwrapping joins only paragraph and list-item continuations", () => {
  const F = "```";
  // Joining any line of these changes what GitHub renders, so each comes back as it was.
  const unchanged = [
    `- ${F}\n  code a\n  code b\n  ${F}\n\nafter\nwrap`, // a fence on a list item stops all joining
    `> ${F}\nx\ny`,
    `a\n    ${F}\nb\nc\n\n${F}\ncode1\ncode2\n${F}`,
    `${F}\nexample:\n    ${F}\nline a\nline b\n${F}`,
    "# H\n    code1\n    code2", // indented code right after a heading or a rule
    "***\n    code1\n    code2",
    `${F}\nx\n${F}\n    code1\n    code2`, // a closed fence still ends all joining
    "-     code\nnext", // indented code inside a list item or quote
    ">     code\nnext",
    "Intro text\na | b\n--|--\n1 | 2", // a table header without a leading pipe
    "> a | b\n> --- | ---\n> c | d\ne", // a table in a quote doesn't continue lazily
    "[a]:\n/url\n'title'\nnext", // a link definition over several lines
    "[foo\nbar]: /url\n'title'\nnext\n\n[foo bar]",
    "see [x](<a\nb.md>)", // a <...> link destination can't span a line break
    "[a]( <b\nc>)",
    "[a\\]b]: /u\nfoo\n\n[a\\]b]", // a link label with an escaped bracket
    "a\n:-\nb", // a one-column table needs no pipe
    "foo <pre>a\nb</pre> c", // inline raw HTML that keeps its line breaks
    "foo <code>a\nb\nc</code> d",
    "$$\nx % c\ny\n$$", // a math block's TeX comment would swallow the next line
    `| a |\n| - |\n| b |\n${String.fromCharCode(0xa0)}\nfoo\nbar`, // a no-break space doesn't end a table
    "Title\n=",
    "Title\n--",
    "more words\n10. ten\n   wrapped\n=", // a setext heading over several lines keeps its anchor
    "- item\n    ===", // a list item moves the indent its underlines and rules may have
    "- item\n    ***",
    "> para\nwrap\n> ===",
    "<pre>\nline1\nline2\n</pre>",
    '[foo]: /url\n"title"\nnext text',
    "> # heading\nlazy",
    "-\nfoo",
    "1.\nfoo",
    "text\n# Heading",
  ];
  for (const input of unchanged) assert.equal(unwrapMarkdown(input), input, JSON.stringify(input));
  // A GitHub alert's marker must stand alone on its line.
  assert.equal(unwrapMarkdown("> [!NOTE]\nlazy"), "> [!NOTE]\nlazy");
  assert.equal(
    unwrapMarkdown(`${F}x${F} text\nwrapped\n\n${F}\ncode1\ncode2\n${F}`),
    `${F}x${F} text wrapped\n\n${F}\ncode1\ncode2\n${F}`,
  );
  const cases = [
    // [input, expected]
    ["### Heading\ntext\nmore", "### Heading\ntext more"],
    ["- a\n- b\n* c\n+ d", "- a\n- b\n* c\n+ d"],
    // Only column-0 paragraphs and list items are joined; nested items and quotes keep their lines.
    ["- a\n  - nested\n    more", "- a\n  - nested\n    more"],
    ["1. one\n   cont\n2) two", "1. one cont\n2) two"],
    ["> quote\n> more\nlazy", "> quote\n> more\nlazy"],
    // Only spaces and tabs are trimmed; other spaces are content.
    [`foo\n${String.fromCharCode(0x3000)}bar`, `foo ${String.fromCharCode(0x3000)}bar`],
    [`foo${String.fromCharCode(0xa0)}\nbar`, `foo${String.fromCharCode(0xa0)} bar`],
    ["text\n```js\nline one\nline two\n```\nafter", "text\n```js\nline one\nline two\n```\nafter"],
    ["~~~~\na\nb\n~~~\nc\n~~~~", "~~~~\na\nb\n~~~\nc\n~~~~"],
    ["- item\n    ```\n    a\n    b\n    ```", "- item\n    ```\n    a\n    b\n    ```"],
    ["| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |", "| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |"],
    ["a | b\n--- | ---\n1 | 2\n3 | 4\n\nafter\nwrap", "a | b\n--- | ---\n1 | 2\n3 | 4\n\nafter wrap"],
    ["Title\n===\ntext\n\n---\n\nmore", "Title\n===\ntext\n\n---\n\nmore"],
    ["Sub\n---", "Sub\n---"],
    ["two spaces  \nnext\nwrap", "two spaces  \nnext wrap"],
    ["backslash\\\nnext", "backslash\\\nnext"],
    ["para\n\n    code one\n    code two\n\nend", "para\n\n    code one\n    code two\n\nend"],
    ["para\n    indented continuation", "para indented continuation"],
    ["[x]: https://example.com\n[y]: https://example.org", "[x]: https://example.com\n[y]: https://example.org"],
    ["<details>\n<summary>x</summary>\ntext", "<details>\n<summary>x</summary>\ntext"],
    // The earlier line keeps its trailing space; a code span crossing the break keeps the break.
    ["trailing space \nnext", "trailing space  next"],
    ["foo `a \nb` bar\nmore", "foo `a \nb` bar more"],
    // An inline raw tag left open at a paragraph's end stops joining for the rest of the body.
    ["foo <pre>a\n\nb\nc", "foo <pre>a\n\nb\nc"],
    ["", ""],
  ];
  for (const [input, expected] of cases) assert.equal(unwrapMarkdown(input), expected, JSON.stringify(input));
});

test("nothing found falls back to a pointer line", () => {
  assert.equal(
    buildReleaseNotes({ tag: "v9.9.9", changelogText: CHANGELOG, groups: null }),
    "See the CHANGELOG for v9.9.9.\n",
  );
  assert.equal(
    buildReleaseNotes({ tag: "", changelogText: CHANGELOG, groups: null }),
    "See the CHANGELOG for this release.\n",
  );
  assert.equal(
    buildReleaseNotes({ tag: "v14.4.0", changelogText: null, groups: null }),
    "See CHANGELOG.md for release notes.\n",
  );
  const bare = CHANGELOG.replace("### Fixed\n\n- Hand-written fix.\n\n", "");
  // A pre-release with nothing pending prints the intro alone: the repository's
  // CHANGELOG.md never gets a section for it that a pointer could name.
  const nothingPending = intro("Pre-release of 14.5.0 for testing; no changes are recorded since 14.4.0.") + "\n";
  assert.equal(buildReleaseNotes({ tag: "v14.5.0-rc.1", changelogText: bare, groups: empty }), nothingPending);
  assert.equal(buildReleaseNotes({ tag: "v14.5.0-rc.1", changelogText: bare, groups: null }), nothingPending);
  // Bare category headings with no bullets are not notes either.
  const headingsOnly = bare.replace("## [Unreleased]\n\n", "## [Unreleased]\n\n### Added\n\n### Fixed\n\n");
  assert.notEqual(headingsOnly, bare);
  assert.equal(
    buildReleaseNotes({ tag: "v14.5.0-rc.1", changelogText: headingsOnly, groups: empty }),
    nothingPending,
  );
  // ...while a single bullet under them is.
  assert.match(
    buildReleaseNotes({ tag: "v14.5.0-rc.1", changelogText: headingsOnly, groups: { ...empty, Fixed: ["- One fix."] } }),
    /cover everything since 14\.4\.0\. Its installer [^\n]*\n\n### Added\n\n### Fixed\n\n- One fix\.\n$/,
  );
});

test("oversized notes keep whole lines under the cap and link the full changelog", () => {
  const bullets = Array.from({ length: 4000 }, (_, i) => `- Entry number ${i} with some padding text.`);
  const text = CHANGELOG.replace("- Released feature.", bullets.join("\n"));
  const out = buildReleaseNotes({
    tag: "v14.4.0",
    changelogText: text,
    groups: null,
    env: { GITHUB_REPOSITORY: "theBGuy/PlugY4Kolbot" },
  });
  assert.ok(Buffer.byteLength(out, "utf8") <= MAX_BYTES);
  assert.match(out, /truncated; see the \[full changelog\]\(https:\/\/github\.com\/theBGuy\/PlugY4Kolbot\/blob\/v14\.4\.0\/CHANGELOG\.md\)/);
  const kept = out.slice(0, out.indexOf("\n\n---"));
  assert.ok(kept.split("\n").every((l) => l === "" || l === "### Added" || /^- Entry number \d+ with some padding text\.$/.test(l)));
});

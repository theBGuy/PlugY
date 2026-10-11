# Changelog fragments

Instead of editing `## [Unreleased]` in [`../CHANGELOG.md`](../CHANGELOG.md)
directly, where every branch collides, each user-facing change adds its **own
file** here. One file per change means parallel branches never conflict on the
changelog. At a stable release, `node scripts/changelog-release.mjs <version>`
assembles these fragments into `CHANGELOG.md`, in the exact
[Keep a Changelog](https://keepachangelog.com/) format, and deletes them. A
pre-release (`-alpha.N`, `-beta.N`, `-rc.N`) leaves them in place: its release
notes are rendered from them, and the stable release consumes them later.

**Player-facing changes only.** The release notes on GitHub come from this
changelog, so write for players and bot operators. Build scripts, CI, and repo
tooling get **no fragment**.

CI keys on paths, not user impact: the PR check fires on any change under
`src/`, `game-files/` or `installer/`, so a change there that genuinely needs no
fragment (an internal refactor, say) takes the `no-changelog` label or
`skip-changelog` in the PR title.

## Format

Create a file named **`<category>-<slug>.md`**, where:

- `<category>` is one of `added`, `changed`, or `fixed` (the Keep a Changelog
  groups this project uses: `feat` → `added`, `fix` → `fixed`, refactors/perf →
  `changed`). A pending `added-*` fragment makes the release driver suggest a
  minor version.
- `<slug>` is a short kebab-case topic. Keep it distinctive; append the PR number
  if you want to be certain it's unique (e.g. `added-shared-stash-locking-42.md`).

The **file body is the finished changelog bullet, verbatim**, including the
leading `- ` and the 2-space indent on wrapped continuation lines. Write it *for
humans*: a clear sentence about what changed for the player, not your commit
subject. State what the change gives the reader, not the defect it removes. The
assembler concatenates bodies under the right `###` heading without re-wrapping,
so what you write is exactly what ships.

Use at most one em dash per bullet; two read as a paired aside and fail
`--check`. Em dashes inside backticks, **bold**, or "quotes" don't count, so
quoted game text can keep its own.

### Example: `added-show-error-message-box.md`

```md
- **`ShowErrorMessageBox` option** in the `[GENERAL]` section of `PlugY.ini`, for
  unattended bots. Set it to 0 and the errors `PlugY.dll` raises in the game are
  reported without a message box waiting for a click.
```

### Example: `fixed-autosave-stash-files.md`

```md
- In single-player games on Diablo II 1.14d, the PlugY stash (`.d2x`) and the
  shared stash are saved with the character every time the game saves it.
```

## Commands

- `node scripts/changelog-draft.mjs --preview` shows the pending fragments
  assembled under an Unreleased heading (nothing is written).
- `node scripts/changelog-draft.mjs --check` validates every pending fragment
  (filename + bullet format) and reports all problems; the PR workflow runs
  this, so a malformed fragment fails the PR instead of the release.
- `node scripts/changelog-draft.mjs` drafts starting-point bullets from the git
  history since the last release tag (curate before saving as a fragment).
- `node scripts/release.mjs` (maintainers) is the interactive release driver:
  it checks the repo state, previews the fragments, picks the version (patch,
  minor, major or a pre-release), runs `changelog-release.mjs`, then commits,
  creates the annotated tag, and pushes master and the tag together. Rehearse
  with `--dry-run`; `--help` lists the options.
- `node scripts/changelog-release.mjs <version>` (maintainers) stamps the version
  into `src/common/PlugYVersion.h` and the version banners of the INIs,
  `LocalizedStrings.ini` and readmes in `game-files/`; for a stable version it
  also assembles the fragments into `CHANGELOG.md` as `## [<version>] - <date>`
  and deletes them.
- `node scripts/changelog-extract.mjs <tag>` prints the release notes for a tag
  (the release workflow uses it for the GitHub release body).

`README.md` in this directory is ignored by the tooling and never consumed.

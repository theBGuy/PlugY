# PlugY4Kolbot
theBGuy's fork of "PlugY, The Survival Kit" by Yohann Nicolas, the plug-in for Diablo II Lord of Destruction, hardened for running many kolbot/D2Bot games at once. This fork is developed and tested on Diablo II 1.14d; upstream PlugY also supports 1.09b - 1.13d, which this fork doesn't test.

> The upstream repository is [ChaosMarc/PlugY](https://github.com/ChaosMarc/PlugY). Ready-to-use builds of this fork, with checksums and build attestations, are on the [Releases page](https://github.com/theBGuy/PlugY4Kolbot/releases). File names are unchanged (`PlugY.dll`, `PlugY.exe`, `PlugY.ini` and the `PlugY\` folder), so it is a drop-in upgrade for an existing PlugY install.

Extends Diablo II - Lord of Destruction by a number of useful features

## FEATURES
Each features can be turned on/off via PlugY.ini (see "COMMENTS ON THE CONFIGURATION FILE")
* Disable access to Battle.net.
* Infinite storage space in the stash.
* Shared storage space in the stash.
* Enable the ladder only runewords for singleplayer and TCP/IP game.
* World Event and Uber Quest for singleplayer and TCP/IP game.
* Can open Cow Level Portal even when player have kill the Cow King in that difficulty.
* Prevent Nihlathak's portal from closing.
* Move Deckard Cain near the waypoint in Harrogath.
* Unassign assigned skills and stats points.
* Auto backup save files before saving.
* Change the selected language.
* Always regenerate maps in SinglePlayer like in MultiPlayer.
* Automatically execute /players X when you launch a new game.
* Added some pages for display more characters stats.
* Display item level in its popup.
* Launch any number of Diablo II games on the same computer.
* Increase the stash to 10x10 squares.
* Stash requests for bots and scripts (1.14d): show any page, see what every page holds, and move items between pages without leaving the page shown (see [docs/stash-protocol.md](docs/stash-protocol.md)).
* Pinned items (1.14d), the Horadric Cube by default, stay on their page when you swap stash pages.
* Change the save path directory.
* Always display Mana and Life values above the globes.
* D2 can load all files, even those opened with Microsoft Excel (disabled by default).
* Display the current stats value (without magical bonus).
* Can launch game in windowed mode.
* Add following commands (see "COMMENTS ON THE CONFIGURATION FILE") :
	* /lockmouse : Lock mouse cursor in the window.
	* /lock : Same as /lockmouse.
	* /unlockmouse : Unlock mouse cursor in the window.
	* /unlock : Same as /unlockmouse.
	* /renamepage name : Rename current page stash (use # for page number).
	* /rp name : See /renamepage.
	* /setindex : Set current stash page as index.
	* /setmainindex : Set current stash page as main index.
	* /resetindex : Remove index flag on the current stash page.
	* /insertpage : Insert a new page after the current page.
	* /ip : See /insertpage.
	* /deletepage : Delete current page if empty.
	* /dp : See /deletepage.
	* /swappage page : Swap the content of current stash page with the content of another page.
	* /sp page : See /swappage.
	* /togglepage page : Swap the content of current stash page with the content of another page in opposing stash shared/personal.
	* /tp page : See /togglepage.
	* /dlm : Toggle always display mana and life mode.
	* /dml : See /dlm.
	* /dl : See /dlm.
	* /dm : See /dlm.
	* /page 1 : Show normal stats page (stats page must be opened).
	* /page 2 : Show extra stats page (stats page must be opened).
	* /page 3 : Show resistance stats page (stats page must be opened).
	* /page 4 : Show player breakpoints (stats page must be opened).
	* /page 5 : Show mercenaries breakpoints (stats page must be opened).
	* /page 6 : (beta) Show available runewords (stats page must be opened).
	* /save : (beta) Save game without exit.
	* /listcube : (beta) Create a "cube.txt" file in save directory containing all cube's receipts.
	* /renamechar newname : (beta) Rename your character and save it.
	
## Installing
* **Installer:** choose the Diablo II folder (the one that holds `Game.exe`); any copy of Diablo II works. For D2Bot / kolbot, tick **Patch Game.exe (D2Bot / kolbot)**: `Game.exe` then starts PlugY by itself, with PlugY's files next to it. Each install gets its own entry in Apps & features. Uninstalling the install that patched `Game.exe` restores it, and leaves your other PlugY4Kolbot installs alone. The desktop and Start-menu shortcuts start the install that created them last.
* **Silent install:** `PlugY4Kolbot_v<version>_Installer.exe /S /D2="C:\Games\Diablo II" /PATCH` installs without showing its pages, for the Diablo II folder that `/D2=` names (a full path, in quotes). It exits with 2, installing nothing, when `/D2=` is not a full path or the folder holds no `Game.exe`. `/PATCH` ticks **Patch Game.exe** for D2Bot / kolbot, so everything goes beside `Game.exe`; if `Game.exe` can't be patched (for example while it runs), PlugY4Kolbot is still installed and the installer exits with 3. Without `/PATCH`, a new install puts `PlugY.exe` and `PlugY.ini` in `Mod PlugY` for Windows' registered copy of Diablo II, and beside `Game.exe` for any other copy. To update an existing install silently, also give its PlugY folder as `/D=<folder>`, last and without quotes, as the updater does: it is updated where it is, its patch included.
* **Zip:** extract `PlugY4Kolbot_v<version>.zip` into the Diablo II folder. For D2Bot / kolbot, also run `PatchD2File.exe` there (from `PlugY4Kolbot_v<version>_D2FilePatcher.zip`); it asks for administrator rights when the folder needs them, such as under Program Files. Its script options and exit codes are in `PlugY_The_Survival_Kit_-_Readme.txt`, in the main zip.

## Updating
* **In game:** when a newer release is out, the main menu shows `Update: X.Y.Z` in green (with `ActivePrintPlugYVersion=1`, the default). The answer from the last check shows at once, even while a fresh check runs.
* **One click:** double-click `PlugY4KolbotUpdater.exe` in your PlugY folder (the one that holds `PlugY.exe`), or use the Start-menu shortcut "Check for PlugY4Kolbot updates". It updates that install to the newest release, verifies the download against `SHA256SUMS.txt` and GitHub's own SHA-256 digest first, and keeps your `PlugY.ini` (new options take their defaults from `PlugY\PlugYDefault.ini`).
  * An install the installer made, wherever it is: the updater runs the new installer for that install alone, without its pages. The install keeps its `Game.exe` patch and its shortcuts, and Apps & features shows its new version. The installer asks for administrator rights.
  * A zip-style install: the updater replaces the files one by one and backs up each to `PlugY4Kolbot-backup\<old version>-<timestamp>\`. It asks for administrator rights only when the folder needs them, such as under Program Files.
* **D2Bot / kolbot:** close every Diablo II game and `PlugY.exe` that use the folder first. While any of them still uses the files, the updater changes nothing and asks you to close them, then run it again.
* **Two installs in one Diablo II folder** (such as a `Mod PlugY` install and a patched one beside `Game.exe`) share its `PlugY.dll`, so give them the same `UpdateChannel`. The updater never puts an older `PlugY.dll` back: while the other install's channel is ahead, it changes nothing and exits with 1, `--check` included.
* **Scripted use:** `PlugY4KolbotUpdater.exe --check` only reports, and `--yes` installs without asking. `--prerelease` also offers alpha, beta and rc builds. Exit codes: 0 updated or up to date, 1 error, 2 update available (`--check`), 3 files in use, 4 cancelled.

## Privacy
The in-game update check asks api.github.com for the newest release at most once a day per Windows user and update channel (hourly after a failed check), and every running game shares one cached answer per update channel in the `%LOCALAPPDATA%\PlugY4Kolbot` folder. Without a usable cache folder it doesn't check at all. It sends only a `PlugY4Kolbot/<version>` User-Agent, though GitHub still sees your IP address. The game never waits for the check. Two options in the `[GENERAL]` section of `PlugY.ini` control it:
* `CheckForUpdates=1` (default). Set it to 0 and the game never contacts GitHub; the updater still checks when you run it.
* `UpdateChannel=stable` (default). `prerelease` also offers alpha, beta and rc builds.

## Logs
Each launch of the game writes its own log to the `PlugY\logs` folder, named after its start time and process id; when the game folder is read-only, the logs go to `%LOCALAPPDATA%\PlugY4Kolbot\logs` instead. Logs older than 7 days are removed, and at most the newest 500 are kept. `ActiveLogFile=1` (the default for new installs) logs the whole session and `0` only PlugY's startup; an existing `PlugY.ini` keeps its value.

## Contribute
Bug reports and pull requests are welcome in [this repository](https://github.com/theBGuy/PlugY4Kolbot).
* **Bugs and ideas:** open an issue with your game version, your `PlugY.ini` settings, the session log from `PlugY\logs` and the steps that trigger the problem.
* **Translations:** send a pull request that edits `game-files/PlugY/LocalizedStrings.ini`. The file must stay UTF-16LE with its byte-order mark; many editors save as UTF-8 by default, so check the encoding before you commit.
* **Changelog:** give every user-facing change a fragment in `changelog.d/` (see [changelog.d/README.md](changelog.d/README.md)), so it reaches the release notes.

## License
PlugY4Kolbot is free software under the GNU General Public License v3 (see [LICENSE](LICENSE)), provided without any warranty. It is a fork of PlugY, The Survival Kit, created by Yohann Nicolas. This project is not affiliated with or endorsed by Blizzard Entertainment.

## Repository layout
* `src/plugy/`: the plugin, `PlugY.dll`. Its `d2/` folder holds the Diablo II structures and function addresses.
* `src/launcher/`: the `PlugY.exe` launcher.
* `src/patcher/`: `PatchD2File.exe` and `RestoreD2File.exe`, which make `Game.exe` load PlugY and undo that.
* `src/updater/`: `PlugY4KolbotUpdater.exe`.
* `src/common/`: code these programs share, including the version number (`PlugYVersion.h`) and the update check.
* `game-files/`: the files installed beside the programs: `PlugY.ini`, the `PlugY\` folder and the readmes.
* `installer/`: the NSIS installer script.
* `scripts/`: the build, package and release scripts.
* `tests/`: the native tests.
* `docs/`: the stash protocol reference.
* `changelog.d/`: the changelog fragments of the next release.

## Building from source
Prerequisites (Windows):
* Visual Studio 2022 with the "Desktop development with C++" workload, including the x86 build tools.
* NSIS 3.11 or later, for the installer.
* 7-Zip, for the release zips.
* PowerShell 7 (`pwsh`) or Windows PowerShell 5.1, which run the build scripts.

Build the plugin, the launcher, the updater and the D2 file patcher, then package them:
```
pwsh -NoProfile -File scripts/build.ps1 -Config Release
pwsh -NoProfile -File scripts/package.ps1
```
Add `-Test` to the build command to also build and run the native test suites (about 30-40 seconds more); it exits non-zero when a test fails, and CI always runs it.
With only Windows PowerShell 5.1, run the same scripts as `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1` (and likewise for `package.ps1`).
The first command finds Visual Studio on its own and writes `PlugY.dll`, `PlugY.exe`, `PlugY4KolbotUpdater.exe`, `PatchD2File.exe` and `RestoreD2File.exe` to `build/out/Release/`. The second writes the release assets to `dist/`, named after the version in `src/common/PlugYVersion.h`:
* `PlugY4Kolbot_v<version>.zip` (the files for a manual or D2Bot/kolbot install, including `PlugY4KolbotUpdater.exe`)
* `PlugY4Kolbot_v<version>_D2FilePatcher.zip`
* `PlugY4Kolbot_v<version>_Installer.exe`
* `PlugY4Kolbot_v<version>_symbols.zip` (debug symbols)
* `SHA256SUMS.txt`

Every push to `master` and every pull request runs the same build in CI, and the packaged files are attached to the run as an artifact.

## Releasing (maintainers)
1. Give every user-facing change a changelog fragment in `changelog.d/` (see [changelog.d/README.md](changelog.d/README.md)). Pull requests that change code are checked for one; the `no-changelog` label or `skip-changelog` in the PR title exempts changes users never see.
2. Run `node scripts/release.mjs` from an up-to-date `master` (add `--dry-run` to rehearse a release first). It bumps the version and, for a stable release, assembles the fragments into `CHANGELOG.md` (a pre-release leaves them pending for the next stable release). Then it commits, creates the tag `vX.Y.Z` (or a pre-release tag such as `vX.Y.Z-rc.N` / `-beta.N`) and pushes `master` and the tag together.
3. The tag push runs the release workflow, which checks the tag against `src/common/PlugYVersion.h` and that the tagged commit is on `master`, builds every binary and runs the native tests, packages every asset, attests their provenance and creates a **draft** GitHub release. Its notes are the changelog section for a stable release, or the pending fragments for a pre-release.
4. Review the draft and smoke-test its downloads.
5. Publish it: `gh release edit vX.Y.Z --draft=false --latest` for a stable release, or `gh release edit vX.Y.Z-rc.N --draft=false` for a pre-release.

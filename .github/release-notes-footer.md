---

### Installing or upgrading

PlugY4Kolbot keeps PlugY's file names, so it drops into an existing PlugY install. It is developed and tested on Diablo II 1.14d.

- **D2Bot / kolbot and manual installs:** to update an existing install, close every Diablo II game, then run `PlugY4KolbotUpdater.exe` from your PlugY game folder (if the folder doesn't have it yet, copy it there from `PlugY4Kolbot_v*.zip` first). It verifies the download and keeps your `PlugY.ini`, and offers pre-releases only with `--prerelease` or `UpdateChannel=prerelease` in `PlugY.ini`. For an install the installer made, it runs the new installer for that install alone; otherwise it backs up every file it replaces. To update by hand instead, close every game and copy everything from `PlugY4Kolbot_v*.zip` except `PlugY.ini` into your PlugY game folder; new options take their defaults from `PlugY\PlugYDefault.ini`, so your settings carry over.
- **Installer:** `PlugY4Kolbot_v*_Installer.exe` upgrades an existing PlugY install in place and keeps your existing `PlugY.ini`.
- **Saves:** to keep your shared stash safe, point only one PlugY build at a save folder at a time. PlugY4Kolbot locks the shared stash between running games, and an upstream PlugY build running on the same folder at the same time would bypass that protection.

### Verifying your download

- Compare each file's hash with `SHA256SUMS.txt` (in PowerShell: `Get-FileHash <file>`).
- Confirm a file was built by this repository's release workflow: `gh attestation verify <file> -R theBGuy/PlugY4Kolbot`.
- The programs are not code-signed, so Windows SmartScreen or an antivirus may warn about them; the checks above confirm a file is the one this repository released.

### License and source

PlugY4Kolbot is free software under the GNU General Public License v3, provided without any warranty. The complete source for these binaries is the [{{TAG}} source tree](https://github.com/theBGuy/PlugY4Kolbot/tree/{{TAG}}). It is a fork of PlugY, The Survival Kit, created by Yohann Nicolas, and builds on his work. This project is not affiliated with or endorsed by Blizzard Entertainment.

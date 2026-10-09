- **One-click updater.** Double-click `PlugY4KolbotUpdater.exe` next to `PlugY.dll`,
  or the "Check for PlugY4Kolbot updates" Start-menu shortcut after an installer
  install, to update that folder to the newest release. It verifies the download
  against `SHA256SUMS.txt` and GitHub's own SHA-256 digest, keeps your `PlugY.ini`
  (new options take their defaults), backs up every file it replaces to
  `PlugY4Kolbot-backup\` in a zip-style install, and changes nothing while a game
  still uses its files. `--check` and `--yes` let scripts and bots run it
  unattended.

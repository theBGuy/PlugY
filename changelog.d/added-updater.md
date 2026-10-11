- **One-click updater.** Double-click `PlugY4KolbotUpdater.exe` in your PlugY folder,
  or the "Check for PlugY4Kolbot updates" Start-menu shortcut, to update that install
  to the newest release. It verifies the download against `SHA256SUMS.txt` and
  GitHub's own SHA-256 digest, keeps your `PlugY.ini` (new options take their
  defaults), and changes nothing while a game still uses its files. Every install the
  installer made updates on its own: the new installer runs for that install alone,
  without its pages, and keeps its `Game.exe` patch and shortcuts. A zip-style install
  gets each replaced file backed up to `PlugY4Kolbot-backup\`. `--check` and `--yes`
  let scripts and bots run it unattended.

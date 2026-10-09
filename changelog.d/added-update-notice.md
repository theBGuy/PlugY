- **Update notice on the main menu.** When a newer release is out, the main menu
  shows `Update: X.Y.Z` in green (with `ActivePrintPlugYVersion=1`, the default).
  Two new options in the `[GENERAL]` section of `PlugY.ini` control it:
  `CheckForUpdates` (1 by default; set 0 and the game never contacts GitHub) and
  `UpdateChannel` (`stable` by default; `prerelease` also offers alpha, beta and rc
  builds). The check never delays the game and asks api.github.com at most once a
  day per Windows user and update channel, sending only a `PlugY4Kolbot/<version>`
  User-Agent.

- **Per-launch session logs.** Every launch of the game keeps its own log: PlugY
  writes one file per launch in `PlugY\logs`, named after the start time and the
  process, instead of the single `PlugY.log`. The log of a game that crashed is
  still there after a bot manager restarts it, and every game started from the
  same folder logs to its own file. Each non-empty line starts with the time,
  and logs older than 7 days are removed (at most the newest 500 are kept). New
  installs log the whole session (`ActiveLogFile=1`). An existing `PlugY.ini`
  keeps its value, and with upstream PlugY's default, 0, only PlugY's startup is
  logged: set it to 1 for full session logs. When the game folder is read-only,
  the logs go to `%LOCALAPPDATA%\PlugY4Kolbot\logs` instead.

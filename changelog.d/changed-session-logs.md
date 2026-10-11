- Every launch of the game keeps its own log: PlugY writes one file per launch
  in `PlugY\logs`, named after the start time and the process, so the log of
  a game that crashed is still there after a bot manager restarts it, and every
  game started from the same folder logs to its own file. Each non-empty line
  starts with the time, logs older than 7 days are removed (at most the newest
  500 are kept), and logging is on by default (`ActiveLogFile=1`; an existing
  `PlugY.ini` keeps its own value). When the game folder is read-only, the logs
  go to `%LOCALAPPDATA%\PlugY4Kolbot\logs` instead. Uber Quest details are
  logged only in the uber levels, which keeps the log short.

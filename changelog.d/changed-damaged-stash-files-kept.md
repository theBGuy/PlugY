- **Damaged stash files are kept.** In single player, your `.d2x` stash and the
  shared `.sss` stash stay untouched on disk when PlugY can't read them or finds
  them truncated: the character stops at "Unable to enter game", and PlugY names
  the file in a message box (or in the session log when `ShowErrorMessageBox=0`
  and `ActiveLogFile=1`), so you can restore it from a backup or close the
  program that locks it and try again.

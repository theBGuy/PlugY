- On Diablo II 1.14d, `/save` also saves the PlugY stash (`.d2x`) and the shared
  stash along with the character. With `ActiveAutoBackup=1`, a `/save` makes a
  backup only when the game has none yet, so frequent saves keep your older
  backups, and `MaxBackupPerCharacter` counts and removes only that character's
  own backup folders.

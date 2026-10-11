- **Stashes save with the character.** In single-player games on Diablo II
  1.14d, the PlugY stash (`.d2x`) and the shared stash are saved with the
  character every time the game saves it: its automatic save every few minutes,
  the save on death, `/save`, and leaving the game. A game that crashes or is
  closed by a bot manager reloads the character, its stash and the shared stash
  from the same moment. While another game holds the shared stash, all three are
  saved at the next save. If another game saved the shared stash after this one
  loaded it and this game changed it too, the character and its stash are saved
  when you leave the game, which reports the conflict and keeps this game's
  shared stash in a conflict file. With `ActiveAutoBackup=1` and the multi-page
  stash on, each game makes one backup, at its first save.

- **Shared stash across several single-player games at once.** Games running side
  by side can share one stash file safely: when another game saved the shared
  stash after this one loaded it, or the file can't be locked or written, PlugY
  leaves the main file as it is and saves this game's shared stash to
  `_LOD_[HC_]<SharedStashFilename>.conflict-<YYYYMMDD-HHMMSS>-<pid>.sss` beside it,
  for you to merge by hand. A game that didn't change the shared stash never
  rewrites it, and the message about a conflict appears once per game.

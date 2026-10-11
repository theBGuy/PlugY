- **Safer stash saves.** PlugY writes its `.d2x` and `.sss` save files to a
  temporary file first and swaps it in only once it is complete, so a crash or a
  full disk during a save leaves the previous file intact. If the swap can't
  happen, the new data is kept in a `.tmp` file next to the save, and a save
  file you marked read-only is left as it is.

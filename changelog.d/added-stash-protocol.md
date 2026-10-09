- **Stash requests for bots and scripts (Diablo II 1.14d).** With the new
  `[STASH] ActiveStashProtocol=1` (the default), a bot or script can show any stash
  page, see what every page holds, and move a stash item to any page without leaving
  the page it shows. kolbot uses it through `libs/modules/PlugY.js`, and the wire
  format is documented in `docs/stash-protocol.md` on GitHub.

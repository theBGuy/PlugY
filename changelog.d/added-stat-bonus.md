- **Magic find and gold find bonus.** On 1.14d, the new `[STAT BONUS]` section of `PlugY.ini`
  adds magic find (`MagicFind`) and gold find (`GoldFind`) on top of your gear, up to 10000% each,
  for every character in a single-player game or a TCP/IP game you host. PlugY's extra stats page
  (`/page 2`, with `ActiveNewStatsInterface=1`) shows the total, on a joiner too when it runs this
  version with the multi-page stash or the stat bonus turned on. It is off by default
  (`ActiveStatBonus=0`) and never saved in the character file: with data files that save magic find
  or gold find (some mods), the bonus for that stat is not applied.

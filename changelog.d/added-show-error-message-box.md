- **`ShowErrorMessageBox` option** in the `[GENERAL]` section of `PlugY.ini`, for
  unattended bots. Set it to 0 and the errors `PlugY.dll` raises in the game are
  reported without a message box waiting for a click; they reach the session
  log in `PlugY\logs` when `ActiveLogFile=1`. Fatal errors still close the
  game. The default, 1, keeps the message box. The `PlugY.exe` launcher keeps
  its own messages.

;	File created by Yohann NICOLAS.
Unicode True

!include "MUI2.nsh"
!include "Sections.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!include "WordFunc.nsh"

; VERSION ("v14.3.0") names the installer; DISPLAY_VERSION is the bare version for
; Add/Remove Programs. A /DVERSION=... override wins, minus a leading "v".
!ifdef VERSION
  !searchparse /noerrors "#${VERSION}" "#v" DISPLAY_VERSION
  !ifndef DISPLAY_VERSION
    !define DISPLAY_VERSION "${VERSION}"
  !endif
!else
  !searchparse /file "..\src\common\PlugYVersion.h" '#define PLUGY_VERSION "' PLUGY_VERSION '"'
  !define VERSION "v${PLUGY_VERSION}"
  !define DISPLAY_VERSION "${PLUGY_VERSION}"
!endif
; Defined (as "rc.2") only for a pre-release version such as 15.0.0-rc.2: the same "-" test as
; scripts/package.ps1.
!searchparse /noerrors "${DISPLAY_VERSION}" "-" PRERELEASE_ID
; package.ps1 stages the binaries and the contents of game-files\ beside this script.
!define D2FILES "."
!define NAME "PlugY4Kolbot"
; The pre-rename name keeps keying the registry, so installing over an upstream
; PlugY upgrades it in place.
!define LEGACY_NAME "PlugY, The Survival Kit"
!define MOD_DIR "Mod PlugY"
!define REGKEY "SOFTWARE\${LEGACY_NAME}"
; The single Add/Remove entry of upstream PlugY and older installs. Every install removes it: the
; uninstaller it starts trusts REGKEY, which then names the new install, so it would delete that
; install's PlugY.dll and data.
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_NAME}"
; One Add/Remove entry per install, under this key and named after its folder ($UninstKey).
!define UNINSTALL_ROOT "Software\Microsoft\Windows\CurrentVersion\Uninstall"
; REGKEY names only the install that ran last. Every install's Diablo II folder, by its own
; folder: its uninstaller and its updater must find it after another install took over REGKEY.
!define INSTALLS_KEY "SOFTWARE\${NAME}\Installs"
; By Diablo II folder, the install that set out to patch that Game.exe: only it restores the file,
; never a patch that was already there.
!define PATCHED_KEY "SOFTWARE\${NAME}\PatchedGame"
; Desktop / StartMenu: the install those shortcuts start, the last one that created them. Shortcut
; sections can be unticked, so that need not be the install REGKEY names.
!define SHORTCUTS_KEY "SOFTWARE\${NAME}\Shortcuts"
!define REPO_URL "https://github.com/theBGuy/PlugY4Kolbot"
!define UNINSTALL_FILE "PlugY Uninstaller.exe"
; The updater finds its install by its own folder in INSTALLS_KEY and runs the new installer for it
; with /D2= and /D=, so it must sit in $INSTDIR.
!define UPDATER_FILE "PlugY4KolbotUpdater.exe"
!define UPDATER_LNK "Check for ${NAME} updates.lnk"

!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_ICON "..\src\launcher\PlugY.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "PlugYInstallerHeader.bmp"
!define MUI_LANGDLL_REGISTRY_ROOT HKLM
!define MUI_LANGDLL_REGISTRY_KEY "${REGKEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Install Language"
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_UNFINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\$(README_FILENAME)"
!define MUI_FINISHPAGE_RUN ;"PlugY.exe"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchPlugY"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_NOREBOOTSUPPORT
!define MUI_ABORTWARNING
;!define MUI_UNABORTWARNING

Name "${NAME} ${VERSION}"
OutFile "PlugY4Kolbot_${VERSION}_Installer.exe"
InstallDirRegKey HKLM "${REGKEY}" "InstallPath"

Var D2Path			; the Diablo II folder: Game.exe, PlugY.dll and, when patched, everything
Var LastD2Path		; D2Path when $INSTDIR was last derived from it
Var RegInstallPath	; the registered install (REGKEY), "" when there is none
Var RegDllPath
Var BlizzardD2Path	; Diablo II's own InstallPath, where PlugY.exe finds Game.exe otherwise
Var SharedD2		; uninstall: 1 when another install also uses $D2Path
Var UninstKey		; this install's Add/Remove key
Var PatchOption		; 1 with /PATCH on the command line

;--------------------------------
; Pages
;!insertmacro MUI_PAGE_LICENSE "${NSISDIR}\Docs\Modern UI\License.txt"
!insertmacro MUI_PAGE_COMPONENTS
;Page Custom OptionsPage
!define MUI_PAGE_HEADER_TEXT "$(D2DIR_HEADER)"
!define MUI_PAGE_HEADER_SUBTEXT "$(D2DIR_SUBHEADER)"
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(D2DIR_TOP)"
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "$(D2DIR_DESTINATION)"
!define MUI_DIRECTORYPAGE_VARIABLE $D2Path
!define MUI_PAGE_CUSTOMFUNCTION_SHOW D2DirShow
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE D2DirLeave
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_HEADER_TEXT "$(PLUGYDIR_HEADER)"
!define MUI_PAGE_HEADER_SUBTEXT "$(PLUGYDIR_SUBHEADER)"
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(PLUGYDIR_TOP)"
!define MUI_PAGE_CUSTOMFUNCTION_PRE PlugYDirPre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE PlugYDirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH


;--------------------------------
; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "TradChinese"

LangString DESC_CORE ${LANG_ENGLISH} "Core files.$\nPlugY.dll will be installed in Diablo II directory."
LangString DESC_CORE ${LANG_FRENCH} "Fichiers nécessaires.$\nPlugY.dll sera installé dans le répertoire de Diablo II."
LangString DESC_CORE ${LANG_GERMAN} "Hauptdateien.$\nPlugY.dll wird in das Installationsverzeichnis von Diablo II installiert."
LangString DESC_CORE ${LANG_RUSSIAN} "Основные файлы.$\nPlugY.dll должен быть установлен в каталог Diablo II."
LangString DESC_CORE ${LANG_TRADCHINESE} "核心檔案。$\nPlugY.dll 將被安裝於 Diablo II 目錄。"
LangString DESC_DESKTOP_SHORTCUTS ${LANG_ENGLISH} "Add shortcut on Desktop to launch Diablo II with ${NAME}."
LangString DESC_DESKTOP_SHORTCUTS ${LANG_FRENCH} "Ajoute un raccourci pour démarrer Diablo II avec ${NAME}."
LangString DESC_DESKTOP_SHORTCUTS ${LANG_GERMAN} "Fügt eine Verknüpfung auf dem Desktop mit dem Namen ${NAME} hinzu mit der Diablo II gestartet werden kann."
LangString DESC_DESKTOP_SHORTCUTS ${LANG_RUSSIAN} "Добавить ярлык на рабочий стол для запуска Diablo II с ${NAME}."
LangString DESC_DESKTOP_SHORTCUTS ${LANG_TRADCHINESE} "在桌面新增捷徑以藉由 ${NAME} 執行 Diablo II。"
LangString DESC_MENU_SHORTCUTS ${LANG_ENGLISH} "Add shortcuts in start menu."
LangString DESC_MENU_SHORTCUTS ${LANG_FRENCH} "Ajoute des raccourcis dans le menu démarrer."
LangString DESC_MENU_SHORTCUTS ${LANG_GERMAN} "Fügt Verknüpfungen im Startmenü hinzu."
LangString DESC_MENU_SHORTCUTS ${LANG_RUSSIAN} "Добавить ярлыки в главное меню."
LangString DESC_MENU_SHORTCUTS ${LANG_TRADCHINESE} "新增「開始」功能表捷徑"
LangString DESC_UNINSTALLER ${LANG_ENGLISH} "Create a Windows uninstall program.$\nAdding registry keys in Windows."
LangString DESC_UNINSTALLER ${LANG_FRENCH} "Crée un programme Windows de désinstallation.$\nAjoute des clés de registres dans Windows."
LangString DESC_UNINSTALLER ${LANG_GERMAN} "Erzeugt ein Deinstallationsprogramm.$\nWindows Registrierungseinträge werden erstellt."
LangString DESC_UNINSTALLER ${LANG_RUSSIAN} "Создать в Windows деинстллятор программы.$\nДобавляет ключи реестра в Windows."
LangString DESC_UNINSTALLER ${LANG_TRADCHINESE} "建立 Windows 解除安裝程式。$\n加入 Windows 登錄訊息。"
LangString DESC_PATCH_GAME ${LANG_ENGLISH} "Makes Game.exe start PlugY by itself, as D2Bot and kolbot need. PlugY's files go next to Game.exe. Uninstalling restores Game.exe."
LangString DESC_PATCH_GAME ${LANG_FRENCH} "Game.exe démarre PlugY de lui-même, comme D2Bot et kolbot le demandent. Les fichiers de PlugY vont à côté de Game.exe. La désinstallation restaure Game.exe."
LangString DESC_PATCH_GAME ${LANG_GERMAN} "Game.exe startet PlugY selbst, wie D2Bot und kolbot es brauchen. Die PlugY-Dateien kommen neben Game.exe. Die Deinstallation stellt Game.exe wieder her."
LangString DESC_PATCH_GAME ${LANG_RUSSIAN} "Game.exe будет сам запускать PlugY, как нужно для D2Bot и kolbot. Файлы PlugY устанавливаются рядом с Game.exe. Удаление восстанавливает Game.exe."
LangString DESC_PATCH_GAME ${LANG_TRADCHINESE} "讓 Game.exe 自行載入 PlugY，D2Bot 與 kolbot 需要此設定。PlugY 的檔案會安裝在 Game.exe 旁。解除安裝時會還原 Game.exe。"
LangString SECTION_NAME_CORE ${LANG_ENGLISH} "${NAME} (required)"
LangString SECTION_NAME_CORE ${LANG_FRENCH} "${NAME} (nécessaire)"
LangString SECTION_NAME_CORE ${LANG_GERMAN} "${NAME} (benötigt)"
LangString SECTION_NAME_CORE ${LANG_RUSSIAN} "${NAME} (требуется)"
LangString SECTION_NAME_CORE ${LANG_TRADCHINESE} "${NAME} (必要)"
LangString SECTION_NAME_DESKTOP_SHORTCUT ${LANG_ENGLISH} "Desktop Shortcut"
LangString SECTION_NAME_DESKTOP_SHORTCUT ${LANG_FRENCH} "Raccourci sur le bureau"
LangString SECTION_NAME_DESKTOP_SHORTCUT ${LANG_GERMAN} "Desktopverknüpfung"
LangString SECTION_NAME_DESKTOP_SHORTCUT ${LANG_RUSSIAN} "Ярлык на рабочем столе"
LangString SECTION_NAME_DESKTOP_SHORTCUT ${LANG_TRADCHINESE} "桌面捷徑"
LangString SECTION_NAME_STARTMENU_SHORTCUTS ${LANG_ENGLISH} "Start menu Shortcuts"
LangString SECTION_NAME_STARTMENU_SHORTCUTS ${LANG_FRENCH} "Raccourcis dans le Menu démarrer"
LangString SECTION_NAME_STARTMENU_SHORTCUTS ${LANG_GERMAN} "Startmenüverknüpfungen"
LangString SECTION_NAME_STARTMENU_SHORTCUTS ${LANG_RUSSIAN} "Ярлыки в в главном меню"
LangString SECTION_NAME_STARTMENU_SHORTCUTS ${LANG_TRADCHINESE} "「開始」功能表捷徑"
LangString SECTION_NAME_UNINSTALLER ${LANG_ENGLISH} "Uninstaller (add keys registers)"
LangString SECTION_NAME_UNINSTALLER ${LANG_FRENCH} "Dé-installeur (ajoute clés de registre)"
LangString SECTION_NAME_UNINSTALLER ${LANG_GERMAN} "Deinstallationsprogramm (Fügt Windows Registrierungseinträge hinzu)"
LangString SECTION_NAME_UNINSTALLER ${LANG_RUSSIAN}  "Деинсталлятор (добавляет ключи реестра)"
LangString SECTION_NAME_UNINSTALLER ${LANG_TRADCHINESE} "移除工具 (加入登錄訊息)"
LangString SECTION_PATCH_GAME ${LANG_ENGLISH} "Patch Game.exe (D2Bot / kolbot)"
LangString SECTION_PATCH_GAME ${LANG_FRENCH} "Patcher Game.exe (D2Bot / kolbot)"
LangString SECTION_PATCH_GAME ${LANG_GERMAN} "Game.exe patchen (D2Bot / kolbot)"
LangString SECTION_PATCH_GAME ${LANG_RUSSIAN} "Пропатчить Game.exe (D2Bot / kolbot)"
LangString SECTION_PATCH_GAME ${LANG_TRADCHINESE} "修補 Game.exe (D2Bot / kolbot)"

LangString README_FILENAME ${LANG_ENGLISH} "PlugY_The_Survival_Kit_-_Readme.txt"
LangString README_FILENAME ${LANG_FRENCH} "PlugY_The_Survival_Kit_-_LisezMoi.txt"
LangString README_FILENAME ${LANG_GERMAN} "PlugY_The_Survival_Kit_-_Liesmich.txt"
LangString README_FILENAME ${LANG_RUSSIAN} "PlugY_The_Survival_Kit_-_Readme.txt"
LangString README_FILENAME ${LANG_TRADCHINESE} "PlugY_The_Survival_Kit_-_讀我檔案.txt"

LangString D2DIR_HEADER ${LANG_ENGLISH} "Choose the Diablo II folder"
LangString D2DIR_HEADER ${LANG_FRENCH} "Choisissez le répertoire de Diablo II"
LangString D2DIR_HEADER ${LANG_GERMAN} "Diablo II-Verzeichnis wählen"
LangString D2DIR_HEADER ${LANG_RUSSIAN} "Выберите каталог Diablo II"
LangString D2DIR_HEADER ${LANG_TRADCHINESE} "選擇 Diablo II 資料夾"
LangString D2DIR_SUBHEADER ${LANG_ENGLISH} "Choose the copy of Diablo II to add ${NAME} to."
LangString D2DIR_SUBHEADER ${LANG_FRENCH} "Choisissez la copie de Diablo II à laquelle ajouter ${NAME}."
LangString D2DIR_SUBHEADER ${LANG_GERMAN} "Wählen Sie die Diablo II-Kopie, der ${NAME} hinzugefügt wird."
LangString D2DIR_SUBHEADER ${LANG_RUSSIAN} "Выберите копию Diablo II, в которую будет добавлен ${NAME}."
LangString D2DIR_SUBHEADER ${LANG_TRADCHINESE} "選擇要加入 ${NAME} 的 Diablo II。"
LangString D2DIR_TOP ${LANG_ENGLISH} "PlugY.dll is installed in the Diablo II folder, the one that holds Game.exe. To use another copy of Diablo II, click Browse and select its folder."
LangString D2DIR_TOP ${LANG_FRENCH} "PlugY.dll est installé dans le répertoire de Diablo II, celui qui contient Game.exe. Pour utiliser une autre copie de Diablo II, cliquez sur Parcourir et sélectionnez son répertoire."
LangString D2DIR_TOP ${LANG_GERMAN} "PlugY.dll wird in das Diablo II-Verzeichnis installiert, das Game.exe enthält. Um eine andere Diablo II-Kopie zu verwenden, klicken Sie auf Durchsuchen und wählen Sie deren Verzeichnis."
LangString D2DIR_TOP ${LANG_RUSSIAN} "PlugY.dll устанавливается в каталог Diablo II, в котором находится Game.exe. Чтобы выбрать другую копию Diablo II, нажмите «Обзор» и укажите её каталог."
LangString D2DIR_TOP ${LANG_TRADCHINESE} "PlugY.dll 會安裝在含有 Game.exe 的 Diablo II 資料夾。若要使用另一份 Diablo II，請按「瀏覽」並選擇其資料夾。"
LangString D2DIR_DESTINATION ${LANG_ENGLISH} "Diablo II folder"
LangString D2DIR_DESTINATION ${LANG_FRENCH} "Répertoire de Diablo II"
LangString D2DIR_DESTINATION ${LANG_GERMAN} "Diablo II-Verzeichnis"
LangString D2DIR_DESTINATION ${LANG_RUSSIAN} "Каталог Diablo II"
LangString D2DIR_DESTINATION ${LANG_TRADCHINESE} "Diablo II 資料夾"
LangString PLUGYDIR_HEADER ${LANG_ENGLISH} "Choose the PlugY folder"
LangString PLUGYDIR_HEADER ${LANG_FRENCH} "Choisissez le répertoire de PlugY"
LangString PLUGYDIR_HEADER ${LANG_GERMAN} "PlugY-Verzeichnis wählen"
LangString PLUGYDIR_HEADER ${LANG_RUSSIAN} "Выберите каталог PlugY"
LangString PLUGYDIR_HEADER ${LANG_TRADCHINESE} "選擇 PlugY 資料夾"
LangString PLUGYDIR_SUBHEADER ${LANG_ENGLISH} "Choose where PlugY.exe, PlugY.ini and the PlugY folder go."
LangString PLUGYDIR_SUBHEADER ${LANG_FRENCH} "Choisissez où installer PlugY.exe, PlugY.ini et le répertoire PlugY."
LangString PLUGYDIR_SUBHEADER ${LANG_GERMAN} "Wählen Sie, wohin PlugY.exe, PlugY.ini und der Ordner PlugY kommen."
LangString PLUGYDIR_SUBHEADER ${LANG_RUSSIAN} "Выберите, куда установить PlugY.exe, PlugY.ini и папку PlugY."
LangString PLUGYDIR_SUBHEADER ${LANG_TRADCHINESE} "選擇 PlugY.exe、PlugY.ini 與 PlugY 資料夾的安裝位置。"
LangString PLUGYDIR_TOP ${LANG_ENGLISH} "PlugY.exe, PlugY.ini and the PlugY data folder are installed in this folder. Start the game with PlugY.exe or its shortcut: it reads PlugY.ini from here."
LangString PLUGYDIR_TOP ${LANG_FRENCH} "PlugY.exe, PlugY.ini et le répertoire de données PlugY sont installés dans ce répertoire. Lancez le jeu avec PlugY.exe ou son raccourci : il lit PlugY.ini ici."
LangString PLUGYDIR_TOP ${LANG_GERMAN} "PlugY.exe, PlugY.ini und der Datenordner PlugY werden in diesem Verzeichnis installiert. Starten Sie das Spiel mit PlugY.exe oder der Verknüpfung: es liest PlugY.ini von hier."
LangString PLUGYDIR_TOP ${LANG_RUSSIAN} "PlugY.exe, PlugY.ini и папка данных PlugY устанавливаются в этот каталог. Запускайте игру через PlugY.exe или его ярлык: он читает PlugY.ini отсюда."
LangString PLUGYDIR_TOP ${LANG_TRADCHINESE} "PlugY.exe、PlugY.ini 與 PlugY 資料夾會安裝在此資料夾。請以 PlugY.exe 或其捷徑啟動遊戲，它會從這裡讀取 PlugY.ini。"

LangString ERROR_NO_GAME_EXE ${LANG_ENGLISH} "Game.exe was not found in$\n$D2Path$\n$\nChoose the Diablo II folder that holds Game.exe."
LangString ERROR_NO_GAME_EXE ${LANG_FRENCH} "Game.exe est introuvable dans$\n$D2Path$\n$\nChoisissez le répertoire de Diablo II qui contient Game.exe."
LangString ERROR_NO_GAME_EXE ${LANG_GERMAN} "Game.exe wurde nicht gefunden in$\n$D2Path$\n$\nWählen Sie das Diablo II-Verzeichnis, das Game.exe enthält."
LangString ERROR_NO_GAME_EXE ${LANG_RUSSIAN} "Game.exe не найден в$\n$D2Path$\n$\nВыберите каталог Diablo II, в котором находится Game.exe."
LangString ERROR_NO_GAME_EXE ${LANG_TRADCHINESE} "在下列資料夾找不到 Game.exe：$\n$D2Path$\n$\n請選擇含有 Game.exe 的 Diablo II 資料夾。"

; $0 holds PatchD2File.exe's exit code ("?" when it could not run).
LangString PATCH_DONE ${LANG_ENGLISH} "Game.exe patched: it starts PlugY by itself."
LangString PATCH_DONE ${LANG_FRENCH} "Game.exe patché : il démarre PlugY de lui-même."
LangString PATCH_DONE ${LANG_GERMAN} "Game.exe gepatcht: es startet PlugY selbst."
LangString PATCH_DONE ${LANG_RUSSIAN} "Game.exe пропатчен: он сам запускает PlugY."
LangString PATCH_DONE ${LANG_TRADCHINESE} "Game.exe 已修補：會自行載入 PlugY。"
LangString PATCH_ALREADY ${LANG_ENGLISH} "Game.exe already starts PlugY."
LangString PATCH_ALREADY ${LANG_FRENCH} "Game.exe démarre déjà PlugY."
LangString PATCH_ALREADY ${LANG_GERMAN} "Game.exe startet PlugY bereits."
LangString PATCH_ALREADY ${LANG_RUSSIAN} "Game.exe уже запускает PlugY."
LangString PATCH_ALREADY ${LANG_TRADCHINESE} "Game.exe 已會載入 PlugY。"
LangString PATCH_IN_USE ${LANG_ENGLISH} "Game.exe is in use, so it can't be patched.$\n$\nClose Diablo II, and every D2Bot game started from$\n$D2Path$\nthen click Retry."
LangString PATCH_IN_USE ${LANG_FRENCH} "Game.exe est en cours d'utilisation et ne peut pas être patché.$\n$\nFermez Diablo II, et toutes les parties D2Bot lancées depuis$\n$D2Path$\npuis cliquez sur Réessayer."
LangString PATCH_IN_USE ${LANG_GERMAN} "Game.exe wird verwendet und kann nicht gepatcht werden.$\n$\nSchließen Sie Diablo II und alle D2Bot-Spiele, die aus$\n$D2Path$\ngestartet wurden, und klicken Sie dann auf Wiederholen."
LangString PATCH_IN_USE ${LANG_RUSSIAN} "Game.exe используется, поэтому его нельзя пропатчить.$\n$\nЗакройте Diablo II и все игры D2Bot, запущенные из$\n$D2Path$\nи нажмите «Повторить»."
LangString PATCH_IN_USE ${LANG_TRADCHINESE} "Game.exe 正在使用中，無法修補。$\n$\n請關閉 Diablo II 以及所有從下列資料夾啟動的 D2Bot 遊戲：$\n$D2Path$\n然後按「重試」。"
LangString PATCH_FAILED ${LANG_ENGLISH} "Game.exe could not be patched (PatchD2File.exe exit code $0).$\n$\n${NAME} is installed. To patch Game.exe later, run PatchD2File.exe in$\n$D2Path"
LangString PATCH_FAILED ${LANG_FRENCH} "Game.exe n'a pas pu être patché (code de sortie de PatchD2File.exe : $0).$\n$\n${NAME} est installé. Pour patcher Game.exe plus tard, lancez PatchD2File.exe dans$\n$D2Path"
LangString PATCH_FAILED ${LANG_GERMAN} "Game.exe konnte nicht gepatcht werden (Exitcode von PatchD2File.exe: $0).$\n$\n${NAME} ist installiert. Um Game.exe später zu patchen, starten Sie PatchD2File.exe in$\n$D2Path"
LangString PATCH_FAILED ${LANG_RUSSIAN} "Не удалось пропатчить Game.exe (код выхода PatchD2File.exe: $0).$\n$\n${NAME} установлен. Чтобы пропатчить Game.exe позже, запустите PatchD2File.exe в$\n$D2Path"
LangString PATCH_FAILED ${LANG_TRADCHINESE} "無法修補 Game.exe (PatchD2File.exe 結束代碼 $0)。$\n$\n${NAME} 已安裝。若要稍後修補 Game.exe，請執行下列資料夾中的 PatchD2File.exe：$\n$D2Path"
LangString UNPATCH_IN_USE ${LANG_ENGLISH} "Game.exe is in use, so it can't be restored.$\n$\nClose Diablo II, and every D2Bot game started from$\n$D2Path$\nthen click Retry."
LangString UNPATCH_IN_USE ${LANG_FRENCH} "Game.exe est en cours d'utilisation et ne peut pas être restauré.$\n$\nFermez Diablo II, et toutes les parties D2Bot lancées depuis$\n$D2Path$\npuis cliquez sur Réessayer."
LangString UNPATCH_IN_USE ${LANG_GERMAN} "Game.exe wird verwendet und kann nicht wiederhergestellt werden.$\n$\nSchließen Sie Diablo II und alle D2Bot-Spiele, die aus$\n$D2Path$\ngestartet wurden, und klicken Sie dann auf Wiederholen."
LangString UNPATCH_IN_USE ${LANG_RUSSIAN} "Game.exe используется, поэтому его нельзя восстановить.$\n$\nЗакройте Diablo II и все игры D2Bot, запущенные из$\n$D2Path$\nи нажмите «Повторить»."
LangString UNPATCH_IN_USE ${LANG_TRADCHINESE} "Game.exe 正在使用中，無法還原。$\n$\n請關閉 Diablo II 以及所有從下列資料夾啟動的 D2Bot 遊戲：$\n$D2Path$\n然後按「重試」。"
LangString UNPATCH_FAILED ${LANG_ENGLISH} "Game.exe could not be restored (exit code $0). PatchD2File.exe and RestoreD2File.exe stay in$\n$D2Path$\nRun RestoreD2File.exe there to restore it."
LangString UNPATCH_FAILED ${LANG_FRENCH} "Game.exe n'a pas pu être restauré (code de sortie $0). PatchD2File.exe et RestoreD2File.exe restent dans$\n$D2Path$\nLancez-y RestoreD2File.exe pour le restaurer."
LangString UNPATCH_FAILED ${LANG_GERMAN} "Game.exe konnte nicht wiederhergestellt werden (Exitcode $0). PatchD2File.exe und RestoreD2File.exe bleiben in$\n$D2Path$\nStarten Sie dort RestoreD2File.exe, um es wiederherzustellen."
LangString UNPATCH_FAILED ${LANG_RUSSIAN} "Не удалось восстановить Game.exe (код выхода $0). PatchD2File.exe и RestoreD2File.exe остаются в$\n$D2Path$\nЗапустите там RestoreD2File.exe, чтобы восстановить его."
LangString UNPATCH_FAILED ${LANG_TRADCHINESE} "無法還原 Game.exe (結束代碼 $0)。PatchD2File.exe 與 RestoreD2File.exe 會保留在：$\n$D2Path$\n請在該處執行 RestoreD2File.exe 以還原。"
; Followed by "[SECTION] Key=value" in the installer's details.
LangString INI_OPTION_ADDED ${LANG_ENGLISH} "New option in PlugY.ini:"
LangString INI_OPTION_ADDED ${LANG_FRENCH} "Nouvelle option dans PlugY.ini :"
LangString INI_OPTION_ADDED ${LANG_GERMAN} "Neue Option in PlugY.ini:"
LangString INI_OPTION_ADDED ${LANG_RUSSIAN} "Новый параметр в PlugY.ini:"
LangString INI_OPTION_ADDED ${LANG_TRADCHINESE} "PlugY.ini 新增選項："

; Drops a trailing backslash, so folders compare equal however they were typed or stored.
; A drive root ("C:\") stays whole. Clobbers $R9.
!macro TrimSlash VAR
  StrCpy $R9 ${VAR} 1 -1
  ${If} $R9 == "\"
    StrLen $R9 ${VAR}
    ${If} $R9 > 3
      IntOp $R9 $R9 - 1
      StrCpy ${VAR} ${VAR} $R9
    ${EndIf}
  ${EndIf}
!macroend

; $UninstKey for $INSTDIR: a key name can't hold "\", so those become "/". Clobbers $R7-$R9.
!macro SetUninstKey
  StrCpy $R8 ""
  StrCpy $R7 0
  ${Do}
    StrCpy $R9 $INSTDIR 1 $R7
    ${IfThen} $R9 == "" ${|} ${ExitDo} ${|}
    ${IfThen} $R9 == "\" ${|} StrCpy $R9 "/" ${|}
    StrCpy $R8 "$R8$R9"
    IntOp $R7 $R7 + 1
  ${Loop}
  StrCpy $UninstKey "${UNINSTALL_ROOT}\${NAME} $R8"
!macroend

; Shortcuts made before SHORTCUTS_KEY existed (LNK, recorded as VALUE) start the install REGKEY
; named before this one: record it, so only its uninstaller removes them. Clobbers $0.
!macro SeedShortcutOwner VALUE LNK
  ClearErrors
  ReadRegStr $0 HKLM "${SHORTCUTS_KEY}" "${VALUE}"
  ${If} ${Errors}
  ${AndIf} $RegInstallPath != ""
  ${AndIf} ${FileExists} "${LNK}"
    WriteRegStr HKLM "${SHORTCUTS_KEY}" "${VALUE}" "$RegInstallPath"
  ${EndIf}
!macroend

; SECTION is ticked only when $INSTDIR owns the shortcut (VALUE in SHORTCUTS_KEY, or the REGKEY
; install when there is no record, as SeedShortcutOwner assumes) and it still exists: LNK, or
; upstream PlugY's LEGACY_LNK, which Core deletes, so the section must replace it. Clobbers $0-$1.
!macro KeepShortcut SECTION VALUE LNK LEGACY_LNK
  ClearErrors
  ReadRegStr $0 HKLM "${SHORTCUTS_KEY}" "${VALUE}"
  ${If} ${Errors}
    StrCpy $0 $RegInstallPath
  ${EndIf}
  StrCpy $1 0
  ${If} $0 == $INSTDIR
    ${If} ${FileExists} "${LNK}"
    ${OrIf} ${FileExists} "${LEGACY_LNK}"
      StrCpy $1 1
    ${EndIf}
  ${EndIf}
  ${If} $1 == 1
    !insertmacro SelectSection ${SECTION}
  ${Else}
    !insertmacro UnselectSection ${SECTION}
  ${EndIf}
!macroend

;--------------------------------
; Initialisation
Function .onInit
  !define MUI_LANGDLL_WINDOWTITLE "${NAME} ${VERSION}"
  !define MUI_LANGDLL_INFO "Select your language:"
  !insertmacro MUI_LANGDLL_DISPLAY
  !undef MUI_LANGDLL_WINDOWTITLE
  !undef MUI_LANGDLL_INFO
  ReadRegStr $RegInstallPath HKLM "${REGKEY}" "InstallPath"
  ReadRegStr $RegDllPath HKLM "${REGKEY}" "PlugYDllPath"
  ; PlugY.exe's own lookup (PlugYRun.cpp): an HKCU key hides HKLM's even without InstallPath,
  ; and then PlugY.exe finds no registered Diablo II at all.
  ReadRegStr $BlizzardD2Path HKCU "SOFTWARE\Blizzard Entertainment\Diablo II" "InstallPath"
  ${If} $BlizzardD2Path == ""
    ClearErrors
    EnumRegValue $0 HKCU "SOFTWARE\Blizzard Entertainment\Diablo II" 0
    ${If} ${Errors}
      ReadRegStr $BlizzardD2Path HKLM "SOFTWARE\Blizzard Entertainment\Diablo II" "InstallPath"
    ${EndIf}
  ${EndIf}
  !insertmacro TrimSlash $RegInstallPath
  !insertmacro TrimSlash $RegDllPath
  !insertmacro TrimSlash $BlizzardD2Path
  !insertmacro TrimSlash $INSTDIR

  ; The registered install's Diablo II folder comes first, so an update stays where it is.
  ${If} $RegDllPath != ""
  ${AndIf} ${FileExists} "$RegDllPath\Game.exe"
    StrCpy $D2Path $RegDllPath
  ${ElseIf} $BlizzardD2Path != ""
    StrCpy $D2Path $BlizzardD2Path
  ${Else}
    StrCpy $D2Path "$PROGRAMFILES32\Diablo II"
  ${EndIf}
  ; /D2=<absolute folder>: the Diablo II folder, for a silent install, or for the updater, which also
  ; passes /D=. A silent run never falls back to another folder.
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/D2=" $R1
  ${IfNot} ${Errors}
    StrCpy $R2 $R1 2 1
    StrCpy $R3 $R1 2
    ${If} $R2 == ":\"
    ${OrIf} $R2 == ":/"
    ${OrIf} $R3 == "\\"
      StrCpy $D2Path $R1
      !insertmacro TrimSlash $D2Path
    ${ElseIf} ${Silent}
      SetErrorLevel 2
      Abort
    ${EndIf}
  ${EndIf}
  ; /PATCH ticks "Patch Game.exe" (InitLayout applies it), so a silent install can patch for D2Bot.
  ClearErrors
  ${GetOptions} $R0 "/PATCH" $R1
  ${IfNot} ${Errors}
  ${AndIf} $R1 == ""
    StrCpy $PatchOption 1
  ${EndIf}
  Call InitLayout
  Call KeepShortcuts
FunctionEnd

Function un.onInit
  !insertmacro MUI_UNGETLANGUAGE
  !insertmacro TrimSlash $INSTDIR
  ReadRegStr $RegInstallPath HKLM "${REGKEY}" "InstallPath"
  ReadRegStr $RegDllPath HKLM "${REGKEY}" "PlugYDllPath"
  ReadRegStr $D2Path HKLM "${INSTALLS_KEY}" "$INSTDIR"
  !insertmacro TrimSlash $RegInstallPath
  !insertmacro TrimSlash $RegDllPath
  ; Installs from before INSTALLS_KEY: REGKEY describes only the install that ran last, and a
  ; Diablo II folder that cannot be known is left alone.
  ${If} $D2Path == ""
    ${If} $RegInstallPath == $INSTDIR
      StrCpy $D2Path $RegDllPath
    ${ElseIf} ${FileExists} "$INSTDIR\Game.exe"
      StrCpy $D2Path $INSTDIR
    ${EndIf}
  ${EndIf}
  !insertmacro TrimSlash $D2Path
  !insertmacro SetUninstKey

  ; Another install using the same Diablo II folder (a "Mod PlugY" one beside a patched one, or a
  ; registered install of any version) still needs PlugY.dll there. One whose folder was deleted
  ; by hand, uninstaller included, no longer counts.
  StrCpy $SharedD2 0
  ${If} $D2Path != ""
    ${If} $RegInstallPath != $INSTDIR
    ${AndIf} $RegDllPath == $D2Path
    ${AndIf} ${FileExists} "$RegInstallPath\${UNINSTALL_FILE}"
      StrCpy $SharedD2 1
    ${EndIf}
    StrCpy $R2 0
    ${Do}
      ClearErrors
      EnumRegValue $R3 HKLM "${INSTALLS_KEY}" $R2
      ${IfThen} ${Errors} ${|} ${ExitDo} ${|}
      IntOp $R2 $R2 + 1
      ${If} $R3 != $INSTDIR
        ReadRegStr $R4 HKLM "${INSTALLS_KEY}" $R3
        !insertmacro TrimSlash $R4
        ${If} $R4 == $D2Path
        ${AndIf} ${FileExists} "$R3\${UNINSTALL_FILE}"
          StrCpy $SharedD2 1
        ${EndIf}
      ${EndIf}
    ${Loop}
  ${EndIf}
FunctionEnd

Function LaunchPlugY
	SetOutPath "$INSTDIR"
	ExecShell "" "$INSTDIR\PlugY.exe"
FunctionEnd

; Adds each option of the shipped PlugY.ini that $INSTDIR\PlugY.ini lacks, set to its PlugYDefault.ini value: PlugY
; already used that value, so nothing plays differently. An option already there, even empty, keeps its value; so does
; [LAUNCHING], which PlugY.exe reads from PlugY.ini alone. The shipped file's comment lines are not copied. It is
; parsed as written: "[SECTION]" and "Key=value" lines at the start of a line (scripts/installer-ini.test.mjs checks
; it). Clobbers $0-$7.
Function AddMissingOptions
  InitPluginsDir
  File "/oname=$PLUGINSDIR\PlugY.ini" "${D2FILES}\PlugY.ini"
  ClearErrors
  FileOpen $0 "$PLUGINSDIR\PlugY.ini" r
  ${IfThen} ${Errors} ${|} Return ${|}
  StrCpy $2 ""
  ${Do}
    ClearErrors
    FileRead $0 $1
    ${IfThen} ${Errors} ${|} ${ExitDo} ${|}
    ${TrimNewLines} $1 $1
    StrCpy $3 $1 1
    ${If} $3 == "["
      ClearErrors
      ${WordFind} $1 "]" "E+1" $2
      ${If} ${Errors}
        StrCpy $2 ""
      ${Else}
        StrCpy $2 $2 "" 1
        ${IfThen} $2 == "LAUNCHING" ${|} StrCpy $2 "" ${|}
      ${EndIf}
      ${If} $2 != ""
        ; $6 = 1 when the user's file holds an option of this section already; else WriteINIStr may append the
        ; section, so it gets an empty line before it first.
        FileSeek $0 0 CUR $7
        StrCpy $6 0
        ${Do}
          ClearErrors
          FileRead $0 $1
          ${IfThen} ${Errors} ${|} ${ExitDo} ${|}
          ${TrimNewLines} $1 $1
          StrCpy $3 $1 1
          ${IfThen} $3 == "[" ${|} ${ExitDo} ${|}
          ${If} $3 != ";"
            ClearErrors
            ${WordFind} $1 "=" "E+1" $3
            ${IfNot} ${Errors}
              ClearErrors
              ReadINIStr $4 "$INSTDIR\PlugY.ini" $2 $3
              ${IfNot} ${Errors}
                StrCpy $6 1
                ${ExitDo}
              ${EndIf}
            ${EndIf}
          ${EndIf}
        ${Loop}
        FileSeek $0 $7 SET
      ${EndIf}
    ${ElseIf} $3 != ";"
    ${AndIf} $2 != ""
      ClearErrors
      ${WordFind} $1 "=" "E+1" $3
      ${IfNot} ${Errors}
        ClearErrors
        ReadINIStr $4 "$INSTDIR\PlugY.ini" $2 $3
        ${If} ${Errors}
          ClearErrors
          ReadINIStr $4 "$INSTDIR\PlugY\PlugYDefault.ini" $2 $3
          ${If} ${Errors}
            ClearErrors
            ${WordFind} $1 "=" "E+1}" $4
            ${IfThen} ${Errors} ${|} StrCpy $4 "" ${|}
          ${EndIf}
          ${If} $6 == 0
            ClearErrors
            FileOpen $5 "$INSTDIR\PlugY.ini" a
            ${IfNot} ${Errors}
              FileSeek $5 0 END $7
              ${If} $7 > 0
                FileSeek $5 -1 END
                FileReadByte $5 $7
                FileSeek $5 0 END
                ${If} $7 != 10
                  FileWriteByte $5 13
                  FileWriteByte $5 10
                ${EndIf}
                FileWriteByte $5 13
                FileWriteByte $5 10
              ${EndIf}
              FileClose $5
            ${EndIf}
            StrCpy $6 1
          ${EndIf}
          ClearErrors
          WriteINIStr "$INSTDIR\PlugY.ini" $2 $3 $4
          ${IfNot} ${Errors}
            DetailPrint "$(INI_OPTION_ADDED) [$2] $3=$4"
          ${EndIf}
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${Loop}
  FileClose $0
FunctionEnd

InstType "Standard"
InstType "Minimal"

Section "!$(SECTION_NAME_CORE)" Core
  SectionIn 1 2 RO
  ; Upstream PlugY's shortcuts go, wherever its install is: its Start-menu uninstaller trusts
  ; REGKEY, which names this install from now on (see UNINSTALL_KEY).
  Delete "$DESKTOP\${LEGACY_NAME}.lnk"
  Delete "$SMPROGRAMS\${LEGACY_NAME}\Uninstall.lnk"
  Delete "$SMPROGRAMS\${LEGACY_NAME}\${LEGACY_NAME}.lnk"
  RMDir "$SMPROGRAMS\${LEGACY_NAME}"
  SetOutPath $D2Path
  File "${D2FILES}\PlugY.dll"
  SetOutPath $INSTDIR
  File "${D2FILES}\PlugY.exe"
  File "${D2FILES}\${UPDATER_FILE}"
  ; A new install in another folder of the same game, such as beside Game.exe for the patch, starts
  ; from the registered install's settings. A relative SavePath is relative to Diablo II's save
  ; folder, so saves stay found; a DllToLoad file kept in the other folder is not copied.
  ${If} $RegInstallPath != ""
  ${AndIf} $RegInstallPath != $INSTDIR
  ${AndIf} $RegDllPath == $D2Path
  ${AndIfNot} ${FileExists} "$INSTDIR\PlugY.ini"
  ${AndIf} ${FileExists} "$RegInstallPath\PlugY.ini"
    CopyFiles /SILENT "$RegInstallPath\PlugY.ini" "$INSTDIR"
  ${EndIf}
  ; An upgrade keeps the user's PlugY.ini; AddMissingOptions adds the options it lacks once
  ; PlugY\PlugYDefault.ini, which is always replaced, is in place.
  SetOverwrite off
  File "${D2FILES}\PlugY.ini"
  SetOverwrite on
  File "${D2FILES}\PlugY_The_Survival_Kit_-_Readme.txt"
  File "${D2FILES}\PlugY_The_Survival_Kit_-_LisezMoi.txt"
  File "${D2FILES}\PlugY_The_Survival_Kit_-_Liesmich.txt"
  File "${D2FILES}\PlugY_The_Survival_Kit_-_讀我檔案.txt"
  File /oname=LICENSE.txt "..\LICENSE"
  File "..\CHANGELOG.md"
  setOutPath "$INSTDIR\PlugY"
  File "${D2FILES}\PlugY\EmptyPage.dc6"
  File "${D2FILES}\PlugY\PlugYDefault.ini"
  File "${D2FILES}\PlugY\PlugYFixed.ini"
  File "${D2FILES}\PlugY\LocalizedStrings.ini"
  File "${D2FILES}\PlugY\SharedGoldBtns.dc6"
  File "${D2FILES}\PlugY\StashBtns.dc6"
  File "${D2FILES}\PlugY\StatsBackground.dc6"
  File "${D2FILES}\PlugY\statsinterface.txt"
  File "${D2FILES}\PlugY\TradeStash.dc6"
  File "${D2FILES}\PlugY\UnassignSkillsBtns.dc6"
  File "${D2FILES}\PlugY\UnassignStatsBtns.dc6"
  Call AddMissingOptions
!ifdef PRERELEASE_ID
  ; This block runs after the merge on every pre-release install or update (changelog-lib.mjs announces
  ; both settings), so it sees keys the merge added too; WriteINIStr keeps every other byte but ends a
  ; file that has no final newline with CRLF. Testers hear about the next pre-release only on that
  ; channel, so any value but "prerelease" (in any case, as LogicLib compares and the updater reads it)
  ; becomes prerelease. Their bug reports need whole-session logs: only ActiveLogFile=0 changes.
  ReadINIStr $0 "$INSTDIR\PlugY.ini" "GENERAL" "UpdateChannel"
  ${If} $0 != "prerelease"
    WriteINIStr "$INSTDIR\PlugY.ini" "GENERAL" "UpdateChannel" "prerelease"
  ${EndIf}
  ReadINIStr $0 "$INSTDIR\PlugY.ini" "GENERAL" "ActiveLogFile"
  ${If} $0 == "0"
    WriteINIStr "$INSTDIR\PlugY.ini" "GENERAL" "ActiveLogFile" "1"
  ${EndIf}
!endif
SectionEnd

; Opt-in: it changes Game.exe for every program that starts it, not only for PlugY.exe.
Section /o "$(SECTION_PATCH_GAME)" PatchGame
  ; Copies to run by hand. The installer runs its own copy from $PLUGINSDIR, where NSIS also loads
  ; its plug-ins: one in a game folder, which may be user-writable, could be swapped before it runs
  ; elevated.
  SetOutPath $D2Path
  File "${D2FILES}\PatchD2File.exe"
  File "${D2FILES}\RestoreD2File.exe"
  InitPluginsDir
  SetOutPath $PLUGINSDIR
  File "${D2FILES}\PatchD2File.exe"
  ; $OUTDIR is the patcher's working folder, the one it patches.
  SetOutPath $D2Path
  retry:
  ClearErrors
  ExecWait '"$PLUGINSDIR\PatchD2File.exe" --silent' $0
  ${If} ${Errors}
    StrCpy $0 "?"
  ${EndIf}
  ${If} $0 == 0
    DetailPrint "$(PATCH_DONE)"
  ${ElseIf} $0 == 1
    DetailPrint "$(PATCH_ALREADY)"
  ${Else}
    ${If} $0 == 4
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(PATCH_IN_USE)" /SD IDCANCEL IDRETRY retry
    ${EndIf}
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(PATCH_FAILED)" /SD IDOK
    ; A script that asked for the patch learns it failed: exit 3, the files installed but Game.exe as
    ; it was. Not for the patch preselected for an update, so the updater still sees exit 0.
    ${If} $PatchOption == 1
      SetErrorLevel 3
    ${EndIf}
  ${EndIf}
  ; This install owns the patch unless Game.exe was patched already (1), holds unexpected bytes (2)
  ; or is no supported game (3); the patcher reads it before asking for write access, so a locked or
  ; read-only Game.exe still answers 1 or 2. After another failure PATCH_FAILED sends the user to
  ; patch it by hand, and uninstalling must still restore it; restoring an unpatched one exits 1.
  ${If} $0 != 1
  ${AndIf} $0 != 2
  ${AndIf} $0 != 3
    WriteRegStr HKLM "${PATCHED_KEY}" "$D2Path" "$INSTDIR"
  ${EndIf}
SectionEnd

Section "$(SECTION_NAME_DESKTOP_SHORTCUT)" DesktopShortcuts
  SectionIn 1
  SetOutPath $INSTDIR
  CreateShortCut "$DESKTOP\${NAME}.lnk" "$INSTDIR\PlugY.exe" "" "$INSTDIR\PlugY.exe" 0
  WriteRegStr HKLM "${SHORTCUTS_KEY}" "Desktop" "$INSTDIR"
SectionEnd

Section $(SECTION_NAME_STARTMENU_SHORTCUTS) MenuShortcuts
  SectionIn 1
  CreateDirectory "$SMPROGRAMS\${NAME}"
  SetOutPath $INSTDIR
  CreateShortCut "$SMPROGRAMS\${NAME}\Uninstall.lnk" "$INSTDIR\${UNINSTALL_FILE}" "" "$INSTDIR\${UNINSTALL_FILE}" 0
  CreateShortCut "$SMPROGRAMS\${NAME}\${NAME}.lnk" "$INSTDIR\PlugY.exe" "" "$INSTDIR\PlugY.exe" 0
  CreateShortCut "$SMPROGRAMS\${NAME}\${UPDATER_LNK}" "$INSTDIR\${UPDATER_FILE}" "" "$INSTDIR\${UPDATER_FILE}" 0
  WriteRegStr HKLM "${SHORTCUTS_KEY}" "StartMenu" "$INSTDIR"
SectionEnd

Section $(SECTION_NAME_UNINSTALLER) Uninstaller
  ; Always installed: an upgrade must replace an upstream install's uninstaller and its Add/Remove
  ; entry, or that old uninstaller would later delete the wrong folders.
  SectionIn 1 2 RO

  ; Create uninstaller
  setOutPath "$INSTDIR"
  WriteUninstaller "${UNINSTALL_FILE}"

  ; Before REGKEY moves on to this install.
  !insertmacro SeedShortcutOwner Desktop "$DESKTOP\${NAME}.lnk"
  !insertmacro SeedShortcutOwner StartMenu "$SMPROGRAMS\${NAME}\${NAME}.lnk"

  ; Write the installation path into the registry
  WriteRegStr HKLM "${REGKEY}" "InstallPath" $INSTDIR
  WriteRegStr HKLM "${REGKEY}" "PlugYDllPath" "$D2Path"
  WriteRegStr HKLM "${INSTALLS_KEY}" "$INSTDIR" "$D2Path"

  ; Write the uninstall keys for Windows: one entry per install, named after its folder.
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  !insertmacro SetUninstKey
  WriteRegStr HKLM "$UninstKey" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "$UninstKey" "DisplayName" "${NAME} ($INSTDIR)"
  WriteRegStr HKLM "$UninstKey" "Publisher" "theBGuy"
  WriteRegStr HKLM "$UninstKey" "HelpLink" "${REPO_URL}"
  WriteRegStr HKLM "$UninstKey" "URLInfoAbout" "${REPO_URL}"
  WriteRegStr HKLM "$UninstKey" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKLM "$UninstKey" "UninstallString" '"$INSTDIR\${UNINSTALL_FILE}"'
  WriteRegDWORD HKLM "$UninstKey" "NoModify" 1
  WriteRegDWORD HKLM "$UninstKey" "NoRepair" 1
SectionEnd

;--------------------------------
; Layout: $INSTDIR follows $D2Path. Everything goes beside Game.exe when the game is patched
; (it reads PlugY.ini from its own folder) or is not Diablo II's registered copy (PlugY.exe
; finds Game.exe only beside itself or through that registry entry). Otherwise the PlugY
; folder page offers "Mod PlugY", or the registered install's own folder.
Function InitLayout
  ; Silent installs skip the folder page and its Game.exe check.
  ${If} ${Silent}
  ${AndIfNot} ${FileExists} "$D2Path\Game.exe"
    SetErrorLevel 2
    Abort
  ${EndIf}
  ${If} $PatchOption == 1
    !insertmacro SelectSection ${PatchGame}
  ${EndIf}
  ; Updating the install that patched this Game.exe: offer the same again.
  ReadRegStr $0 HKLM "${PATCHED_KEY}" "$D2Path"
  ${If} $0 != ""
  ${AndIf} $0 == $INSTDIR
    !insertmacro SelectSection ${PatchGame}
  ${EndIf}
  ; A silent run that names an existing install of this Diablo II folder (in INSTALLS_KEY, or REGKEY's
  ; pair for one made before that list), as the updater does with /D2= and /D=, updates it where it
  ; is, even where these rules would now choose another folder. The patch still puts everything
  ; beside Game.exe, where the patched game reads PlugY.ini.
  ReadRegStr $1 HKLM "${INSTALLS_KEY}" "$INSTDIR"
  ${If} $INSTDIR != ""
  ${AndIf} $INSTDIR == $RegInstallPath
  ${AndIf} $D2Path == $RegDllPath
    StrCpy $1 $D2Path
  ${EndIf}
  ${If} ${Silent}
  ${AndIf} $1 != ""
  ${AndIf} $1 == $D2Path
  ${AndIfNot} ${SectionIsSelected} ${PatchGame}
    StrCpy $LastD2Path $D2Path
    Return
  ${EndIf}
  ${If} ${SectionIsSelected} ${PatchGame}
  ${OrIf} $D2Path != $BlizzardD2Path
    StrCpy $INSTDIR $D2Path
  ${ElseIf} $INSTDIR == ""
    StrCpy $INSTDIR "$D2Path\${MOD_DIR}"
  ${ElseIf} $INSTDIR == $RegInstallPath
  ${AndIf} $D2Path != $RegDllPath
    StrCpy $INSTDIR "$D2Path\${MOD_DIR}"
  ${EndIf}
  StrCpy $LastD2Path $D2Path
FunctionEnd

; The shortcut sections for the install .onInit settles on: an update of it keeps its shortcuts as
; they are and never takes over another install's (the updater runs it silently); a new one keeps the
; default choice. An interactive run judges it before the folder pages; the components page shows the
; result, and the user can change it.
Function KeepShortcuts
  ${If} $INSTDIR == ""
    Return
  ${EndIf}
  ClearErrors
  ReadRegStr $0 HKLM "${INSTALLS_KEY}" "$INSTDIR"
  ${If} ${Errors}
  ${AndIf} $INSTDIR != $RegInstallPath
    Return
  ${EndIf}
  !insertmacro KeepShortcut ${DesktopShortcuts} Desktop "$DESKTOP\${NAME}.lnk" "$DESKTOP\${LEGACY_NAME}.lnk"
  !insertmacro KeepShortcut ${MenuShortcuts} StartMenu "$SMPROGRAMS\${NAME}\${NAME}.lnk" "$SMPROGRAMS\${LEGACY_NAME}\${LEGACY_NAME}.lnk"
FunctionEnd

; When PlugYDirPre will skip the PlugY folder page, this page's button installs; NSIS set its label
; at build time, from the page that follows. Judged on the folder shown when the page opens: an
; edit made on the page does not update it.
Function D2DirShow
  GetDlgItem $0 $HWNDPARENT 1
  ${If} ${SectionIsSelected} ${PatchGame}
  ${OrIf} $D2Path != $BlizzardD2Path
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(^InstallBtn)"
  ${Else}
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(^NextBtn)"
  ${EndIf}
FunctionEnd

Function D2DirLeave
  !insertmacro TrimSlash $D2Path
  ${IfNot} ${FileExists} "$D2Path\Game.exe"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(ERROR_NO_GAME_EXE)"
    Abort
  ${EndIf}
  ${If} ${SectionIsSelected} ${PatchGame}
  ${OrIf} $D2Path != $BlizzardD2Path
    StrCpy $INSTDIR $D2Path
  ${ElseIf} $D2Path != $LastD2Path
  ${OrIf} $INSTDIR == $LastD2Path
    ; Another folder, or back from the beside-Game.exe layout: the default PlugY folder again.
    ${If} $D2Path == $RegDllPath
    ${AndIf} $RegInstallPath != ""
      StrCpy $INSTDIR $RegInstallPath
    ${Else}
      StrCpy $INSTDIR "$D2Path\${MOD_DIR}"
    ${EndIf}
  ${EndIf}
  StrCpy $LastD2Path $D2Path
FunctionEnd

Function PlugYDirPre
  ${If} ${SectionIsSelected} ${PatchGame}
  ${OrIf} $D2Path != $BlizzardD2Path
    Abort
  ${EndIf}
FunctionEnd

Function PlugYDirLeave
  !insertmacro TrimSlash $INSTDIR
FunctionEnd

; Restores Game.exe in $D2Path. $R0 = 1 when that failed: the patcher tools must stay there.
Function un.RestoreGameExe
  StrCpy $R0 0
  ; The uninstaller's own copy, as in the PatchGame section: the one in $D2Path may have been swapped.
  InitPluginsDir
  SetOutPath $PLUGINSDIR
  File "${D2FILES}\PatchD2File.exe"
  SetOutPath $D2Path
  retry:
  ClearErrors
  ExecWait '"$PLUGINSDIR\PatchD2File.exe" -u --silent' $0
  ${If} ${Errors}
    StrCpy $0 "?"
  ${EndIf}
  ${If} $0 != 0
  ${AndIf} $0 != 1
    ${If} $0 == 4
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(UNPATCH_IN_USE)" /SD IDCANCEL IDRETRY retry
    ${EndIf}
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(UNPATCH_FAILED)" /SD IDOK
    StrCpy $R0 1
  ${EndIf}
  SetOutPath $TEMP
FunctionEnd

;--------------------------------
; Uninstall
Section "Uninstall" Uninstall
  ; REGKEY belongs to the install that ran last.
  ${If} $RegInstallPath == $INSTDIR
    DeleteRegKey HKLM "${REGKEY}"
  ${EndIf}
  DeleteRegKey HKLM "$UninstKey"
  DeleteRegValue HKLM "${INSTALLS_KEY}" "$INSTDIR"

  ; Shortcuts go with the install they start (SHORTCUTS_KEY). Upstream PlugY's were removed at
  ; install (Core).
  ReadRegStr $0 HKLM "${SHORTCUTS_KEY}" "Desktop"
  ${If} $0 == $INSTDIR
    Delete "$DESKTOP\${NAME}.lnk"
    DeleteRegValue HKLM "${SHORTCUTS_KEY}" "Desktop"
  ${EndIf}
  ReadRegStr $0 HKLM "${SHORTCUTS_KEY}" "StartMenu"
  ${If} $0 == $INSTDIR
    Delete "$SMPROGRAMS\${NAME}\Uninstall.lnk"
    Delete "$SMPROGRAMS\${NAME}\${NAME}.lnk"
    Delete "$SMPROGRAMS\${NAME}\${UPDATER_LNK}"
    RMDir "$SMPROGRAMS\${NAME}"
    DeleteRegValue HKLM "${SHORTCUTS_KEY}" "StartMenu"
  ${EndIf}

  ; Remove files and uninstaller. Game.exe first, and only when this install patched it.
  ${If} $D2Path != ""
    StrCpy $R0 0
    ReadRegStr $0 HKLM "${PATCHED_KEY}" "$D2Path"
    ${If} $0 == $INSTDIR
      Call un.RestoreGameExe
      ${If} $R0 != 1
        DeleteRegValue HKLM "${PATCHED_KEY}" "$D2Path"
      ${EndIf}
    ${EndIf}
    ${If} $SharedD2 != 1
      Delete "$D2Path\PlugY.dll"
      ${If} $R0 != 1
        Delete "$D2Path\PatchD2File.exe"
        Delete "$D2Path\RestoreD2File.exe"
      ${EndIf}
    ${EndIf}
  ${EndIf}
  DeleteRegKey /ifempty HKLM "${PATCHED_KEY}"
  DeleteRegKey /ifempty HKLM "${INSTALLS_KEY}"
  DeleteRegKey /ifempty HKLM "${SHORTCUTS_KEY}"
  DeleteRegKey /ifempty HKLM "SOFTWARE\${NAME}"
  Delete "$INSTDIR\PlugY.exe"
  Delete "$INSTDIR\${UPDATER_FILE}"
  Delete "$INSTDIR\${UPDATER_FILE}.old"
  Delete "$INSTDIR\PlugY.log"
  ; Beside Game.exe, BnetLog.txt is Diablo II's own.
  ${If} $INSTDIR != $D2Path
    Delete "$INSTDIR\BnetLog.txt"
  ${EndIf}
  Delete "$INSTDIR\PlugY.ini"
  Delete "$INSTDIR\PlugY_The_Survival_Kit_-_Readme.txt"
  Delete "$INSTDIR\PlugY_The_Survival_Kit_-_LisezMoi.txt"
  Delete "$INSTDIR\PlugY_The_Survival_Kit_-_Liesmich.txt"
  Delete "$INSTDIR\PlugY_The_Survival_Kit_-_讀我檔案.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\CHANGELOG.md"
  ; Core installs the data under $INSTDIR only: with a separate PlugY folder, a PlugY folder next
  ; to Game.exe belongs to a manual install.
  Delete "$INSTDIR\PlugY\EmptyPage.dc6"
  Delete "$INSTDIR\PlugY\PlugYDefault.ini"
  Delete "$INSTDIR\PlugY\PlugYFixed.ini"
  Delete "$INSTDIR\PlugY\LocalizedStrings.ini"
  Delete "$INSTDIR\PlugY\SharedGoldBtns.dc6"
  Delete "$INSTDIR\PlugY\StashBtns.dc6"
  Delete "$INSTDIR\PlugY\StatsBackground.dc6"
  Delete "$INSTDIR\PlugY\statsinterface.txt"
  Delete "$INSTDIR\PlugY\TradeStash.dc6"
  Delete "$INSTDIR\PlugY\UnassignSkillsBtns.dc6"
  Delete "$INSTDIR\PlugY\UnassignStatsBtns.dc6"
  ; Session logs of the games started from this folder (PlugY.dll writes them under the game's working
  ; directory; PlugY.log above is the single log of older versions).
  RMDir /r "$INSTDIR\PlugY\logs"
  RMDir "$INSTDIR\PlugY"
  Delete "$INSTDIR\${UNINSTALL_FILE}"
  ; Non-recursive: the updater's PlugY4Kolbot-backup\ holds the user's replaced files,
  ; so the install folder stays when backups exist.
  RMDir "$INSTDIR"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT "${Core}" $(DESC_CORE)
  !insertmacro MUI_DESCRIPTION_TEXT "${DesktopShortcuts}" $(DESC_DESKTOP_SHORTCUTS)
  !insertmacro MUI_DESCRIPTION_TEXT "${MenuShortcuts}" $(DESC_MENU_SHORTCUTS)
  !insertmacro MUI_DESCRIPTION_TEXT "${PatchGame}" $(DESC_PATCH_GAME)
  !insertmacro MUI_DESCRIPTION_TEXT "${Uninstaller}" $(DESC_UNINSTALLER)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

###########################################################################################################

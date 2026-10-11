<#
.SYNOPSIS
    Packages the binaries from build/out/<Config>/ into the PlugY4Kolbot release assets.

.DESCRIPTION
    Writes exactly these files to -OutDir, where <V> is PLUGY_VERSION from
    src/common/PlugYVersion.h:
      PlugY4Kolbot_v<V>.zip                 DLL, launcher, updater, INI, readmes, PlugY/ data
      PlugY4Kolbot_v<V>_D2FilePatcher.zip   PatchD2File.exe, RestoreD2File.exe
      PlugY4Kolbot_v<V>_Installer.exe       NSIS installer (installer/PlugY.nsi)
      PlugY4Kolbot_v<V>_symbols.zip         PDBs (updater included) and PlugY.map
      SHA256SUMS.txt                        sha256sum -c format, LF endings
    Inputs are staged under build/out/package/, so nothing is written inside the
    source tree. A pre-release ships a CHANGELOG.md with a section for itself,
    rendered from the pending changelog.d fragments by
    scripts/changelog-package.mjs; a stable release ships CHANGELOG.md as
    committed. Run scripts/build.ps1 first. Needs 7-Zip, NSIS 3.11 or later
    (makensis) and, for a pre-release, Node.js.

.PARAMETER Config
    Release (default) or Debug: which build/out/<Config>/ to package.

.PARAMETER OutDir
    Destination directory (default <repo>/dist), created if missing. Assets of a
    previous run in it (PlugY4Kolbot_v*, the pre-rename PlugY_The_Survival_Kit_v*,
    SHA256SUMS.txt) are replaced; a directory holding anything else is refused.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$OutDir
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildOut = Join-Path $RepoRoot 'build\out'
$BinDir = Join-Path $BuildOut $Config
$StageRoot = Join-Path $BuildOut 'package'
$GameFiles = Join-Path $RepoRoot 'game-files'
$InstallerSrc = Join-Path $RepoRoot 'installer'
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'dist' }
# Resolved against the PowerShell location: .NET's GetFullPath would use the
# process working directory, which Set-Location does not change.
$OutDir = [System.IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir))

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

# Runs a native tool and aborts on a non-zero exit code, echoing its output.
function Invoke-Tool([string]$Exe, [string[]]$Arguments) {
    # Under 'Stop', Windows PowerShell 5.1 turns any redirected stderr line into
    # a terminating error; failures are judged by exit code instead.
    $ErrorActionPreference = 'Continue'
    $output = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    foreach ($line in $output) {
        if ($line.Length -gt 0) { Write-Host $line }
    }
    if ($code -ne 0) {
        Fail ("(exit $code): `"$Exe`" " + ($Arguments -join ' '))
    }
}

function Find-Tool([string]$Name, [string]$Fallback) {
    $cmd = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    if (Test-Path -LiteralPath $Fallback) { return $Fallback }
    Fail "$Name not found on PATH or at $Fallback"
}

# Returns makensis's version text, or fails when it is older than $Minimum or unreadable.
# NSIS numbers its minors 01, 02 ... 13, so they compare as integers.
function Assert-NsisVersion([string]$Exe, [version]$Minimum) {
    $ErrorActionPreference = 'Continue'
    $text = ((& $Exe /VERSION 2>&1 | ForEach-Object { "$_" }) -join '').Trim()
    $code = $LASTEXITCODE
    $m = [regex]::Match($text, '\Av(\d{1,5})\.(\d{1,5})(?!\d)')
    if ($code -ne 0 -or -not $m.Success) { Fail "could not read the NSIS version from `"$Exe`" /VERSION (exit $code, output '$text')" }
    $found = [version]::new([int]$m.Groups[1].Value, [int]$m.Groups[2].Value)
    if ($found -lt $Minimum) { Fail "$Exe is NSIS $text; NSIS $Minimum or later is required" }
    return $text
}

function Test-UnderPath([string]$Path, [string]$Root) {
    $p = $Path.TrimEnd('\') + '\'
    $r = $Root.TrimEnd('\') + '\'
    return $p.StartsWith($r, [System.StringComparison]::OrdinalIgnoreCase)
}

function Bin([string]$Name) { return (Join-Path $BinDir $Name) }

function New-CleanDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Copy-Into([string]$Destination, [string[]]$Files) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($file in $Files) { Copy-Item -LiteralPath $file -Destination $Destination }
}

# Zips the contents of $Dir (no top-level folder). 7-Zip expands the wildcard
# itself, so non-ASCII names never pass through argv; -mcu=on stores them as UTF-8.
function New-Zip([string]$Dir, [string]$Zip) {
    Push-Location -LiteralPath $Dir
    try {
        Invoke-Tool $SevenZip @('a', '-tzip', '-mcu=on', '-bso0', '-bsp0', $Zip, '*')
    } finally {
        Pop-Location
    }
}

# --- Version ------------------------------------------------------------------------
$VersionHeader = Join-Path $RepoRoot 'src\common\PlugYVersion.h'
$m = [regex]::Match([System.IO.File]::ReadAllText($VersionHeader), '(?m)^#define PLUGY_VERSION "([^"]*)"')
if (-not $m.Success) { Fail "PLUGY_VERSION not found in $VersionHeader" }
$Version = $m.Groups[1].Value
# Same rule as scripts/version-lib.mjs: semver without leading zeros, at most
# 13 characters (the main menu's 200 px version line, MainScreen.cpp).
if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(alpha|beta|rc)\.(0|[1-9][0-9]*))?\z') { Fail "PLUGY_VERSION '$Version' is not X.Y.Z or X.Y.Z-alpha|beta|rc.N" }
if ($Version.Length -gt 13) { Fail "PLUGY_VERSION '$Version' is longer than 13 characters" }
$Base = "PlugY4Kolbot_v$Version"
$IsPrerelease = $Version.Contains('-')

# --- Inputs and tools ---------------------------------------------------------------
$Binaries = @('PlugY.dll', 'PlugY.exe', 'PatchD2File.exe', 'RestoreD2File.exe', 'PlugY4KolbotUpdater.exe')
$Symbols = @('PlugY.pdb', 'PlugY.map', 'PlugYRun.pdb', 'PatchD2File.pdb', 'RestoreD2File.pdb', 'PlugY4KolbotUpdater.pdb')
foreach ($name in $Binaries + $Symbols) {
    if (-not (Test-Path -LiteralPath (Join-Path $BinDir $name))) { Fail "$name missing from $BinDir (run scripts/build.ps1 -Config $Config first)" }
}
$License = Join-Path $RepoRoot 'LICENSE'
$Changelog = Join-Path $RepoRoot 'CHANGELOG.md'
$Ini = Join-Path $GameFiles 'PlugY.ini'
foreach ($file in @($License, $Changelog, $Ini)) {
    if (-not (Test-Path -LiteralPath $file)) { Fail "$file not found" }
}
# The zh-TW readme name is non-ASCII and this file must stay ASCII, so readmes are
# selected by pattern; PlugY.nsi installs exactly four.
$Readmes = @(Get-ChildItem -LiteralPath $GameFiles -Filter 'PlugY_The_Survival_Kit_-_*.txt' -File | ForEach-Object { $_.FullName })
if ($Readmes.Count -ne 4) { Fail "expected 4 readmes in $GameFiles, found $($Readmes.Count)" }
# The zip must carry exactly the data files the installer installs, so the set comes
# from PlugY.nsi's File lines and the folder may hold nothing else.
$DataDir = Join-Path $GameFiles 'PlugY'
$nsiText = [System.IO.File]::ReadAllText((Join-Path $InstallerSrc 'PlugY.nsi'), [System.Text.Encoding]::UTF8)
$DataNames = @([regex]::Matches($nsiText, '(?m)^\s*File\s+"\$\{D2FILES\}\\PlugY\\([^"\\]+)"') | ForEach-Object { $_.Groups[1].Value })
if ($DataNames.Count -eq 0) { Fail "no File `"`${D2FILES}\PlugY\...`" lines found in PlugY.nsi" }
$onDisk = @(Get-ChildItem -LiteralPath $DataDir -File | ForEach-Object { $_.Name })
$missing = @($DataNames | Where-Object { $onDisk -notcontains $_ })
$extra = @($onDisk | Where-Object { $DataNames -notcontains $_ })
if ($missing.Count -gt 0 -or $extra.Count -gt 0) {
    Fail ("$DataDir does not match PlugY.nsi's File list; missing: [" + ($missing -join ', ') + '], not installed: [' + ($extra -join ', ') + ']')
}
$DataFiles = @($DataNames | ForEach-Object { Join-Path $DataDir $_ })

# A stale build/out from another version would otherwise ship under this version's name.
$FileVersion = ($Version -replace '-.*$', '') + '.0'
foreach ($name in $Binaries) {
    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Bin $name))
    # PatchD2File/RestoreD2File keep the patcher's own FileVersion.
    $checkFile = $name -notin @('PatchD2File.exe', 'RestoreD2File.exe')
    if ($info.ProductVersion -cne $Version -or ($checkFile -and $info.FileVersion -cne $FileVersion)) {
        $expected = "ProductVersion $Version" + $(if ($checkFile) { ", FileVersion $FileVersion" } else { '' })
        Fail ("binaries were built from a different version; run scripts/build.ps1 first ($name has " +
              "ProductVersion $($info.ProductVersion), FileVersion $($info.FileVersion); expected $expected)")
    }
}

$SevenZip = Find-Tool '7z' 'C:\Program Files\7-Zip\7z.exe'
$MakeNsis = Find-Tool 'makensis' 'C:\Program Files (x86)\NSIS\makensis.exe'
# NSIS 3.11 retries creating a restricted $PLUGINSDIR (NSIS bug #1315); the installer
# and uninstaller run PatchD2File.exe from that folder.
$MakeNsisVersion = Assert-NsisVersion $MakeNsis '3.11'
$Node = $(if ($IsPrerelease) { Find-Tool 'node' 'C:\Program Files\nodejs\node.exe' })
Write-Host "Packaging PlugY4Kolbot $Version ($Config)"
Write-Host "7-Zip:    $SevenZip"
Write-Host "makensis: $MakeNsis ($MakeNsisVersion)"
if ($Node) { Write-Host "node:     $Node" }
New-CleanDirectory $StageRoot

# --- Changelog ----------------------------------------------------------------------
# The committed CHANGELOG.md gets no section for a pre-release, so its testers would
# find nothing about it; the zip and the installer ship a rendered copy instead. It is
# rendered before -OutDir is swept, so a failure leaves the previous assets in place.
if ($IsPrerelease) {
    $changelogDir = Join-Path $StageRoot 'changelog'
    New-Item -ItemType Directory -Force -Path $changelogDir | Out-Null
    $Changelog = Join-Path $changelogDir 'CHANGELOG.md'
    Invoke-Tool $Node @((Join-Path $RepoRoot 'scripts\changelog-package.mjs'), $Version, $Changelog)
    if (-not (Test-Path -LiteralPath $Changelog)) { Fail "scripts/changelog-package.mjs did not write $Changelog" }
}

# --- Output directory ---------------------------------------------------------------
# Only a previous run's assets are ever deleted, including those named before the
# PlugY4Kolbot rename. An existing -OutDir holding anything else (another file, any
# subdirectory) is refused untouched, as is any directory that contains the repo or
# sits inside it outside dist\.
if ((Test-UnderPath $RepoRoot $OutDir) -or
    ((Test-UnderPath $OutDir $RepoRoot) -and -not (Test-UnderPath $OutDir (Join-Path $RepoRoot 'dist')))) {
    Fail "refusing to use -OutDir $OutDir (inside the repo it must be under dist\)"
}
if (Test-Path -LiteralPath $OutDir -PathType Leaf) { Fail "-OutDir $OutDir is a file" }
if (Test-Path -LiteralPath $OutDir) {
    $entries = @(Get-ChildItem -LiteralPath $OutDir -Force)
    $foreign = @($entries | Where-Object {
        $_.PSIsContainer -or -not ($_.Name -like 'PlugY4Kolbot_v*' -or $_.Name -like 'PlugY_The_Survival_Kit_v*' -or
                                 $_.Name -eq 'SHA256SUMS.txt')
    } | ForEach-Object { $_.Name })
    if ($foreign.Count -gt 0) {
        Fail ("refusing to use -OutDir $OutDir`: it holds entries that are not PlugY4Kolbot release assets (" +
              (($foreign | Select-Object -First 5) -join ', ') + $(if ($foreign.Count -gt 5) { ', ...' } else { '' }) +
              '); pass an empty or new directory')
    }
    foreach ($entry in $entries) { Remove-Item -LiteralPath $entry.FullName -Force }
} else {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

# --- Main zip -----------------------------------------------------------------------
$main = Join-Path $StageRoot 'main'
Copy-Into $main (@((Bin 'PlugY.dll'), (Bin 'PlugY.exe'), (Bin 'PlugY4KolbotUpdater.exe'), $Ini, $Changelog) + $Readmes)
Copy-Item -LiteralPath $License -Destination (Join-Path $main 'LICENSE.txt')
Copy-Into (Join-Path $main 'PlugY') $DataFiles
New-Zip $main (Join-Path $OutDir "$Base.zip")

# --- Patcher zip --------------------------------------------------------------------
$patcher = Join-Path $StageRoot 'patcher'
Copy-Into $patcher @((Bin 'PatchD2File.exe'), (Bin 'RestoreD2File.exe'))
Copy-Item -LiteralPath $License -Destination (Join-Path $patcher 'LICENSE.txt')
New-Zip $patcher (Join-Path $OutDir "${Base}_D2FilePatcher.zip")

# --- Symbols zip --------------------------------------------------------------------
$symbolsDir = Join-Path $StageRoot 'symbols'
Copy-Into $symbolsDir @($Symbols | ForEach-Object { Bin $_ })
New-Zip $symbolsDir (Join-Path $OutDir "${Base}_symbols.zip")

# --- Installer ----------------------------------------------------------------------
# PlugY.nsi takes the binaries and game files from its own directory and reaches
# ..\src\launcher, ..\src\common, ..\LICENSE and ..\CHANGELOG.md, so the stage puts them there.
$nsisRoot = Join-Path $StageRoot 'installer'
$nsisDir = Join-Path $nsisRoot 'installer'
Copy-Into $nsisDir (@((Join-Path $InstallerSrc 'PlugY.nsi'), (Join-Path $InstallerSrc 'PlugYInstallerHeader.bmp'), $Ini) +
                    $Readmes + @($Binaries | ForEach-Object { Bin $_ }))
Copy-Into (Join-Path $nsisDir 'PlugY') $DataFiles
Copy-Into (Join-Path $nsisRoot 'src\launcher') @((Join-Path $RepoRoot 'src\launcher\PlugY.ico'))
Copy-Into (Join-Path $nsisRoot 'src\common') @($VersionHeader)
Copy-Into $nsisRoot @($License, $Changelog)
# /WX: a warning (a missing optional file, a stale define) fails the release
# instead of shipping a quietly different installer.
Invoke-Tool $MakeNsis @('/V2', '/WX', (Join-Path $nsisDir 'PlugY.nsi'))
$installer = Join-Path $nsisDir "${Base}_Installer.exe"
if (-not (Test-Path -LiteralPath $installer)) { Fail "makensis did not produce $installer" }
Move-Item -LiteralPath $installer -Destination $OutDir

# --- Checksums ----------------------------------------------------------------------
$assets = [string[]]@(Get-ChildItem -LiteralPath $OutDir -File | ForEach-Object { $_.Name })
[Array]::Sort($assets, [System.StringComparer]::Ordinal)
$sums = ''
foreach ($name in $assets) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $OutDir $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    $sums += "$hash  $name`n"
}
[System.IO.File]::WriteAllText((Join-Path $OutDir 'SHA256SUMS.txt'), $sums, (New-Object System.Text.UTF8Encoding $false))

Write-Host ''
Write-Host "Packaged $($assets.Count) assets + SHA256SUMS.txt in $OutDir" -ForegroundColor Green
exit 0

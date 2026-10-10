<#
.SYNOPSIS
    Installs the pinned NSIS release that CI builds the installer with.

.DESCRIPTION
    Downloads nsis-<version>.zip from SourceForge, refuses it unless its SHA256
    matches the pin below, extracts it and checks that its makensis reports the
    pinned version. Under GitHub Actions the folder holding makensis.exe goes to
    the front of PATH for the later steps, where scripts/package.ps1 finds it;
    elsewhere the folder is only printed.

.PARAMETER Destination
    Folder to work in (default: $env:RUNNER_TEMP, else the user's temp folder).
    Its nsis-pinned subfolder is replaced.
#>
[CmdletBinding()]
param(
    [string]$Destination
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 redraws a progress bar per downloaded chunk, which slows Invoke-WebRequest.
$ProgressPreference = 'SilentlyContinue'

# NSIS publishes no SHA256: before moving the pin, check the zip against the MD5 in
# SourceForge's file list (https://sourceforge.net/projects/nsis/rss?path=/NSIS%203).
$Version = '3.13'
$Sha256 = 'ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8'
$Url = "https://downloads.sourceforge.net/project/nsis/NSIS%203/$Version/nsis-$Version.zip"

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

if (-not $Destination) {
    $Destination = $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() })
}
# Resolved against the PowerShell location: .NET's GetFullPath would use the
# process working directory, which Set-Location does not change.
$Destination = [System.IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination))
$work = Join-Path $Destination 'nsis-pinned'
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$zip = Join-Path $work "nsis-$Version.zip"

# SourceForge redirects to a mirror per request, so a retry can reach a working one.
# It answers a browser User-Agent (PowerShell's default starts with Mozilla) with an
# HTML download page instead of the file.
$attempts = 3
for ($attempt = 1; ; $attempt++) {
    try {
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing -UserAgent 'PlugY4Kolbot-CI' -TimeoutSec 120
        break
    } catch {
        if ($attempt -ge $attempts) { Fail "could not download $Url ($($_.Exception.Message))" }
        Write-Host "Download attempt $attempt of $attempts failed ($($_.Exception.Message)); retrying"
        Start-Sleep -Seconds (10 * $attempt)
    }
}

$actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -cne $Sha256) { Fail "nsis-$Version.zip from $Url has SHA256 $actual; expected $Sha256" }

Expand-Archive -LiteralPath $zip -DestinationPath $work
$bin = Join-Path $work "nsis-$Version\Bin"
$makensis = Join-Path $bin 'makensis.exe'
if (-not (Test-Path -LiteralPath $makensis)) { Fail "nsis-$Version.zip has no nsis-$Version\Bin\makensis.exe" }
$reported = (& $makensis /VERSION | ForEach-Object { "$_" }) -join ''
if ($LASTEXITCODE -ne 0 -or $reported -cne "v$Version") {
    Fail "`"$makensis`" /VERSION reported '$reported' (exit $LASTEXITCODE); expected v$Version"
}

if ($env:GITHUB_PATH) {
    [System.IO.File]::AppendAllText($env:GITHUB_PATH, "$bin`n")
    Write-Host "NSIS $Version (SHA256 verified) is first on PATH from $bin"
} else {
    Write-Host "NSIS $Version (SHA256 verified) is in $bin"
}
exit 0

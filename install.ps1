param(
  [ValidateSet("install", "update", "reinstall", "uninstall", "check")]
  [string]$Mode = "install",
  [switch]$Uninstall
)

if ($Uninstall) {
  $Mode = "uninstall"
}

$ErrorActionPreference = "Stop"
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

$BaseUrl = if ($env:TEZZ_INSTALL_BASE) { $env:TEZZ_INSTALL_BASE } else { "https://tezznative.org/download" }
$PortalUrl = if ($env:TEZZ_PORTAL_BASE) { $env:TEZZ_PORTAL_BASE.TrimEnd("/") } else { "https://tezznative.org" }
$InstallScope = if ($env:TEZZ_INSTALL_SCOPE) { $env:TEZZ_INSTALL_SCOPE.ToLowerInvariant() } else { "user" }
if ($InstallScope -ne "user" -and $InstallScope -ne "system") { $InstallScope = "user" }

$DefaultInstallDir = if ($InstallScope -eq "system") { "C:\ProgramData\TezzNative" } else { Join-Path $HOME "TezzNative" }
$InstallDir = if ($env:TEZZ_INSTALL_DIR) { $env:TEZZ_INSTALL_DIR } else { $DefaultInstallDir }
$SdkDir = Join-Path $InstallDir "sdk"
$ZipPath = Join-Path $env:TEMP ("tezznative-sdk-" + [guid]::NewGuid().ToString() + ".zip")
$CacheBust = if ($env:TEZZ_INSTALL_CACHE_BUST) { $env:TEZZ_INSTALL_CACHE_BUST } else { [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString() }
$PathMode = if ($env:TEZZ_INSTALL_AUTO_PATH) { $env:TEZZ_INSTALL_AUTO_PATH.ToLowerInvariant() } else { "user" }
if ($PathMode -eq "auto") { $PathMode = if ($InstallScope -eq "system") { "system" } else { "user" } }
if ($PathMode -ne "user" -and $PathMode -ne "system" -and $PathMode -ne "none") { $PathMode = "user" }

function Test-IsAdmin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($id)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RemoteVersion {
  try {
    $local = Get-LocalVersion
    $uri = "$PortalUrl/api/update_check.php?platform=windows-x64&version=$([uri]::EscapeDataString($local))&mode=$([uri]::EscapeDataString($Mode))"
    $json = Invoke-RestMethod -Method Get -Uri $uri -TimeoutSec 15
    if ($json -and $json.data -and $json.data.latest -and $json.data.latest.version) {
      return [string]$json.data.latest.version
    }
  } catch {
  }
  try {
    $json = Invoke-RestMethod -Method Get -Uri "$BaseUrl/sdk/version.json" -TimeoutSec 15
    if ($json -and $json.version) { return [string]$json.version }
  } catch {
  }
  return ""
}

function Send-InstallEvent {
  param(
    [string]$Status,
    [string]$Message
  )
  try {
    $installId = if ($env:COMPUTERNAME) { $env:COMPUTERNAME } else { [guid]::NewGuid().ToString() }
    Invoke-RestMethod -Method Post -Uri "$PortalUrl/api/install_event.php" -Body @{
      platform = "windows-x64"
      version = $(if ($RemoteVersion) { $RemoteVersion } else { "unknown" })
      status = $Status
      install_id = $installId
      message = $Message
    } | Out-Null
  } catch {
  }
}

function Get-LocalVersion {
  $verPath = Join-Path $SdkDir "version.json"
  if (Test-Path $verPath) {
    try {
      $obj = Get-Content $verPath -Raw | ConvertFrom-Json
      if ($obj -and $obj.version) { return [string]$obj.version }
    } catch {
    }
  }
  $modPath = Join-Path $SdkDir "tezz.mod"
  if (Test-Path $modPath) {
    $line = Get-Content $modPath | Where-Object { $_ -match '^\s*version\s*=' } | Select-Object -First 1
    if ($line) { return ($line -replace '^\s*version\s*=\s*', '').Trim() }
  }
  return ""
}

function Remove-PathEntry {
  param(
    [string]$Scope,
    [string]$PathValue
  )
  try {
    $current = [Environment]::GetEnvironmentVariable("Path", $Scope)
    if (-not $current) { return }
    $parts = $current.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
    $filtered = @()
    foreach ($part in $parts) {
      if ($part.TrimEnd('\', '/') -ine $PathValue.TrimEnd('\', '/')) {
        $filtered += $part
      }
    }
    $next = ($filtered -join ';')
    [Environment]::SetEnvironmentVariable("Path", $next, $Scope)
  } catch {
  }
}

function Purge-LegacyEnvironment {
  # 1. Stop any legacy server processes
  Get-Process | Where-Object { $_.ProcessName -like "tezz_serve*" -or $_.ProcessName -eq "tezz_http_server" } | Stop-Process -Force -ErrorAction SilentlyContinue

  # 2. Remove legacy .tezznative directory
  $legacyDir = Join-Path $HOME ".tezznative"
  if (Test-Path $legacyDir) {
    try {
      Remove-Item -Recurse -Force $legacyDir -ErrorAction SilentlyContinue
      Write-Host "Purged legacy installation: $legacyDir" -ForegroundColor Yellow
    } catch {
    }
  }

  # 3. Clean legacy .tezznative\bin and stale paths from User and Machine PATH
  foreach ($scope in @("User", "Machine")) {
    try {
      $p = [Environment]::GetEnvironmentVariable("Path", $scope)
      if ($p) {
        $parts = $p.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
        $kept = @()
        $changed = $false
        foreach ($part in $parts) {
          if ($part -match '(?i)[\/\\]\.tezznative([\/\\]bin)?$') {
            $changed = $true
          } else {
            $kept += $part
          }
        }
        if ($changed) {
          [Environment]::SetEnvironmentVariable("Path", ($kept -join ';'), $scope)
        }
      }
    } catch {
    }
  }

  # 4. Clean current session PATH
  if ($env:PATH) {
    $sessParts = $env:PATH.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
    $sessKept = @()
    foreach ($sp in $sessParts) {
      if ($sp -notmatch '(?i)[\/\\]\.tezznative([\/\\]bin)?$') {
        $sessKept += $sp
      }
    }
    $env:PATH = ($sessKept -join ';')
  }
}

function Add-PathIfMissing {
  param(
    [string]$Scope,
    [string]$PathValue
  )
  $current = [Environment]::GetEnvironmentVariable("Path", $Scope)
  if (-not $current) { $current = "" }
  $parts = $current.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
  foreach ($part in $parts) {
    if ($part.TrimEnd('\', '/') -ieq $PathValue.TrimEnd('\', '/')) {
      if ($env:PATH -notmatch [regex]::Escape($PathValue)) {
        $env:PATH = "$PathValue;$env:PATH"
      }
      return
    }
  }
  # Prepend so new install takes precedence
  $next = if ([string]::IsNullOrWhiteSpace($current)) { $PathValue } else { "$PathValue;$current" }
  [Environment]::SetEnvironmentVariable("Path", $next, $Scope)
  if ($env:PATH -notmatch [regex]::Escape($PathValue)) {
    $env:PATH = "$PathValue;$env:PATH"
  }
}

function Configure-Env {
  param(
    [string]$BinDir,
    [string]$SdkRoot
  )
  switch ($PathMode) {
    "system" {
      if (Test-IsAdmin) {
        Add-PathIfMissing -Scope "Machine" -PathValue $BinDir
        [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "Machine")
      } else {
        Write-Host "Admin rights not available. Falling back to user PATH/env."
        Add-PathIfMissing -Scope "User" -PathValue $BinDir
        [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
      }
    }
    "none" {
      [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
    }
    default {
      Add-PathIfMissing -Scope "User" -PathValue $BinDir
      [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
    }
  }
  $env:TEZZ_SDK_ROOT = $SdkRoot
  try {
    if (-not ("TezzNativeInstaller.EnvBroadcast" -as [type])) {
      Add-Type -Namespace TezzNativeInstaller -Name EnvBroadcast -MemberDefinition @"
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode, SetLastError=true)]
        private static extern System.IntPtr SendMessageTimeout(System.IntPtr hWnd, uint Msg, System.IntPtr wParam, System.IntPtr lParam, uint flags, uint timeout, out System.IntPtr result);
        public static void Notify() {
          System.IntPtr value = System.Runtime.InteropServices.Marshal.StringToHGlobalUni("Environment");
          try { System.IntPtr result; SendMessageTimeout(new System.IntPtr(0xffff), 0x001A, System.IntPtr.Zero, value, 0x0002, 1000, out result); }
          finally { System.Runtime.InteropServices.Marshal.FreeHGlobal(value); }
        }
"@ | Out-Null
    }
    [TezzNativeInstaller.EnvBroadcast]::Notify()
  } catch {
  }
}

function Remove-Install {
  Write-Host "Uninstalling TezzNative..." -ForegroundColor Yellow
  Purge-LegacyEnvironment

  $primaryBin = Join-Path $InstallDir "bin"
  $userBin = Join-Path $HOME "bin"

  # Remove shims from all locations
  foreach ($dir in @($primaryBin, $userBin)) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "tezz.cmd")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "tezz.ps1")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "tezzc.cmd")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "tezz.exe")
  }

  # Remove install directory
  if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
  }

  # Remove PATH entries from User and Machine
  Remove-PathEntry -Scope "User" -PathValue $primaryBin
  Remove-PathEntry -Scope "Machine" -PathValue $primaryBin
  Remove-PathEntry -Scope "User" -PathValue (Join-Path $SdkDir "bin")
  Remove-PathEntry -Scope "Machine" -PathValue (Join-Path $SdkDir "bin")

  # Remove TEZZ_SDK_ROOT
  [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $null, "User")
  if (Test-IsAdmin) {
    [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $null, "Machine")
  }

  # Remove Control Panel Uninstall registration
  Remove-Item -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue

  # Clean current session environment
  $env:TEZZ_SDK_ROOT = $null
  if ($env:PATH) {
    $parts = $env:PATH.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
    $kept = @()
    foreach ($part in $parts) {
      if ($part.TrimEnd('\', '/') -ine $primaryBin.TrimEnd('\', '/') -and $part -notmatch '(?i)tezz') {
        $kept += $part
      }
    }
    $env:PATH = ($kept -join ';')
  }

  Write-Host "TezzNative has been completely uninstalled from $InstallDir" -ForegroundColor Green
}

function Normalize-SdkLayout {
  param([string]$SdkRoot = $SdkDir)
  $binDir = Join-Path $SdkRoot "bin"
  if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
  }

  # Remove any broken tezz.exe in binDir or SdkDir
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $binDir "tezz.exe")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $SdkRoot "tezz.exe")

  $preferred = Join-Path $binDir "tezzc-windows-x64.exe"
  $armPreferred = Join-Path $binDir "tezzc-windows-arm64.exe"
  $fromBin = Join-Path $binDir "tezzc.exe"
  $fromBuild = Join-Path (Join-Path $SdkRoot "build") "tezzc.exe"
  $fromRoot = Join-Path $SdkRoot "tezzc.exe"

  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path $armPreferred)) {
    Copy-Item -Force $armPreferred $fromBin
  } elseif (Test-Path $preferred) {
    Copy-Item -Force $preferred $fromBin
  } elseif (Test-Path $fromBuild) {
    Copy-Item -Force $fromBuild $fromBin
  } elseif (Test-Path $fromRoot) {
    Copy-Item -Force $fromRoot $fromBin
  }

  if (-not (Test-Path $preferred) -and (Test-Path $fromBin)) {
    Copy-Item -Force $fromBin $preferred
  }

  # Ensure root launchers and bin launchers are synchronized
  foreach ($launcher in @("tezz.cmd", "tezz.ps1", "tezz")) {
    $rootLauncher = Join-Path $SdkRoot $launcher
    $binLauncher = Join-Path $binDir $launcher
    if (-not (Test-Path $rootLauncher) -and (Test-Path $binLauncher)) {
      Copy-Item -Force $binLauncher $rootLauncher
    }
    if (-not (Test-Path $binLauncher) -and (Test-Path $rootLauncher)) {
      Copy-Item -Force $rootLauncher $binLauncher
    }
  }
}

function Assert-SdkIntegrity {
  param([string]$SdkRoot = $SdkDir)
  $required = @(
    (Join-Path $SdkRoot "tezz.cmd"),
    (Join-Path $SdkRoot "tezz.ps1"),
    (Join-Path $SdkRoot "tezz.mod"),
    (Join-Path (Join-Path $SdkRoot "tools") "tezz.tn"),
    (Join-Path (Join-Path $SdkRoot "lib") "std.tn"),
    (Join-Path (Join-Path $SdkRoot "lib") "io.tn"),
    (Join-Path (Join-Path $SdkRoot "bin") "tezzc-windows-x64.exe")
  )
  $missing = @()
  foreach ($item in $required) {
    if (-not (Test-Path $item)) { $missing += $item }
  }
  if ($missing.Count -gt 0) {
    throw "SDK integrity check failed. Missing: $($missing -join ', ')"
  }
}

function Install-Shims {
  $primaryBin = Join-Path $InstallDir "bin"
  $userBin = Join-Path $HOME "bin"
  if (-not (Test-Path $primaryBin)) {
    New-Item -ItemType Directory -Force -Path $primaryBin | Out-Null
  }
  if (-not (Test-Path $userBin)) {
    New-Item -ItemType Directory -Force -Path $userBin | Out-Null
  }

  $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-x64.exe"
  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path (Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-arm64.exe"))) {
    $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-arm64.exe"
  } elseif (Test-Path (Join-Path (Join-Path $SdkDir "bin") "tezzc.exe")) {
    $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc.exe"
  }

  $cmdShim = "@echo off`r`nset `"TEZZ_SDK_ROOT=$SdkDir`"`r`nset `"TEZZC=$tezzcExe`"`r`n`"$SdkDir\tezz.cmd`" %*`r`n"
  $psShim = "`$env:TEZZ_SDK_ROOT = `"$SdkDir`"`r`n`$env:TEZZC = `"$tezzcExe`"`r`n& `"$SdkDir\tezz.ps1`" @args`r`n"
  $tezzcShim = "@echo off`r`n`"$tezzcExe`" %*`r`n"

  foreach ($dir in @($primaryBin, $userBin)) {
    Set-Content -Path (Join-Path $dir "tezz.cmd") -Value $cmdShim -Encoding ASCII
    Set-Content -Path (Join-Path $dir "tezz.ps1") -Value $psShim -Encoding ASCII
    Set-Content -Path (Join-Path $dir "tezzc.cmd") -Value $tezzcShim -Encoding ASCII
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "tezz.exe")
  }

  Configure-Env -BinDir $primaryBin -SdkRoot $SdkDir
  if ($userBin -ne $primaryBin) {
    Configure-Env -BinDir $userBin -SdkRoot $SdkDir
  }
  return $primaryBin
}

function Write-OfflineUninstaller {
  $uninstallerPs1 = Join-Path $InstallDir "uninstall.ps1"
  $uninstallerCmd = Join-Path $InstallDir "uninstall.cmd"

  $ps1Content = @"
# TezzNative Offline Uninstaller
`$InstallDir = Split-Path -Parent `$MyInvocation.MyCommand.Path
`$installerScript = Join-Path `$InstallDir "sdk\install.ps1"
if (Test-Path `$installerScript) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `$installerScript -Mode uninstall
} else {
  # Standalone fallback removal
  Write-Host "Removing TezzNative from `$InstallDir..." -ForegroundColor Yellow
  `$binDir = Join-Path `$InstallDir "bin"
  `$userBin = Join-Path `$HOME "bin"
  foreach (`$d in @(`$binDir, `$userBin)) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path `$d "tezz.cmd")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path `$d "tezz.ps1")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path `$d "tezzc.cmd")
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path `$d "tezz.exe")
  }
  Remove-Item -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -Recurse -Force `$InstallDir -ErrorAction SilentlyContinue
  Write-Host "TezzNative uninstalled successfully." -ForegroundColor Green
}
"@

  $cmdContent = @"
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
"@

  Set-Content -Path $uninstallerPs1 -Value $ps1Content -Encoding ASCII
  Set-Content -Path $uninstallerCmd -Value $cmdContent -Encoding ASCII
}

function Smoke-TestInstall {
  param(
    [string]$BinDir,
    [string]$SdkRoot = $SdkDir,
    [switch]$SkipShimCheck
  )
  $tezzc = Join-Path $SdkRoot "bin\tezzc-windows-x64.exe"
  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path (Join-Path $SdkRoot "bin\tezzc-windows-arm64.exe"))) {
    $tezzc = Join-Path $SdkRoot "bin\tezzc-windows-arm64.exe"
  } elseif (Test-Path (Join-Path $SdkRoot "bin\tezzc.exe")) {
    $tezzc = Join-Path $SdkRoot "bin\tezzc.exe"
  }

  $probe = Join-Path $env:TEMP ("tezznative_smoke_" + [guid]::NewGuid().ToString() + ".tn")
  $probeExe = Join-Path $env:TEMP ("tezznative_smoke_" + [guid]::NewGuid().ToString() + ".exe")
  @"
fn main() -> int:
  say "tezz-smoke-ok"
  ret 0
"@ | Set-Content -Path $probe -Encoding ASCII

  $env:PATH = "$SdkRoot\bin;$SdkRoot\lib;$env:PATH"
  & $tezzc buildexe $probe $probeExe | Out-Null
  $buildOk = (Test-Path $probeExe)
  Remove-Item -Force -ErrorAction SilentlyContinue $probe, $probeExe

  if (-not $buildOk) {
    throw "Smoke test failed: tezzc buildexe"
  }

  $shimCmd = Join-Path $BinDir "tezz.cmd"
  if (-not $SkipShimCheck -and (Test-Path $shimCmd)) {
    $verOut = & $shimCmd --version
    if ($LASTEXITCODE -ne 0 -or ($verOut -notmatch "tezz\s+\d+\.\d+")) {
      throw "Smoke test failed: tezz --version returned exit code $($LASTEXITCODE): $verOut"
    }
  }
}

function Get-ExpectedSha256 {
  param([string]$FileName)

  $shaPath = Join-Path $env:TEMP ("$FileName-" + [guid]::NewGuid().ToString() + ".sha256")
  try {
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 30 -Uri "$BaseUrl/$FileName.sha256?nocache=$CacheBust" -OutFile $shaPath
    $raw = (Get-Content -Path $shaPath -Raw).Trim()
    $parts = $raw -split '\s+'
    if ($parts.Count -lt 1 -or $parts[0] -notmatch '^[0-9A-Fa-f]{64}$') {
      throw "Invalid checksum file for $FileName"
    }
    return $parts[0].ToUpperInvariant()
  } finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $shaPath
  }
}

function Assert-ArchiveChecksum {
  param(
    [string]$Path,
    [string]$FileName
  )

  $expected = Get-ExpectedSha256 -FileName $FileName
  $actual = (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToUpperInvariant()
  if ($actual -ne $expected) {
    throw "Checksum mismatch for $FileName. Expected $expected but got $actual. Install stopped before extraction."
  }
  Write-Host "Verified SHA-256: $actual"
}

function Install-Payload {
  param([string]$RemoteVersion)

  $sourceUri = $null
  if (-not [Uri]::TryCreate($BaseUrl, [UriKind]::Absolute, [ref]$sourceUri) -or $sourceUri.Scheme -ne "https" -or $sourceUri.UserInfo -or $BaseUrl -match '\s') {
    throw "SDK downloads require a valid HTTPS source URL without credentials or whitespace."
  }

  if ($InstallScope -eq "system" -and -not (Test-IsAdmin)) {
    throw "System install requires Administrator PowerShell. Re-run as admin or set TEZZ_INSTALL_SCOPE=user."
  }

  $installFullPath = [System.IO.Path]::GetFullPath($InstallDir).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
  $driveRoot = [System.IO.Path]::GetPathRoot($installFullPath).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
  if (-not $installFullPath -or $installFullPath -eq $driveRoot) {
    throw "Choose a dedicated folder for TezzNative; a drive root cannot be used as the install location."
  }
  if (Test-Path $InstallDir) {
    if (-not (Test-Path $InstallDir -PathType Container)) {
      throw "The install location exists and is not a folder: $InstallDir"
    }
    $hasInstallFiles = (Test-Path (Join-Path $SdkDir "tezz.mod")) -or (Test-Path (Join-Path $SdkDir "version.json"))
    $hasExistingFiles = $null -ne (Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction Stop | Select-Object -First 1)
    if ($hasExistingFiles -and -not $hasInstallFiles) {
      throw "The folder $InstallDir contains files but is not a recognized TezzNative install. Choose an empty folder or an existing TezzNative install."
    }
  }

  # Purge any legacy installations and corrupt PATH entries first
  Purge-LegacyEnvironment

  $installParent = Split-Path -Parent $InstallDir
  if (-not $installParent) { $installParent = $InstallDir }
  New-Item -ItemType Directory -Force -Path $installParent | Out-Null
  $stageInstallDir = Join-Path $installParent (".TezzNative-stage-" + [guid]::NewGuid().ToString("N"))
  $backupInstallDir = Join-Path $installParent (".TezzNative-backup-" + [guid]::NewGuid().ToString("N"))
  $stageSdkDir = Join-Path $stageInstallDir "sdk"
  $hadPreviousInstall = Test-Path $InstallDir
  $promoted = $false
  $committed = $false
  try {
    Write-Output "Downloading TezzNative SDK archive..."
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 600 -Uri "$BaseUrl/tezznative-sdk.zip?nocache=$CacheBust" -OutFile $ZipPath
    Write-Output "Verifying the SDK archive checksum..."
    Assert-ArchiveChecksum -Path $ZipPath -FileName "tezznative-sdk.zip"

    Write-Output "Extracting SDK into a temporary install folder..."
    New-Item -ItemType Directory -Force -Path $stageSdkDir | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $stageSdkDir -Force
    Normalize-SdkLayout -SdkRoot $stageSdkDir
    Assert-SdkIntegrity -SdkRoot $stageSdkDir
    Write-Output "Checking the downloaded compiler before switching the install..."
    Smoke-TestInstall -BinDir (Join-Path $stageInstallDir "bin") -SdkRoot $stageSdkDir -SkipShimCheck

    if ($hadPreviousInstall) {
      Move-Item -Path $InstallDir -Destination $backupInstallDir
    }
    try {
      Move-Item -Path $stageInstallDir -Destination $InstallDir
      $promoted = $true
    } catch {
      if ($hadPreviousInstall -and (Test-Path $backupInstallDir)) {
        Move-Item -Path $backupInstallDir -Destination $InstallDir
      }
      throw
    }
    Remove-Item -Force $ZipPath

    Write-Output "Adding TezzNative commands and PATH entries..."
    $binDir = Install-Shims
    Write-OfflineUninstaller
    $committed = $true
  } catch {
    $committed = $false
    if ($promoted) {
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $InstallDir
      if ($hadPreviousInstall -and (Test-Path $backupInstallDir)) {
        try {
          Move-Item -Path $backupInstallDir -Destination $InstallDir
        } catch {
          throw "Install failed and the previous SDK could not be restored automatically. It remains at $backupInstallDir. Original error: $($_.Exception.Message)"
        }
      }
    }
    throw
  } finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $ZipPath
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $stageInstallDir
    if ($committed -and (Test-Path $backupInstallDir)) {
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $backupInstallDir
    }
  }

  # Determine display version
  $effectiveVer = if ($RemoteVersion) { $RemoteVersion } else { Get-LocalVersion }
  if (-not $effectiveVer) { $effectiveVer = "1.0.0" }

  # Register in Windows Control Panel Programs (Installed Apps)
  try {
    $uninstallKey = if ($InstallScope -eq "system" -and (Test-IsAdmin)) { "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" } else { "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" }
    if (-not (Test-Path $uninstallKey)) {
      New-Item -Path $uninstallKey -Force | Out-Null
    }
    Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value "TezzNative Programming Language SDK v$effectiveVer"
    Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value $effectiveVer
    Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "TezzCorp Pvt Ltd. (Created by Rohit Pathak)"
    Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $InstallDir
    Set-ItemProperty -Path $uninstallKey -Name "DisplayIcon" -Value "$binDir\tezz.cmd,0"
    Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "`"$InstallDir\uninstall.cmd`""
    Set-ItemProperty -Path $uninstallKey -Name "URLInfoAbout" -Value "https://tezznative.org"
    Set-ItemProperty -Path $uninstallKey -Name "HelpLink" -Value "https://tezznative.org/docs/"
    Set-ItemProperty -Path $uninstallKey -Name "EstimatedSize" -Value 120000 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1 -Type DWord
  } catch {
  }

  Write-Host ""
  Write-Host "TezzNative installed successfully." -ForegroundColor Green
  Write-Host "Created by Rohit Pathak | TezzCorp Pvt Ltd." -ForegroundColor Cyan
  Write-Host "Install root: $InstallDir"
  Write-Host "SDK root:     $SdkDir"
  Write-Host "Bin dir:      $binDir"
  Write-Host "Version:      $effectiveVer"
  Write-Host ""
  Write-Host "Next steps:"
  Write-Host "  tezz --version"
  Write-Host "  tezz init my-project"
  Write-Host "  tezz run hello.tn"
  Write-Host ""
  Write-Host "Direct Script Execution:"
  Write-Host "  1. Add shebang: #!/usr/bin/env tezz"
  Write-Host "  2. Run from WSL/Git Bash or call: tezz script.tn"
}

$Mode = $Mode.ToLowerInvariant()
if ($Mode -eq "install" -or $Mode -eq "update" -or $Mode -eq "reinstall") {
  Write-Output "Checking the official TezzNative release..."
}
$RemoteVersion = Get-RemoteVersion
$LocalVersion = Get-LocalVersion

switch ($Mode) {
  "check" {
    Write-Host "Local version:  " $(if ($LocalVersion) { $LocalVersion } else { "not-installed" })
    Write-Host "Remote version: " $(if ($RemoteVersion) { $RemoteVersion } else { "unknown" })
    if ($LocalVersion -and $RemoteVersion -and $LocalVersion -eq $RemoteVersion) {
      Write-Host "Status: up-to-date"
    } elseif ($LocalVersion) {
      Write-Host "Status: update-available"
    } else {
      Write-Host "Status: not-installed"
    }
    Send-InstallEvent -Status "check" -Message "local=$LocalVersion remote=$RemoteVersion"
  }
  "uninstall" {
    Remove-Install
    Send-InstallEvent -Status "uninstall" -Message "removed"
  }
  "reinstall" {
    Remove-Install
    Install-Payload -RemoteVersion $RemoteVersion
    Send-InstallEvent -Status "reinstall" -Message "completed"
  }
  "update" {
    if (-not $LocalVersion) {
      Install-Payload -RemoteVersion $RemoteVersion
      Send-InstallEvent -Status "install" -Message "installed from update mode"
    } elseif ($RemoteVersion -and $LocalVersion -eq $RemoteVersion) {
      Write-Host "TezzNative is already up-to-date ($LocalVersion)."
      Send-InstallEvent -Status "check" -Message "already up-to-date"
    } else {
      Install-Payload -RemoteVersion $RemoteVersion
      Send-InstallEvent -Status "update" -Message "updated"
    }
  }
  default {
    Install-Payload -RemoteVersion $RemoteVersion
    Send-InstallEvent -Status "install" -Message "completed"
  }
}

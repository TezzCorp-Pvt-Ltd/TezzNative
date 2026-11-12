param(
  [string]$BaseUrl = "",
  [string]$PortalUrl = ""
)

$ErrorActionPreference = "Stop"
if (-not $IsWindows -and $env:OS -ne "Windows_NT") {
  throw "The TezzNative Windows installer must be run on Windows."
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

if (-not $BaseUrl) {
  $BaseUrl = if ($env:TEZZ_INSTALL_BASE) { $env:TEZZ_INSTALL_BASE } else { "https://tezznative.org/download" }
}
if (-not $PortalUrl) {
  $PortalUrl = if ($env:TEZZ_PORTAL_BASE) { $env:TEZZ_PORTAL_BASE.TrimEnd("/") } else { "https://tezznative.org" }
}

$script:process = $null
$script:timer = $null
$script:outputLine = 0
$script:logPath = Join-Path $env:TEMP ("tezznative-installer-" + [guid]::NewGuid().ToString("N") + ".log")
$script:errorPath = Join-Path $env:TEMP ("tezznative-installer-" + [guid]::NewGuid().ToString("N") + ".err")
$script:downloadedCli = $false
$script:cliPath = if ($PSScriptRoot) { Join-Path $PSScriptRoot "install.ps1" } else { "" }
$script:completed = $false

$form = New-Object System.Windows.Forms.Form
$form.Text = "TezzNative Setup"
$form.StartPosition = "CenterScreen"
$form.ClientSize = New-Object System.Drawing.Size(760, 590)
$form.MinimumSize = New-Object System.Drawing.Size(760, 590)
$form.MaximumSize = New-Object System.Drawing.Size(760, 590)
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(246, 247, 251)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

$hero = New-Object System.Windows.Forms.Panel
$hero.Location = New-Object System.Drawing.Point(0, 0)
$hero.Size = New-Object System.Drawing.Size(760, 132)
$hero.BackColor = [System.Drawing.Color]::FromArgb(92, 74, 222)
$form.Controls.Add($hero)

$title = New-Object System.Windows.Forms.Label
$title.Text = "Install TezzNative"
$title.Location = New-Object System.Drawing.Point(32, 25)
$title.Size = New-Object System.Drawing.Size(680, 42)
$title.Font = New-Object System.Drawing.Font("Segoe UI Semibold", 23, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = [System.Drawing.Color]::White
$hero.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = "A fast native language toolchain, ready for your next project."
$subtitle.Location = New-Object System.Drawing.Point(36, 77)
$subtitle.Size = New-Object System.Drawing.Size(675, 28)
$subtitle.Font = New-Object System.Drawing.Font("Segoe UI", 11)
$subtitle.ForeColor = [System.Drawing.Color]::FromArgb(230, 228, 255)
$hero.Controls.Add($subtitle)

$lblFolder = New-Object System.Windows.Forms.Label
$lblFolder.Text = "Install location"
$lblFolder.Location = New-Object System.Drawing.Point(36, 158)
$lblFolder.Size = New-Object System.Drawing.Size(250, 24)
$lblFolder.Font = New-Object System.Drawing.Font("Segoe UI Semibold", 10, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($lblFolder)

$txtFolder = New-Object System.Windows.Forms.TextBox
$txtFolder.Location = New-Object System.Drawing.Point(36, 187)
$txtFolder.Size = New-Object System.Drawing.Size(570, 30)
$txtFolder.Text = Join-Path $HOME "TezzNative"
$txtFolder.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$form.Controls.Add($txtFolder)

$btnBrowse = New-Object System.Windows.Forms.Button
$btnBrowse.Text = "Browse..."
$btnBrowse.Location = New-Object System.Drawing.Point(616, 185)
$btnBrowse.Size = New-Object System.Drawing.Size(108, 34)
$btnBrowse.FlatStyle = "System"
$form.Controls.Add($btnBrowse)
$btnBrowse.Add_Click({
  $picker = New-Object System.Windows.Forms.FolderBrowserDialog
  $picker.Description = "Choose where TezzNative will be installed"
  $picker.SelectedPath = $txtFolder.Text
  $picker.ShowNewFolderButton = $true
  if ($picker.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
    $txtFolder.Text = $picker.SelectedPath
  }
})

$lblSource = New-Object System.Windows.Forms.Label
$lblSource.Text = "Download source"
$lblSource.Location = New-Object System.Drawing.Point(36, 237)
$lblSource.Size = New-Object System.Drawing.Size(250, 24)
$lblSource.Font = New-Object System.Drawing.Font("Segoe UI Semibold", 10, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($lblSource)

$txtSource = New-Object System.Windows.Forms.TextBox
$txtSource.Location = New-Object System.Drawing.Point(36, 266)
$txtSource.Size = New-Object System.Drawing.Size(688, 30)
$txtSource.Text = $BaseUrl.TrimEnd("/")
$txtSource.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$form.Controls.Add($txtSource)

$chkPath = New-Object System.Windows.Forms.CheckBox
$chkPath.Text = "Add TezzNative to my user PATH"
$chkPath.Checked = $true
$chkPath.Location = New-Object System.Drawing.Point(36, 315)
$chkPath.Size = New-Object System.Drawing.Size(350, 27)
$chkPath.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$form.Controls.Add($chkPath)

$lblHint = New-Object System.Windows.Forms.Label
$lblHint.Text = "Your current terminal may need to be reopened before the PATH change is visible."
$lblHint.Location = New-Object System.Drawing.Point(58, 343)
$lblHint.Size = New-Object System.Drawing.Size(660, 24)
$lblHint.ForeColor = [System.Drawing.Color]::FromArgb(100, 105, 120)
$lblHint.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$form.Controls.Add($lblHint)

$progress = New-Object System.Windows.Forms.ProgressBar
$progress.Location = New-Object System.Drawing.Point(36, 388)
$progress.Size = New-Object System.Drawing.Size(688, 13)
$progress.Style = "Blocks"
$progress.Visible = $false
$form.Controls.Add($progress)

$lblStatus = New-Object System.Windows.Forms.Label
$lblStatus.Text = "Choose an install folder, then select Install."
$lblStatus.Location = New-Object System.Drawing.Point(36, 409)
$lblStatus.Size = New-Object System.Drawing.Size(688, 25)
$lblStatus.ForeColor = [System.Drawing.Color]::FromArgb(70, 75, 90)
$form.Controls.Add($lblStatus)

$log = New-Object System.Windows.Forms.TextBox
$log.Location = New-Object System.Drawing.Point(36, 438)
$log.Size = New-Object System.Drawing.Size(688, 78)
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = "Vertical"
$log.BackColor = [System.Drawing.Color]::White
$log.Font = New-Object System.Drawing.Font("Consolas", 8.5)
$log.Visible = $false
$form.Controls.Add($log)

$btnInstall = New-Object System.Windows.Forms.Button
$btnInstall.Text = "Install TezzNative"
$btnInstall.Location = New-Object System.Drawing.Point(490, 535)
$btnInstall.Size = New-Object System.Drawing.Size(150, 38)
$btnInstall.BackColor = [System.Drawing.Color]::FromArgb(92, 74, 222)
$btnInstall.ForeColor = [System.Drawing.Color]::White
$btnInstall.FlatStyle = "Flat"
$btnInstall.FlatAppearance.BorderSize = 0
$btnInstall.Font = New-Object System.Drawing.Font("Segoe UI Semibold", 10, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($btnInstall)

$btnClose = New-Object System.Windows.Forms.Button
$btnClose.Text = "Cancel"
$btnClose.Location = New-Object System.Drawing.Point(650, 535)
$btnClose.Size = New-Object System.Drawing.Size(74, 38)
$btnClose.FlatStyle = "System"
$form.Controls.Add($btnClose)
$btnClose.Add_Click({
  if ($script:process -and -not $script:process.HasExited) {
    [System.Windows.Forms.MessageBox]::Show("Installation is running. Please wait for it to finish before closing this window.", "TezzNative Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    return
  }
  $form.Close()
})

$btnInstall.Add_Click({
  if ($script:completed) { $form.Close(); return }

  $target = $txtFolder.Text.Trim()
  if (-not $target) {
    [System.Windows.Forms.MessageBox]::Show("Enter an install location.", "TezzNative Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
    return
  }
  try { $null = [System.IO.Path]::GetFullPath($target) } catch {
    [System.Windows.Forms.MessageBox]::Show("The install location is not a valid Windows path.", "TezzNative Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
    return
  }
  $source = $txtSource.Text.Trim().TrimEnd("/")
  $uri = $null
  if (-not [Uri]::TryCreate($source, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -ne "https" -or $uri.UserInfo) {
    [System.Windows.Forms.MessageBox]::Show("Use a valid HTTPS download source URL.", "TezzNative Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
    return
  }

  try {
    if (-not $script:cliPath -or -not (Test-Path $script:cliPath)) {
      $portalUri = $null
      if (-not [Uri]::TryCreate($PortalUrl, [UriKind]::Absolute, [ref]$portalUri) -or $portalUri.Scheme -ne "https" -or $portalUri.UserInfo) {
        throw "The installer script source must use HTTPS."
      }
      [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12
      $script:cliPath = Join-Path $env:TEMP ("tezznative-install-" + [guid]::NewGuid().ToString("N") + ".ps1")
      Invoke-WebRequest -UseBasicParsing -TimeoutSec 60 -Uri "$PortalUrl/install.ps1?nocache=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" -OutFile $script:cliPath
      if ((Get-Item $script:cliPath).Length -lt 4096) { throw "Downloaded installer script is unexpectedly small." }
      $script:downloadedCli = $true
    }

    $txtFolder.Enabled = $false
    $txtSource.Enabled = $false
    $btnBrowse.Enabled = $false
    $chkPath.Enabled = $false
    $btnInstall.Enabled = $false
    $progress.Style = "Marquee"
    $progress.MarqueeAnimationSpeed = 24
    $progress.Visible = $true
    $log.Visible = $true
    $form.ClientSize = New-Object System.Drawing.Size(760, 590)
    $lblStatus.Text = "Downloading and verifying the TezzNative SDK..."

    $oldEnv = @{}
    foreach ($key in @("TEZZ_INSTALL_BASE", "TEZZ_INSTALL_DIR", "TEZZ_INSTALL_SCOPE", "TEZZ_INSTALL_AUTO_PATH")) {
      $oldEnv[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    }
    $env:TEZZ_INSTALL_BASE = $source
    $env:TEZZ_INSTALL_DIR = $target
    $env:TEZZ_INSTALL_SCOPE = "user"
    $env:TEZZ_INSTALL_AUTO_PATH = if ($chkPath.Checked) { "user" } else { "none" }

    $powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
    $argumentLine = "-NoProfile -ExecutionPolicy Bypass -File `"$script:cliPath`" -Mode install"
    $script:outputLine = 0
    Remove-Item -Force -ErrorAction SilentlyContinue $script:logPath, $script:errorPath
    $script:process = Start-Process -FilePath $powershell -ArgumentList $argumentLine -PassThru -WindowStyle Hidden -RedirectStandardOutput $script:logPath -RedirectStandardError $script:errorPath
    foreach ($key in $oldEnv.Keys) {
      [Environment]::SetEnvironmentVariable($key, $oldEnv[$key], "Process")
    }

    $script:timer = New-Object System.Windows.Forms.Timer
    $script:timer.Interval = 500
    $script:timer.Add_Tick({
      if (Test-Path $script:logPath) {
        $lines = @(Get-Content -Path $script:logPath -ErrorAction SilentlyContinue)
        while ($script:outputLine -lt $lines.Count) {
          $log.AppendText($lines[$script:outputLine] + [Environment]::NewLine)
          $script:outputLine++
        }
      }
      if ($script:process -and $script:process.HasExited) {
        $script:timer.Stop()
        if (Test-Path $script:errorPath) {
          $errors = @(Get-Content -Path $script:errorPath -ErrorAction SilentlyContinue)
          foreach ($line in $errors | Select-Object -Last 12) {
            if ($line.Trim()) { $log.AppendText($line + [Environment]::NewLine) }
          }
        }
        $progress.Style = "Blocks"
        $progress.Value = 100
        $btnInstall.Enabled = $true
        $btnInstall.Text = "Finish"
        $script:completed = $true
        if ($script:process.ExitCode -eq 0) {
          $lblStatus.Text = "TezzNative is installed. Open a new terminal and run: tezz --version"
          $lblStatus.ForeColor = [System.Drawing.Color]::FromArgb(24, 135, 92)
        } else {
          $lblStatus.Text = "Installation failed. Review the log above, then try again."
          $lblStatus.ForeColor = [System.Drawing.Color]::FromArgb(190, 55, 55)
          $script:completed = $false
          $btnInstall.Text = "Retry"
          $txtFolder.Enabled = $true
          $txtSource.Enabled = $true
          $btnBrowse.Enabled = $true
          $chkPath.Enabled = $true
        }
        $btnClose.Text = "Close"
        if ($script:downloadedCli) { Remove-Item -Force -ErrorAction SilentlyContinue $script:cliPath }
        Remove-Item -Force -ErrorAction SilentlyContinue $script:logPath, $script:errorPath
      }
    })
    $script:timer.Start()
  } catch {
    $btnInstall.Enabled = $true
    $txtFolder.Enabled = $true
    $txtSource.Enabled = $true
    $btnBrowse.Enabled = $true
    $chkPath.Enabled = $true
    $progress.Style = "Blocks"
    [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "TezzNative Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
  }
})

$form.Add_FormClosing({
  param($sender, $eventArgs)
  if ($script:process -and -not $script:process.HasExited) {
    $eventArgs.Cancel = $true
  }
})

[void]$form.ShowDialog()
if ($script:downloadedCli -and (Test-Path $script:cliPath)) {
  Remove-Item -Force -ErrorAction SilentlyContinue $script:cliPath
}
Remove-Item -Force -ErrorAction SilentlyContinue $script:logPath, $script:errorPath

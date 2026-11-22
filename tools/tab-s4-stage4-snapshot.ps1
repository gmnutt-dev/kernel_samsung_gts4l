param(
  [Parameter(Mandatory=$true)]
  [ValidateSet('idle','city-loaded','recents','launcher','pre-sleep','post-wake','post-reboot')]
  [string]$Phase
)
$ErrorActionPreference = 'Stop'
$adb = Join-Path $PSScriptRoot 'adb.exe'
$script = Join-Path $PSScriptRoot 'tab-s4-stage4-snapshot.sh'
if (-not (Test-Path $adb)) { throw 'Extract beside adb.exe in D:\platform-tools' }
if (-not (Test-Path $script)) { throw "Missing $script" }
& $adb push $script /data/local/tmp/tab-s4-stage4-snapshot.sh
if ($LASTEXITCODE -ne 0) { throw 'adb push failed' }
$output = & $adb shell "su -c 'sh /data/local/tmp/tab-s4-stage4-snapshot.sh $Phase'" 2>&1
$output | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw 'Device capture failed' }
$line = $output | Where-Object { $_ -match '^CAPTURE_OK=(.+\.tar\.gz)\s*$' } | Select-Object -Last 1
if (-not $line) { throw 'No completed capture archive found' }
$remote = ([regex]::Match($line,'^CAPTURE_OK=(.+\.tar\.gz)\s*$')).Groups[1].Value.Trim()
$local = Join-Path $PSScriptRoot (Split-Path $remote -Leaf)
& $adb pull $remote $local
if ($LASTEXITCODE -ne 0) { throw "Pull failed; archive remains at $remote" }
Write-Host "Saved $local" -ForegroundColor Green

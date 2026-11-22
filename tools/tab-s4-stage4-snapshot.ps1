param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('idle','city-loaded','recents','launcher','pre-sleep','post-wake','post-reboot')]
    [string]$Phase
)
$ErrorActionPreference = 'Stop'
Write-Host 'Stage 4 FAST V3 (nonblocking power counters only; per-read progress)' -ForegroundColor Cyan
$adb = Join-Path $PSScriptRoot 'adb.exe'
$script = Join-Path $PSScriptRoot 'tab-s4-stage4-snapshot.sh'
if (-not (Test-Path $adb)) { throw 'adb.exe missing: extract kit directly into D:\platform-tools' }
if (-not (Test-Path $script)) { throw "Missing $script" }
& $adb push $script /data/local/tmp/tab-s4-stage4-snapshot.sh
if ($LASTEXITCODE -ne 0) { throw 'ADB push failed' }

# Stream progress immediately (V1 buffered the entire capture until completion).
$lines = New-Object 'System.Collections.Generic.List[string]'
& $adb shell "su -c 'sh /data/local/tmp/tab-s4-stage4-snapshot.sh $Phase'" 2>&1 | ForEach-Object {
    $s = [string]$_
    Write-Host $s
    $lines.Add($s)
}
if ($LASTEXITCODE -ne 0) { throw 'Device capture failed (see streamed progress above)' }
$finished = $lines | Where-Object { $_ -match '^CAPTURE_OK=(.+\.tar\.gz)\s*$' } | Select-Object -Last 1
if (-not $finished) { throw 'No completed archive path found in device output' }
$remote = ([regex]::Match($finished, '^CAPTURE_OK=(.+\.tar\.gz)\s*$')).Groups[1].Value.Trim()
$local = Join-Path $PSScriptRoot (Split-Path $remote -Leaf)
& $adb pull $remote $local
if ($LASTEXITCODE -ne 0) { throw "Pull failed; archive remains on device: $remote" }
Write-Host "Saved $local" -ForegroundColor Green

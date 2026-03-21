$ErrorActionPreference = "Stop"

function Get-ChangedFiles([string[]]$argsForDiff) {
    $output = & git diff @argsForDiff --name-only
    if (-not $output) { return @() }
    return $output -split "`r?`n" | Where-Object { $_ -and $_.Trim().Length -gt 0 }
}

$trackedTargets = @(
    "tolua.c",
    "Plugins/Android/libs/arm64-v8a/libtolua.so"
)

$logFile = "docs/arm64_fr2_change_log.md"

$staged = Get-ChangedFiles @("--cached")
$unstaged = Get-ChangedFiles @()

$needsLog = @($staged + $unstaged | Where-Object { $trackedTargets -contains $_ }).Count -gt 0
$hasLogUpdate = @($staged + $unstaged | Where-Object { $_ -eq $logFile }).Count -gt 0

if (-not $needsLog) {
    Write-Host "[OK] No arm64 FR2 core/binary changes detected."
    exit 0
}

if (-not $hasLogUpdate) {
    Write-Host "[FAIL] arm64 FR2 core/binary changed but log not updated: $logFile"
    Write-Host "       Please append an entry before commit."
    exit 2
}

Write-Host "[OK] arm64 FR2 changes and log update are both present."
exit 0

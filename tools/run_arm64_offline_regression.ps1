param(
    [string]$BytecodeDir = "D:\Games\SSWS_HG_124\gamedata\modcache\SSWS_HG\lua\luajit",
    [string]$OutDir = "tmp\offline_regression",
    [switch]$SkipBuild,
    [switch]$UpdateBaseline
)

$ErrorActionPreference = "Stop"

function Convert-ToWslPath([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $unix = ($full -replace "\\", "/")
    if ($unix -match "^([A-Za-z]):/(.*)$") {
        $drive = $matches[1].ToLowerInvariant()
        $rest = $matches[2]
        return "/mnt/$drive/$rest"
    }
    throw "Unsupported Windows path for WSL conversion: $path"
}

function Invoke-WslBash([string]$command) {
    & wsl bash -lc "$command"
    return $LASTEXITCODE
}

function Get-LogCount([string]$logPath, [string]$pattern) {
    $matches = Select-String -Path $logPath -Pattern $pattern
    if (-not $matches) { return 0 }
    return @($matches).Count
}

$repoRoot = (Resolve-Path ".").Path
$repoRootWsl = Convert-ToWslPath $repoRoot

if (-not (Test-Path $BytecodeDir)) {
    throw "Bytecode directory not found: $BytecodeDir"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outAbs = (Resolve-Path $OutDir).Path

if (-not $SkipBuild) {
    $buildCmd = @(
        "cd '$repoRootWsl'",
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcconv_cli.c tolua.c int64.c uint64.c ./luajit-2.1/src/lj_bc.o ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcconv_cli_wsl_dbg"
    ) -join " && "
    $code = Invoke-WslBash $buildCmd
    if ($code -ne 0) {
        throw "Failed to build tools/bcconv_cli_wsl_dbg (exit=$code)"
    }
}

$targets = @("main.lua", "battle.lua", "migong.lua")
$summary = @()
$failed = $false

foreach ($name in $targets) {
    $inPath = Join-Path $BytecodeDir $name
    if (-not (Test-Path $inPath)) {
        throw "Missing target bytecode: $inPath"
    }

    $outPath = Join-Path $outAbs "$name.fr2.bin"
    $logPath = Join-Path $outAbs "$name.dbg.log"

    $inWsl = Convert-ToWslPath $inPath
    $outWsl = Convert-ToWslPath $outPath
    $logWsl = Convert-ToWslPath $logPath

    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcconv_cli_wsl_dbg '$inWsl' '$outWsl' 1 > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd

    $converted = Get-LogCount $logPath "converted "
    $convFail = Get-LogCount $logPath "bytecode conversion failed"
    $regOverflow = Get-LogCount $logPath "register_overflow"
    $kstrTdup = Get-LogCount $logPath "reject existing FR2 slice: KSTR\+TDUP"
    $tableStyle = Get-LogCount $logPath "GGET/TGET\* func; TGET\* method; MOV arg1; MOV arg2; CALL"
    $copyFallback = Get-LogCount $logPath "copy-fallback"

    if ($code -ne 0 -or $convFail -gt 0 -or $converted -eq 0) {
        $failed = $true
    }
    if ($name -eq "battle.lua" -and $kstrTdup -lt 1) {
        $failed = $true
    }

    $summary += [pscustomobject]@{
        file                = $name
        exit_code           = $code
        converted_lines     = $converted
        conversion_failed   = $convFail
        register_overflow   = $regOverflow
        kstr_tdup_rejects   = $kstrTdup
        table_style_guards  = $tableStyle
        copy_fallback_hits  = $copyFallback
        log_path            = $logPath
        out_path            = $outPath
    }
}

$summaryPath = Join-Path $outAbs "summary.json"
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $summaryPath

$baselinePath = Join-Path $repoRoot "docs\arm64_offline_regression_baseline.json"
if ($UpdateBaseline) {
    $summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $baselinePath
}
elseif (Test-Path $baselinePath) {
    $baseline = Get-Content $baselinePath -Raw | ConvertFrom-Json
    foreach ($row in $summary) {
        $baseRow = $baseline | Where-Object { $_.file -eq $row.file } | Select-Object -First 1
        if ($null -eq $baseRow) {
            $failed = $true
            continue
        }
        if ($row.kstr_tdup_rejects -ne $baseRow.kstr_tdup_rejects -and $row.file -eq "battle.lua") {
            $failed = $true
        }
        if ($row.conversion_failed -ne $baseRow.conversion_failed) {
            $failed = $true
        }
        if ($row.register_overflow -ne $baseRow.register_overflow) {
            $failed = $true
        }
    }
}

Write-Host "Offline regression summary: $summaryPath"
$summary | Format-Table file, exit_code, converted_lines, conversion_failed, register_overflow, kstr_tdup_rejects, copy_fallback_hits

if ($failed) {
    throw "Offline regression failed. Check logs under: $outAbs"
}

Write-Host "[OK] Offline regression passed."

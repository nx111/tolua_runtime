param(
    [string]$BytecodeDir = "D:\Games\SSWS_HG_124\gamedata\modcache\SSWS_HG\lua\luajit",
    [string]$OutDir = "tmp\offline_regression",
    [string]$WslDistro = "",
    [switch]$SkipBuild,
    [switch]$UpdateBaseline
)

$ErrorActionPreference = "Stop"
$script:ResolvedWslDistro = $null

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

function Test-WslShell([string]$distro, [string]$command) {
    if ($distro -and $distro.Trim() -ne "") {
        & wsl -d $distro bash -lc "$command" *> $null
    } else {
        & wsl bash -lc "$command" *> $null
    }
    return ($LASTEXITCODE -eq 0)
}

function Resolve-WslDistro() {
    if ($null -ne $script:ResolvedWslDistro) {
        return $script:ResolvedWslDistro
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    if ($WslDistro -and $WslDistro.Trim() -ne "") {
        $candidates.Add($WslDistro.Trim())
    } else {
        $candidates.Add("")
        $candidates.Add("Ubuntu-24.04")
        $candidates.Add("Ubuntu")
    }

    $seen = @{}
    foreach ($candidate in $candidates) {
        if ($seen.ContainsKey($candidate)) { continue }
        $seen[$candidate] = $true
        if (Test-WslShell $candidate "command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1") {
            $script:ResolvedWslDistro = $candidate
            return $script:ResolvedWslDistro
        }
    }

    throw "No usable WSL distro with gcc+make found. Tried default, Ubuntu-24.04, Ubuntu."
}

function Invoke-WslBash([string]$command) {
    $resolved = Resolve-WslDistro
    if ($resolved -and $resolved.Trim() -ne "") {
        & wsl -d $resolved bash -lc "$command"
    } else {
        & wsl bash -lc "$command"
    }
    return $LASTEXITCODE
}

function Get-LogCount([string]$logPath, [string]$pattern) {
    $matches = Select-String -Path $logPath -Pattern $pattern
    if (-not $matches) { return 0 }
    return @($matches).Count
}

function Test-BattleWorkflowArgShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 202 0 1600 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            hits = 0
            failures = @("bc_dump_proto_wsl failed exit=$code")
        }
    }

    $rows = @{}
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=\d+)?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $pc = [int]$matches['pc']
            $rows[$pc] = [pscustomobject]@{
                pc = $pc
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            }
        }
    }

    $hits = 0
    $fails = New-Object System.Collections.Generic.List[string]

    foreach ($r in $rows.Values | Sort-Object pc) {
        if ($r.op -ne "CALL" -or $r.a -ne 29 -or ($r.c -ne 4 -and $r.c -ne 6)) {
            continue
        }

        $shapeFound = $false
        for ($back = 1; $back -le 12; $back++) {
            $fpc = $r.pc - $back
            if (-not $rows.ContainsKey($fpc) -or -not $rows.ContainsKey($fpc - 1) -or -not $rows.ContainsKey($fpc - 2) -or -not $rows.ContainsKey($fpc - 3)) {
                continue
            }
            $fnew = $rows[$fpc]
            $tdup = $rows[$fpc - 1]
            $kstr = $rows[$fpc - 2]
            $tgets = $rows[$fpc - 3]
            if ($fnew.op -eq "FNEW" -and $fnew.a -eq 32 -and
                $tdup.op -eq "TDUP" -and $tdup.a -eq 31 -and
                $kstr.op -eq "KSTR" -and $kstr.a -eq 30 -and
                $tgets.op -eq "TGETS" -and $tgets.a -eq 29) {
                $shapeFound = $true
                break
            }
        }

        if (-not $shapeFound) { continue }
        $hits++

        $expectedMov = $r.c - 1
        for ($n = 1; $n -le $expectedMov; $n++) {
            $mpc = $r.pc - $n
            if (-not $rows.ContainsKey($mpc)) {
                $fails.Add("pc=$($r.pc) c=$($r.c) missing-mov n=$n")
                break
            }
            $m = $rows[$mpc]
            $expectA = 30 + $n
            $expectD = 29 + $n
            if ($m.op -ne "MOV" -or $m.a -ne $expectA -or $m.d -ne $expectD) {
                $fails.Add("pc=$($r.pc) c=$($r.c) bad-mov n=$n got=$($m.op) A=$($m.a) D=$($m.d)")
                break
            }
        }
    }

    return [pscustomobject]@{
        ok = ($hits -ge 5 -and $fails.Count -eq 0)
        hits = $hits
        failures = @($fails)
    }
}

function Test-BattleDoAllTriggerArgShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 19 79 724 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto19 79..724 failed exit=$code")
        }
    }

    $rows = @{}
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=\d+)?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $pc = [int]$matches['pc']
            $rows[$pc] = [pscustomobject]@{
                pc = $pc
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            }
        }
    }

    $checks = @(
        @{ pc = 718; op = "UGET";  a = 14; b = 0;  c = 1;  d = 1;    tag = "pc718" },
        @{ pc = 719; op = "TGETS"; a = 14; b = 14; c = 30; d = 3614; tag = "pc719" },
        @{ pc = 720; op = "MOV";   a = 16; b = 0;  c = 5;  d = 5;    tag = "pc720" },
        @{ pc = 721; op = "TGETV"; a = 17; b = 9;  c = 13; d = 2317; tag = "pc721" },
        @{ pc = 722; op = "CALL";  a = 14; b = 1;  c = 3;  d = 259;  tag = "pc722" },
        @{ pc = 85;  op = "UGET";  a = 16; b = 0;  c = 1;  d = 1;    tag = "pc085" },
        @{ pc = 86;  op = "TGETS"; a = 16; b = 16; c = 30; d = 4126; tag = "pc086" },
        @{ pc = 87;  op = "MOV";   a = 18; b = 0;  c = 5;  d = 5;    tag = "pc087" },
        @{ pc = 88;  op = "TGETS"; a = 19; b = 11; c = 28; d = 2844; tag = "pc088" },
        @{ pc = 89;  op = "TGETV"; a = 19; b = 19; c = 15; d = 4879; tag = "pc089" },
        @{ pc = 90;  op = "CALL";  a = 16; b = 1;  c = 3;  d = 259;  tag = "pc090" },
        @{ pc = 98;  op = "UGET";  a = 16; b = 0;  c = 1;  d = 1;    tag = "pc098" },
        @{ pc = 99;  op = "TGETS"; a = 16; b = 16; c = 30; d = 4126; tag = "pc099" },
        @{ pc = 100; op = "MOV";   a = 18; b = 0;  c = 5;  d = 5;    tag = "pc100" },
        @{ pc = 101; op = "TGETS"; a = 19; b = 11; c = 31; d = 2847; tag = "pc101" },
        @{ pc = 102; op = "TGETV"; a = 19; b = 19; c = 15; d = 4879; tag = "pc102" },
        @{ pc = 103; op = "CALL";  a = 16; b = 1;  c = 3;  d = 259;  tag = "pc103" }
    )

    $fails = New-Object System.Collections.Generic.List[string]
    foreach ($check in $checks) {
        if (-not $rows.ContainsKey($check.pc)) {
            $fails.Add("$($check.tag) missing")
            continue
        }
        $row = $rows[$check.pc]
        if ($row.op -ne $check.op -or $row.a -ne $check.a -or $row.b -ne $check.b -or $row.c -ne $check.c -or $row.d -ne $check.d) {
            $fails.Add("$($check.tag) got=$($row.op) A=$($row.a) B=$($row.b) C=$($row.c) D=$($row.d)")
        }
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-HostToolsReady([string]$repoRootWsl) {
    $probeCmd = @(
        "cd '$repoRootWsl'",
        "test -x ./tools/bcconv_cli_wsl_dbg",
        "./tools/bcconv_cli_wsl_dbg >/tmp/tolua_bcconv_probe.log 2>&1; test $? -eq 2",
        "test -x ./tools/bc_dump_proto_wsl",
        "./tools/bc_dump_proto_wsl >/tmp/tolua_bcdump_probe.log 2>&1; test $? -eq 1",
        "test -x ./tools/bcrun_cli_wsl",
        "./tools/bcrun_cli_wsl >/tmp/tolua_bcrun_probe.log 2>&1; test $? -eq 2"
    ) -join " && "
    return (Invoke-WslBash $probeCmd) -eq 0
}

function Test-BattleRegisterPrevRoleHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' registerprevrole > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

$repoRoot = (Resolve-Path ".").Path
$repoRootWsl = Convert-ToWslPath $repoRoot
$resolvedWsl = Resolve-WslDistro
$resolvedWslLabel = "<default>"
if (-not [string]::IsNullOrWhiteSpace($resolvedWsl)) {
    $resolvedWslLabel = $resolvedWsl
}
Write-Host "Using WSL distro: $resolvedWslLabel"

if (-not (Test-Path $BytecodeDir)) {
    throw "Bytecode directory not found: $BytecodeDir"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outAbs = (Resolve-Path $OutDir).Path

$shouldBuild = -not $SkipBuild
if (-not $shouldBuild -and -not (Test-HostToolsReady -repoRootWsl $repoRootWsl)) {
    Write-Warning "WSL host tools are missing or incompatible with distro '$resolvedWsl'; rebuilding despite -SkipBuild."
    $shouldBuild = $true
}

if ($shouldBuild) {
    $buildCmd = @(
        "cd '$repoRootWsl'",
        "cd ./luajit-2.1/src && make clean >/dev/null && make XCFLAGS='-DLUAJIT_ENABLE_GC64' >/dev/null && cd ../..",
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcconv_cli.c tolua.c int64.c uint64.c ./luajit-2.1/src/lj_bc.o ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcconv_cli_wsl_dbg",
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcrun_cli.c int64.c uint64.c ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcrun_cli_wsl",
        "gcc -O2 -I./luajit-2.1/src tools/bc_dump_proto.c -o tools/bc_dump_proto_wsl"
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
    $workflowShiftHits = 0
    $workflowShiftFailures = 0
    $doAllTriggerFailures = 0
    $registerPrevRoleFailures = 0

    if ($name -eq "battle.lua") {
        $disPath = Join-Path $outAbs "battle.proto202.dis.txt"
        $workflowCheck = Test-BattleWorkflowArgShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $disPath
        $workflowShiftHits = $workflowCheck.hits
        $workflowShiftFailures = @($workflowCheck.failures).Count
        if (-not $workflowCheck.ok) {
            $failed = $true
            foreach ($msg in $workflowCheck.failures | Select-Object -First 20) {
                Write-Warning "[battle workflow-shift] $msg"
            }
        }

        $doAllTriggerDis = Join-Path $outAbs "battle.proto19.doalltrigger.dis.txt"
        $doAllTriggerCheck = Test-BattleDoAllTriggerArgShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $doAllTriggerDis
        $doAllTriggerFailures = @($doAllTriggerCheck.failures).Count
        if (-not $doAllTriggerCheck.ok) {
            $failed = $true
            foreach ($msg in $doAllTriggerCheck.failures) {
                Write-Warning "[battle doalltrigger] $msg"
            }
        }

        $registerPrevRoleLog = Join-Path $outAbs "battle.registerprevrole.log"
        $registerPrevRoleCheck = Test-BattleRegisterPrevRoleHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $registerPrevRoleLog
        $registerPrevRoleFailures = if ($registerPrevRoleCheck.ok) { 0 } else { 1 }
        if (-not $registerPrevRoleCheck.ok) {
            $failed = $true
            Write-Warning "[battle registerprevrole] harness failed exit=$($registerPrevRoleCheck.exit_code): $registerPrevRoleLog"
        }
    }

    if ($code -ne 0 -or $convFail -gt 0 -or $converted -eq 0) {
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
        workflow_shift_hits = $workflowShiftHits
        workflow_shift_fail = $workflowShiftFailures
        doalltrigger_fail   = $doAllTriggerFailures
        registerprevrole_fail = $registerPrevRoleFailures
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
        if ($row.conversion_failed -ne $baseRow.conversion_failed) {
            $failed = $true
        }
        if ($row.register_overflow -ne $baseRow.register_overflow) {
            $failed = $true
        }
        if ($row.file -eq "battle.lua") {
            if ($row.workflow_shift_fail -ne 0 -or $row.workflow_shift_hits -lt 5) {
                $failed = $true
            }
            if ($row.doalltrigger_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'registerprevrole_fail' -and $row.registerprevrole_fail -ne 0) {
                $failed = $true
            }
        }
    }
}

Write-Host "Offline regression summary: $summaryPath"
$summary | Format-Table file, exit_code, converted_lines, conversion_failed, register_overflow, kstr_tdup_rejects, copy_fallback_hits

if ($failed) {
    throw "Offline regression failed. Check logs under: $outAbs"
}

Write-Host "[OK] Offline regression passed."

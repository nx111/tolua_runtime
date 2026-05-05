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
        "test -x ./tools/bc_dump_proto_wsl",
        "test -x ./tools/bcrun_cli_wsl"
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

function Test-BattleRestHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_rest > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleRestHuanhunMinHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_rest_huanhun_mincall > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleRegisterWorkflowForSkillsEntryShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 6 35 41 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto6 35..41 failed exit=$code")
        }
    }

    $rows = @{}
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=\d+)?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $pc = [int]$matches['pc']
            $rows[$pc] = [pscustomobject]@{
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            }
        }
    }

    $checks = @(
        @{ pc = 35; op = "GGET";  a = 12; b = 0;  c = 7;  d = 7 },
        @{ pc = 36; op = "TGETS"; a = 12; b = 12; c = 8;  d = 3080 },
        @{ pc = 37; op = "TGETV"; a = 14; b = 6;  c = 11; d = 1547 },
        @{ pc = 38; op = "TDUP";  a = 15; b = 0;  c = 9;  d = 9 },
        @{ pc = 39; op = "TSETS"; a = 2;  b = 15; c = 10; d = 3850 },
        @{ pc = 40; op = "TSETS"; a = 3;  b = 15; c = 11; d = 3851 },
        @{ pc = 41; op = "CALL";  a = 12; b = 1;  c = 3;  d = 259 }
    )

    $fails = New-Object System.Collections.Generic.List[string]
    foreach ($check in $checks) {
        if (-not $rows.ContainsKey($check.pc)) {
            $fails.Add("pc$($check.pc) missing")
            continue
        }
        $row = $rows[$check.pc]
        if ($row.op -ne $check.op -or $row.a -ne $check.a -or $row.b -ne $check.b -or $row.c -ne $check.c -or $row.d -ne $check.d) {
            $fails.Add("pc$($check.pc) got=$($row.op) A=$($row.a) B=$($row.b) C=$($row.c) D=$($row.d)")
        }
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-BattleBeforeSkillAnimationCallbackShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 177 744 788 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto177 744..788 failed exit=$code")
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
        @{ pc = 754; op = "MOV";   a = 11; b = 0;  c = 10; d = 10; tag = "pc754" },
        @{ pc = 755; op = "MOV";   a = 13; b = 0;  c = 0;  d = 0;  tag = "pc755" },
        @{ pc = 756; op = "MOV";   a = 14; b = 0;  c = 1;  d = 1;  tag = "pc756" },
        @{ pc = 757; op = "MOV";   a = 15; b = 0;  c = 2;  d = 2;  tag = "pc757" },
        @{ pc = 758; op = "MOV";   a = 16; b = 0;  c = 3;  d = 3;  tag = "pc758" },
        @{ pc = 759; op = "MOV";   a = 17; b = 0;  c = 5;  d = 5;  tag = "pc759" },
        @{ pc = 760; op = "CALL";  a = 11; b = 1;  c = 6;  d = 262; tag = "pc760" },
        @{ pc = 782; op = "TGETS"; a = 12; b = 11; c = 119; d = 2935; tag = "pc782" },
        @{ pc = 783; op = "MOV";   a = 14; b = 0;  c = 0;  d = 0;  tag = "pc783" },
        @{ pc = 784; op = "MOV";   a = 15; b = 0;  c = 1;  d = 1;  tag = "pc784" },
        @{ pc = 785; op = "MOV";   a = 16; b = 0;  c = 2;  d = 2;  tag = "pc785" },
        @{ pc = 786; op = "MOV";   a = 17; b = 0;  c = 3;  d = 3;  tag = "pc786" },
        @{ pc = 787; op = "MOV";   a = 18; b = 0;  c = 5;  d = 5;  tag = "pc787" },
        @{ pc = 788; op = "CALL";  a = 12; b = 1;  c = 6;  d = 262; tag = "pc788" }
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

function Test-BattleBeforeSkillAnimationCallbackHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_beforeskillanimation_callback > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleBeforeRoleActionMinCallShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 132 297 305 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto132 297..305 failed exit=$code")
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
        @{ pc = 297; op = "GGET";   a = 6; b = 0;   c = 66; d = 66;   tag = "pc297" },
        @{ pc = 298; op = "TGETS";  a = 6; b = 6;   c = 67; d = 1603; tag = "pc298" },
        @{ pc = 299; op = "SUBNV";  a = 8; b = 5;   c = 3;  d = 1283; tag = "pc299" },
        @{ pc = 300; op = "TGETS";  a = 9; b = 4;   c = 68; d = 1092; tag = "pc300" },
        @{ pc = 301; op = "TGETS";  a = 9; b = 9;   c = 69; d = 2373; tag = "pc301" },
        @{ pc = 302; op = "IST";    a = 0; b = 0;   c = 9;  d = 9;    tag = "pc302" },
        @{ pc = 303; op = "JMP";    a = 9; b = 128; c = 1;  d = 32769; tag = "pc303" },
        @{ pc = 304; op = "KSHORT"; a = 9; b = 0;   c = 0;  d = 0;    tag = "pc304" },
        @{ pc = 305; op = "CALL";   a = 6; b = 2;   c = 3;  d = 515;  tag = "pc305" }
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

function Test-BattleBeforeRoleActionMinCallHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_beforeroleaction_mincall > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleMinusSkillCDSkillLogHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_minusskillcd_skilllog > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleBeforeRoleActionLogShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 132 655 664 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto132 655..664 failed exit=$code")
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
        @{ pc = 655; op = "MOV";   a = 8;  b = 0; c = 0;  d = 0;    tag = "pc655" },
        @{ pc = 656; op = "TGETS"; a = 7;  b = 0; c = 57; d = 57;   tag = "pc656" },
        @{ pc = 657; op = "TGETS"; a = 9;  b = 1; c = 39; d = 295;  tag = "pc657" },
        @{ pc = 658; op = "TGETS"; a = 9;  b = 9; c = 29; d = 2333; tag = "pc658" },
        @{ pc = 659; op = "KSTR";  a = 10; b = 0; c = 117; d = 117; tag = "pc659" },
        @{ pc = 660; op = "CAT";   a = 9;  b = 9; c = 10; d = 2314; tag = "pc660" },
        @{ pc = 661; op = "MOV";   a = 11; b = 0; c = 9;  d = 9;    tag = "pc661" },
        @{ pc = 662; op = "MOV";   a = 10; b = 0; c = 8;  d = 8;    tag = "pc662" },
        @{ pc = 663; op = "MOV";   a = 8;  b = 0; c = 7;  d = 7;    tag = "pc663" },
        @{ pc = 664; op = "CALL";  a = 8;  b = 1; c = 3;  d = 259;  tag = "pc664" }
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

function Test-BattleBeforeRoleActionLogHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' battle_beforeroleaction_logprobe > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BattleBeforeRoleActionHasBuffShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 132 1143 1152 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto132 1143..1152 failed exit=$code")
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
        @{ pc = 1146; op = "ISNEXT"; a = 13; b = 128; c = 26; d = 32794; tag = "pc1146" },
        @{ pc = 1147; op = "MOV";    a = 17; b = 0;   c = 1;  d = 1;     tag = "pc1147" },
        @{ pc = 1148; op = "TGETS";  a = 15; b = 1;   c = 49; d = 305;   tag = "pc1148" },
        @{ pc = 1149; op = "MOV";    a = 18; b = 0;   c = 14; d = 14;    tag = "pc1149" },
        @{ pc = 1150; op = "CALL";   a = 15; b = 2;   c = 3;  d = 515;   tag = "pc1150" }
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

function Test-BattleMinusSkillCDSkillLogShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 127 60 71 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto127 60..71 failed exit=$code")
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
        @{ pc = 60; op = "MOV";   a = 15; b = 0;  c = 0;  d = 0;    tag = "pc60" },
        @{ pc = 61; op = "TGETS"; a = 13; b = 0;  c = 10; d = 10;   tag = "pc61" },
        @{ pc = 62; op = "TGETS"; a = 16; b = 1;  c = 3;  d = 259;  tag = "pc62" },
        @{ pc = 63; op = "TGETS"; a = 16; b = 16; c = 6;  d = 4102; tag = "pc63" },
        @{ pc = 64; op = "KSTR";  a = 17; b = 0;  c = 11; d = 11;   tag = "pc64" },
        @{ pc = 65; op = "MOV";   a = 18; b = 0;  c = 11; d = 11;   tag = "pc65" },
        @{ pc = 66; op = "KSTR";  a = 19; b = 0;  c = 12; d = 12;   tag = "pc66" },
        @{ pc = 67; op = "MOV";   a = 20; b = 0;  c = 12; d = 12;   tag = "pc67" },
        @{ pc = 68; op = "KSTR";  a = 21; b = 0;  c = 13; d = 13;   tag = "pc68" },
        @{ pc = 69; op = "TGETS"; a = 22; b = 10; c = 7;  d = 2567; tag = "pc69" },
        @{ pc = 70; op = "CAT";   a = 16; b = 16; c = 22; d = 4118; tag = "pc70" },
        @{ pc = 71; op = "CALL";  a = 13; b = 1;  c = 3;  d = 259;  tag = "pc71" }
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

function Test-BattleBeforeRoleActionNeedRefreshShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 132 647 649 > '$disWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 132 878 880 >> '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto132 647..649 or 878..880 failed exit=$code")
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
        @{ pc = 647; op = "MOV";   a = 9;  b = 0; c = 1;   d = 1;   tag = "pc647" },
        @{ pc = 648; op = "TGETS"; a = 7;  b = 1; c = 116; d = 372; tag = "pc648" },
        @{ pc = 649; op = "CALL";  a = 7;  b = 1; c = 2;   d = 258; tag = "pc649" },
        @{ pc = 878; op = "MOV";   a = 18; b = 0; c = 1;   d = 1;   tag = "pc878" },
        @{ pc = 879; op = "TGETS"; a = 16; b = 1; c = 116; d = 372; tag = "pc879" },
        @{ pc = 880; op = "CALL";  a = 16; b = 1; c = 2;   d = 258; tag = "pc880" }
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

function Test-AttackLogicTempValueArgShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 389 2858 2886 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto389 2858..2886 failed exit=$code")
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
        @{ pc = 2858; op = "GGET";  a = 36; b = 0;  c = 34; d = 34;   tag = "pc2858" },
        @{ pc = 2859; op = "TGETS"; a = 36; b = 36; c = 49; d = 9265; tag = "pc2859" },
        @{ pc = 2860; op = "KSTR";  a = 38; b = 2;  c = 206; d = 718; tag = "pc2860" },
        @{ pc = 2861; op = "KSTR";  a = 39; b = 2;  c = 207; d = 719; tag = "pc2861" },
        @{ pc = 2862; op = "KPRI";  a = 40; b = 0;  c = 1;  d = 1;    tag = "pc2862" },
        @{ pc = 2863; op = "FNEW";  a = 41; b = 2;  c = 208; d = 720; tag = "pc2863" },
        @{ pc = 2864; op = "CALL";  a = 36; b = 1;  c = 5;  d = 261;  tag = "pc2864" },
        @{ pc = 2865; op = "GGET";  a = 36; b = 0;  c = 34; d = 34;   tag = "pc2865" },
        @{ pc = 2866; op = "TGETS"; a = 36; b = 36; c = 49; d = 9265; tag = "pc2866" },
        @{ pc = 2867; op = "KSTR";  a = 38; b = 2;  c = 206; d = 718; tag = "pc2867" },
        @{ pc = 2868; op = "KSTR";  a = 39; b = 2;  c = 209; d = 721; tag = "pc2868" },
        @{ pc = 2869; op = "KPRI";  a = 40; b = 0;  c = 1;  d = 1;    tag = "pc2869" },
        @{ pc = 2870; op = "FNEW";  a = 41; b = 2;  c = 210; d = 722; tag = "pc2870" },
        @{ pc = 2871; op = "CALL";  a = 36; b = 1;  c = 5;  d = 261;  tag = "pc2871" },
        @{ pc = 2872; op = "GGET";  a = 36; b = 0;  c = 34; d = 34;   tag = "pc2872" },
        @{ pc = 2873; op = "TGETS"; a = 36; b = 36; c = 49; d = 9265; tag = "pc2873" },
        @{ pc = 2874; op = "KSTR";  a = 38; b = 2;  c = 211; d = 723; tag = "pc2874" },
        @{ pc = 2875; op = "KSTR";  a = 39; b = 2;  c = 207; d = 719; tag = "pc2875" },
        @{ pc = 2876; op = "KPRI";  a = 40; b = 0;  c = 1;  d = 1;    tag = "pc2876" },
        @{ pc = 2877; op = "FNEW";  a = 41; b = 2;  c = 212; d = 724; tag = "pc2877" },
        @{ pc = 2878; op = "CALL";  a = 36; b = 1;  c = 5;  d = 261;  tag = "pc2878" },
        @{ pc = 2879; op = "GGET";  a = 36; b = 0;  c = 34; d = 34;   tag = "pc2879" },
        @{ pc = 2880; op = "TGETS"; a = 36; b = 36; c = 49; d = 9265; tag = "pc2880" },
        @{ pc = 2881; op = "KSTR";  a = 38; b = 2;  c = 211; d = 723; tag = "pc2881" },
        @{ pc = 2882; op = "KSTR";  a = 39; b = 2;  c = 200; d = 712; tag = "pc2882" },
        @{ pc = 2883; op = "KPRI";  a = 40; b = 0;  c = 1;  d = 1;    tag = "pc2883" },
        @{ pc = 2884; op = "FNEW";  a = 41; b = 2;  c = 213; d = 725; tag = "pc2884" },
        @{ pc = 2885; op = "CALL";  a = 36; b = 1;  c = 5;  d = 261;  tag = "pc2885" }
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

function Test-AttackLogicExtend3ArgShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 389 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto389 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=(?<line>\d+))?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                line = if ($matches['line']) { [int]$matches['line'] } else { -1 }
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $fails = New-Object System.Collections.Generic.List[string]
    $good731 = 0
    $good779 = 0
    $badPcs = New-Object System.Collections.Generic.List[string]
    for ($idx = 6; $idx -lt $rows.Count; $idx++) {
        $c = $rows[$idx]
        $i1 = $rows[$idx - 1]
        $i2 = $rows[$idx - 2]
        $i3 = $rows[$idx - 3]
        $i4 = $rows[$idx - 4]
        $i5 = $rows[$idx - 5]
        $i6 = $rows[$idx - 6]

        if ($c.op -ne 'CALL' -or $c.a -ne 36 -or $c.b -ne 1 -or $c.c -ne 5) {
            continue
        }

        $headOk = (
            $i5.op -eq 'TGETS' -and $i5.a -eq 36 -and $i5.b -eq 36 -and
            $i5.c -eq 49 -and $i5.d -eq 9265 -and
            $i6.op -eq 'GGET' -and $i6.a -eq 36 -and $i6.d -eq 34
        )
        if (-not $headOk) {
            continue
        }

        if ($i4.op -eq 'KSTR' -and $i4.a -eq 37 -and ($i4.d -eq 731 -or $i4.d -eq 779) -and
            $i3.op -eq 'KSTR' -and $i3.a -eq 38 -and
            $i2.op -eq 'KPRI' -and $i2.a -eq 39 -and
            $i1.op -eq 'FNEW' -and $i1.a -eq 40) {
            $badPcs.Add([string]$c.pc)
            continue
        }

        if ($i4.op -eq 'KSTR' -and $i4.a -eq 38 -and ($i4.d -eq 731 -or $i4.d -eq 779) -and
            $i3.op -eq 'KSTR' -and $i3.a -eq 39 -and
            $i2.op -eq 'KPRI' -and $i2.a -eq 40 -and
            $i1.op -eq 'FNEW' -and $i1.a -eq 41) {
            if ($i4.d -eq 731) {
                $good731++
            } else {
                $good779++
            }
        }
    }

    if ($badPcs.Count -ne 0) {
        $fails.Add("residual bad extend3 root C5 windows at CALL pc=" + ($badPcs -join ','))
    }
    if ($good731 -eq 0) {
        $fails.Add("missing corrected d731 extend3 root C5 window")
    }
    if ($good779 -eq 0) {
        $fails.Add("missing corrected d779 extend3 root C5 window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtend3PrevRoleDefCallWindow([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 364 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto364 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=20\s+B=1\s+C=10\s+D=266'
    $goodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=30\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=20\s+B=1\s+C=10\s+D=266'
    $overshiftPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=30\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=0\s+C=20\s+D=20.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=30\s+B=0\s+C=29\s+D=29.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=28\s+D=28.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=26\s+D=26.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=23\s+D=23.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=22\s+D=22.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=20\s+B=1\s+C=10\s+D=266'
    $firstWindowFixedPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=30\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=0\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=20\s+D=20.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=30\s+B=0\s+C=30\s+D=30.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=29\s+B=0\s+C=29\s+D=29.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=28\s+B=0\s+C=28\s+D=28.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=27\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=26\s+B=0\s+C=26\s+D=26.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=25\s+B=0\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=24\s+B=0\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=23\s+D=23.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=22\s+D=22.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=20\s+B=1\s+C=10\s+D=266'
    $badCount = [regex]::Matches($disText, $badPattern).Count
    $goodCount = [regex]::Matches($disText, $goodPattern).Count
    $overshiftCount = [regex]::Matches($disText, $overshiftPattern).Count
    $firstWindowFixedCount = [regex]::Matches($disText, $firstWindowFixedPattern).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badCount -ne 0) {
        $fails.Add("residual bad proto364 callback C10 window")
    }
    if ($overshiftCount -ne 0) {
        $fails.Add("residual proto364 attacker callback C10 over-shift window")
    }
    if ($goodCount -eq 0) {
        $fails.Add("missing corrected proto364 callback C10 window")
    }
    if ($firstWindowFixedCount -eq 0) {
        $fails.Add("missing corrected proto364 attacker callback C10 window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-GameEngineExecuteNextStoryActionCallbackWindows([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 108 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto108 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=(?<line>\d+))?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                line = if ($matches['line']) { [int]$matches['line'] } else { -1 }
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $targetLines = @(2649, 2654, 2677, 2715, 2725, 2745, 2750)
    $fails = New-Object System.Collections.Generic.List[string]

    foreach ($lineNo in $targetLines) {
        $badCount = 0
        $goodCount = 0
        for ($i = 0; $i -le $rows.Count - 4; $i++) {
            $r0 = $rows[$i + 0]
            $r1 = $rows[$i + 1]
            $r2 = $rows[$i + 2]
            $r3 = $rows[$i + 3]
            if ($r0.line -ne $lineNo -or $r1.line -ne $lineNo -or $r2.line -ne $lineNo -or $r3.line -ne $lineNo) {
                continue
            }

            if ($r0.op -eq 'MOV' -and $r0.a -eq 8 -and $r0.b -eq 0 -and $r0.c -eq 0 -and $r0.d -eq 0 -and
                $r1.op -eq 'TGETS' -and $r1.a -eq 7 -and $r1.b -eq 0 -and $r1.c -eq 84 -and $r1.d -eq 84 -and
                $r2.op -eq 'MOV' -and $r2.a -eq 9 -and $r2.b -eq 0 -and $r2.c -eq 3 -and $r2.d -eq 3 -and
                $r3.op -eq 'CALL' -and $r3.a -eq 7 -and $r3.b -eq 1 -and $r3.c -eq 3 -and $r3.d -eq 259) {
                $badCount++
            }

            if ($r0.op -eq 'MOV' -and $r0.a -eq 9 -and $r0.b -eq 0 -and $r0.c -eq 0 -and $r0.d -eq 0 -and
                $r1.op -eq 'TGETS' -and $r1.a -eq 7 -and $r1.b -eq 0 -and $r1.c -eq 84 -and $r1.d -eq 84 -and
                $r2.op -eq 'MOV' -and $r2.a -eq 10 -and $r2.b -eq 0 -and $r2.c -eq 3 -and $r2.d -eq 3 -and
                $r3.op -eq 'CALL' -and $r3.a -eq 7 -and $r3.b -eq 1 -and $r3.c -eq 3 -and $r3.d -eq 259) {
                $goodCount++
            }
        }

        if ($badCount -ne 0) {
            $fails.Add("residual bad GameEngine callback window at line=$lineNo")
        }
        if ($goodCount -eq 0) {
            $fails.Add("missing corrected GameEngine callback window at line=$lineNo")
        }
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicRoleValuesArgShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 389 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto389 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=(?<line>\d+))?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                line = if ($matches['line']) { [int]$matches['line'] } else { -1 }
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $specs = @(
        @{ first = 815; second = 816; fnew = 817; tag = 'atk-8783' },
        @{ first = 818; second = 819; fnew = 820; tag = 'def-8795' },
        @{ first = 818; second = 821; fnew = 822; tag = 'def-8824' },
        @{ first = 818; second = 824; fnew = 825; tag = 'def-8854' },
        @{ first = 815; second = 569; fnew = 826; tag = 'atk-8891' },
        @{ first = 818; second = 683; fnew = 827; tag = 'def-8912' }
    )

    $goodTags = New-Object System.Collections.Generic.HashSet[string]
    $badTags = New-Object System.Collections.Generic.List[string]
    $weirdTags = New-Object System.Collections.Generic.List[string]
    for ($idx = 6; $idx -lt $rows.Count; $idx++) {
        $c = $rows[$idx]
        $i1 = $rows[$idx - 1]
        $i2 = $rows[$idx - 2]
        $i3 = $rows[$idx - 3]
        $i4 = $rows[$idx - 4]
        $i5 = $rows[$idx - 5]
        $i6 = $rows[$idx - 6]

        if ($c.op -ne 'CALL' -or $c.a -ne 36 -or $c.b -ne 1 -or $c.c -ne 5) {
            continue
        }
        if (-not ($i5.op -eq 'TGETS' -and $i5.a -eq 36 -and $i5.b -eq 36 -and $i5.c -eq 49 -and $i5.d -eq 9265)) {
            continue
        }
        if (-not ($i6.op -eq 'GGET' -and $i6.a -eq 36 -and $i6.d -eq 34)) {
            continue
        }

        foreach ($spec in $specs) {
            $badMatch = (
                $i4.op -eq 'KSTR' -and $i4.a -eq 37 -and $i4.d -eq $spec.first -and
                $i3.op -eq 'KSTR' -and $i3.a -eq 38 -and $i3.d -eq $spec.second -and
                $i2.op -eq 'KPRI' -and $i2.a -eq 39 -and $i2.d -eq 1 -and
                $i1.op -eq 'FNEW' -and $i1.a -eq 40 -and $i1.d -eq $spec.fnew
            )
            if ($badMatch) {
                $badTags.Add("$($spec.tag)@pc=$($c.pc)")
                break
            }

            $goodMatch = (
                $i4.op -eq 'KSTR' -and $i4.a -eq 38 -and $i4.d -eq $spec.first -and
                $i3.op -eq 'KSTR' -and $i3.a -eq 39 -and $i3.d -eq $spec.second -and
                $i2.op -eq 'KPRI' -and $i2.a -eq 40 -and $i2.d -eq 1 -and
                $i1.op -eq 'FNEW' -and $i1.a -eq 41 -and $i1.d -eq $spec.fnew
            )
            if ($goodMatch) {
                [void]$goodTags.Add($spec.tag)
                break
            }

            $weirdMatch = (
                $i4.op -eq 'KSTR' -and $i4.d -eq $spec.first -and
                $i3.op -eq 'KSTR' -and $i3.d -eq $spec.second -and
                $i1.op -eq 'FNEW' -and $i1.d -eq $spec.fnew
            )
            if ($weirdMatch) {
                $weirdTags.Add("$($spec.tag)@pc=$($c.pc) got=A$($i4.a)/A$($i3.a)/A$($i2.a)/A$($i1.a)")
                break
            }
        }
    }

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badTags.Count -ne 0) {
        $fails.Add("residual bad rolevalues root C5 windows: " + ($badTags -join ','))
    }
    if ($weirdTags.Count -ne 0) {
        $fails.Add("unexpected rolevalues root C5 windows: " + ($weirdTags -join ','))
    }
    foreach ($spec in $specs) {
        if (-not $goodTags.Contains($spec.tag)) {
            $fails.Add("missing corrected rolevalues root C5 window $($spec.tag)")
        }
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicRoleValuesLogShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 365 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto365 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=(?<line>\d+))?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                line = if ($matches['line']) { [int]$matches['line'] } else { -1 }
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $badPcs = New-Object System.Collections.Generic.List[int]
    $goodPcs = New-Object System.Collections.Generic.List[int]
    $weirdPcs = New-Object System.Collections.Generic.List[string]
    for ($idx = 11; $idx -lt $rows.Count; $idx++) {
        $c = $rows[$idx]
        $i1 = $rows[$idx - 1]
        $i2 = $rows[$idx - 2]
        $i3 = $rows[$idx - 3]
        $i4 = $rows[$idx - 4]
        $i5 = $rows[$idx - 5]
        $i6 = $rows[$idx - 6]
        $i7 = $rows[$idx - 7]
        $i8 = $rows[$idx - 8]
        $i9 = $rows[$idx - 9]
        $i10 = $rows[$idx - 10]
        $i11 = $rows[$idx - 11]

        if ($c.op -ne 'CALL' -or $c.a -ne 15 -or $c.b -ne 1 -or $c.c -ne 3) {
            continue
        }
        if (-not ($i10.op -eq 'TGETS' -and $i10.a -eq 15 -and $i10.b -eq 7 -and $i10.c -eq 42 -and $i10.d -eq 1834)) {
            continue
        }

        $badMatch = (
            $i11.op -eq 'MOV'   -and $i11.a -eq 16 -and $i11.d -eq 7 -and
            $i9.op  -eq 'TGETS' -and $i9.a  -eq 17 -and $i9.b  -eq 6  -and $i9.c  -eq 43 -and $i9.d  -eq 1579 -and
            $i8.op  -eq 'TGETS' -and $i8.a  -eq 17 -and $i8.b  -eq 17 -and $i8.c  -eq 9  -and $i8.d  -eq 4361 -and
            $i7.op  -eq 'KSTR'  -and $i7.a  -eq 18 -and $i7.d  -eq 63 -and
            $i6.op  -eq 'GGET'  -and $i6.a  -eq 19 -and $i6.d  -eq 17 -and
            $i5.op  -eq 'TGETS' -and $i5.a  -eq 19 -and $i5.b  -eq 19 -and $i5.c  -eq 64 -and $i5.d  -eq 4928 -and
            $i4.op  -eq 'SUBNV' -and $i4.a  -eq 21 -and $i4.b  -eq 14 -and $i4.d  -eq 3584 -and
            $i3.op  -eq 'CALL'  -and $i3.a  -eq 19 -and $i3.b  -eq 2  -and $i3.c  -eq 2  -and
            $i2.op  -eq 'KSTR'  -and $i2.a  -eq 20 -and $i2.d  -eq 65 -and
            $i1.op  -eq 'CAT'   -and $i1.a  -eq 17 -and $i1.b  -eq 17 -and $i1.c  -eq 20 -and $i1.d  -eq 4372
        )
        if ($badMatch) {
            $badPcs.Add($c.pc)
            continue
        }

        $goodMatch = (
            $i11.op -eq 'MOV'   -and $i11.a -eq 17 -and $i11.d -eq 7 -and
            $i9.op  -eq 'TGETS' -and $i9.a  -eq 18 -and $i9.b  -eq 6  -and $i9.c  -eq 43 -and $i9.d  -eq 1579 -and
            $i8.op  -eq 'TGETS' -and $i8.a  -eq 18 -and $i8.b  -eq 18 -and $i8.c  -eq 9  -and $i8.d  -eq 4617 -and
            $i7.op  -eq 'KSTR'  -and $i7.a  -eq 19 -and $i7.d  -eq 63 -and
            $i6.op  -eq 'GGET'  -and $i6.a  -eq 20 -and $i6.d  -eq 17 -and
            $i5.op  -eq 'TGETS' -and $i5.a  -eq 20 -and $i5.b  -eq 20 -and $i5.c  -eq 64 -and $i5.d  -eq 5184 -and
            $i4.op  -eq 'SUBNV' -and $i4.a  -eq 22 -and $i4.b  -eq 14 -and $i4.d  -eq 3584 -and
            $i3.op  -eq 'CALL'  -and $i3.a  -eq 20 -and $i3.b  -eq 2  -and $i3.c  -eq 2  -and
            $i2.op  -eq 'KSTR'  -and $i2.a  -eq 21 -and $i2.d  -eq 65 -and
            $i1.op  -eq 'CAT'   -and $i1.a  -eq 18 -and $i1.b  -eq 18 -and $i1.c  -eq 21 -and $i1.d  -eq 4629
        )
        if ($goodMatch) {
            $goodPcs.Add($c.pc)
            continue
        }

        $weirdPrefix = (
            $i11.op -eq 'MOV'   -and $i11.d -eq 7 -and
            $i9.op  -eq 'TGETS' -and $i9.b  -eq 6  -and $i9.c  -eq 43 -and
            $i8.op  -eq 'TGETS' -and $i8.c  -eq 9 -and
            $i7.op  -eq 'KSTR'  -and $i7.d  -eq 63 -and
            $i6.op  -eq 'GGET'  -and $i6.d  -eq 17 -and
            $i2.op  -eq 'KSTR'  -and $i2.d  -eq 65 -and
            $i1.op  -eq 'CAT'
        )
        if ($weirdPrefix) {
            $weirdPcs.Add("pc=$($c.pc) got=MOVA$($i11.a)/TGETSA$($i9.a)/TGETSA$($i8.a)/KSTRA$($i7.a)/GGETA$($i6.a)/CALLA$($i3.a)/KSTRA$($i2.a)/CATA$($i1.a)")
        }
    }

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badPcs.Count -ne 0) {
        $fails.Add("residual bad rolevalues bf.Log window at CALL pc=" + ($badPcs -join ','))
    }
    if ($weirdPcs.Count -ne 0) {
        $fails.Add("unexpected rolevalues bf.Log windows: " + ($weirdPcs -join ';'))
    }
    if ($goodPcs.Count -eq 0) {
        $fails.Add("missing corrected rolevalues bf.Log window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicRoleValuesDefCallbackCallWindow([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 365 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto365 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=(?<line>\d+))?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                line = if ($matches['line']) { [int]$matches['line'] } else { -1 }
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $badPcs = New-Object System.Collections.Generic.List[int]
    $goodPcs = New-Object System.Collections.Generic.List[int]
    $weirdPcs = New-Object System.Collections.Generic.List[string]
    for ($idx = 7; $idx -lt $rows.Count; $idx++) {
        $c = $rows[$idx]
        $i1 = $rows[$idx - 1]
        $i2 = $rows[$idx - 2]
        $i3 = $rows[$idx - 3]
        $i4 = $rows[$idx - 4]
        $i5 = $rows[$idx - 5]
        $i6 = $rows[$idx - 6]
        $i7 = $rows[$idx - 7]

        if ($c.op -ne 'CALL' -or $c.a -ne 16 -or $c.b -ne 1 -or $c.c -ne 7 -or $c.line -ne 7608) {
            continue
        }
        if (-not ($i7.op -eq 'MOV' -and $i7.a -eq 16 -and $i7.d -eq 15)) {
            continue
        }

        $badMatch = (
            $i6.op -eq 'MOV' -and $i6.a -eq 17 -and $i6.d -eq 5 -and
            $i5.op -eq 'MOV' -and $i5.a -eq 18 -and $i5.d -eq 6 -and
            $i4.op -eq 'MOV' -and $i4.a -eq 19 -and $i4.d -eq 2 -and
            $i3.op -eq 'MOV' -and $i3.a -eq 20 -and $i3.d -eq 7 -and
            $i2.op -eq 'MOV' -and $i2.a -eq 21 -and $i2.d -eq 3 -and
            $i1.op -eq 'MOV' -and $i1.a -eq 22 -and $i1.d -eq 4
        )
        if ($badMatch) {
            $badPcs.Add($c.pc)
            continue
        }

        $goodMatch = (
            $i6.op -eq 'MOV' -and $i6.a -eq 18 -and $i6.d -eq 5 -and
            $i5.op -eq 'MOV' -and $i5.a -eq 19 -and $i5.d -eq 6 -and
            $i4.op -eq 'MOV' -and $i4.a -eq 20 -and $i4.d -eq 2 -and
            $i3.op -eq 'MOV' -and $i3.a -eq 21 -and $i3.d -eq 7 -and
            $i2.op -eq 'MOV' -and $i2.a -eq 22 -and $i2.d -eq 3 -and
            $i1.op -eq 'MOV' -and $i1.a -eq 23 -and $i1.d -eq 4
        )
        if ($goodMatch) {
            $goodPcs.Add($c.pc)
            continue
        }

        $weirdPcs.Add("pc=$($c.pc) got=A$($i6.a),A$($i5.a),A$($i4.a),A$($i3.a),A$($i2.a),A$($i1.a)")
    }

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badPcs.Count -ne 0) {
        $fails.Add("residual bad rolevalues def callback window at CALL pc=" + ($badPcs -join ','))
    }
    if ($goodPcs.Count -eq 0) {
        $fails.Add("missing corrected rolevalues def callback window")
    }
    if ($weirdPcs.Count -ne 0) {
        $fails.Add("unexpected rolevalues def callback windows: " + ($weirdPcs -join ';'))
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalentsHasBuffShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 241 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto241 failed exit=$code")
        }
    }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in (Get-Content $disPath)) {
        if ($line -match '^(?<pc>\d{4})(?:\s+line=\d+)?\s+(?<op>[A-Z0-9]+)\s+A=(?<a>\d+)\s+B=(?<b>\d+)\s+C=(?<c>\d+)\s+D=(?<d>\d+)') {
            $rows.Add([pscustomobject]@{
                pc = [int]$matches['pc']
                op = $matches['op']
                a = [int]$matches['a']
                b = [int]$matches['b']
                c = [int]$matches['c']
                d = [int]$matches['d']
            })
        }
    }

    $badPcs = New-Object System.Collections.Generic.List[int]
    $goodPcs = New-Object System.Collections.Generic.List[int]
    $weirdPcs = New-Object System.Collections.Generic.List[string]
    for ($idx = 5; $idx -lt $rows.Count; $idx++) {
        $c = $rows[$idx]
        $i1 = $rows[$idx - 1]
        $i2 = $rows[$idx - 2]
        $i3 = $rows[$idx - 3]
        $i4 = $rows[$idx - 4]
        $i5 = $rows[$idx - 5]

        if ($c.op -ne 'CALL' -or $c.a -ne 15 -or $c.b -ne 2 -or $c.c -ne 3) {
            continue
        }
        if (-not ($i5.op -eq 'MOV' -and $i5.a -eq 17 -and $i5.d -eq 0)) {
            continue
        }
        if (-not ($i4.op -eq 'TGETS' -and $i4.a -eq 15 -and $i4.b -eq 0 -and $i4.c -eq 28 -and $i4.d -eq 28)) {
            continue
        }
        if (-not ($i3.op -eq 'KSTR' -and $i3.a -eq 18 -and $i3.d -eq 85)) {
            continue
        }

        $badMatch = (
            $i2.op -eq 'MOV' -and $i2.a -eq 18 -and $i2.d -eq 17 -and
            $i1.op -eq 'MOV' -and $i1.a -eq 17 -and $i1.d -eq 16
        )
        if ($badMatch) {
            $badPcs.Add($c.pc)
            continue
        }

        $goodMatch = ($i2.op -eq 'MOV' -and $i2.a -eq 18 -and $i2.d -eq 18 -and
                      $i1.op -eq 'MOV' -and $i1.a -eq 17 -and $i1.d -eq 17)
        if ($goodMatch) {
            $goodPcs.Add($c.pc)
            continue
        }

        if ($i2.op -eq 'MOV' -and $i1.op -eq 'MOV') {
            $weirdPcs.Add("pc=$($c.pc) got=MOVA$($i2.a)<-A$($i2.d);MOVA$($i1.a)<-A$($i1.d)")
        }
    }

    $disText = Get-Content $disPath -Raw
    $maxBadPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=15\s+B=\d+\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=67\s+D=3907.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=1\s+C=24\s+D=280.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=31\s+D=4127.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+SUBVN\s+A=16\s+B=16\s+C=10\s+D=4106.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=17\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=15\s+B=2\s+C=3\s+D=515'
    $maxGoodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=15\s+B=\d+\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=67\s+D=3907.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=1\s+C=24\s+D=280.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=31\s+D=4383.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+SUBVN\s+A=17\s+B=17\s+C=10\s+D=4362.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=18\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=15\s+B=2\s+C=3\s+D=515'
    $maxBadCount = [regex]::Matches($disText, $maxBadPattern).Count
    $maxGoodCount = [regex]::Matches($disText, $maxGoodPattern).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badPcs.Count -ne 0) {
        $fails.Add("residual bad extendtalents HasBuff self window at CALL pc=" + ($badPcs -join ','))
    }
    if ($weirdPcs.Count -ne 0) {
        $fails.Add("unexpected extendtalents HasBuff windows: " + ($weirdPcs -join ';'))
    }
    if ($goodPcs.Count -eq 0) {
        $fails.Add("missing corrected extendtalents HasBuff self window")
    }
    if ($maxBadCount -ne 0) {
        $fails.Add("residual bad extendtalents two-arg call window")
    }
    if ($maxGoodCount -eq 0) {
        $fails.Add("missing corrected extendtalents two-arg call window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalentsBiliangLogShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 241 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto241 failed exit=$code")
        }
    }

    $text = Get-Content $disPath -Raw
    $rxOptions = [System.Text.RegularExpressions.RegexOptions]::Multiline
    $badFirst = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=14\s+C=2\s+D=3586.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=15\s+B=\d+\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=5\s+C=15\s+D=1295.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=\d+\s+C=26\s+D=26.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=14\s+B=14\s+C=17\s+D=3601.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=12\s+B=1\s+C=3\s+D=259',
        $rxOptions)
    $goodFirst = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=14\s+C=2\s+D=3586.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=15\s+B=\d+\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=5\s+C=15\s+D=1295.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=\d+\s+C=26\s+D=26.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=14\s+B=14\s+C=17\s+D=3601.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=\d+\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=\d+\s+C=13\s+D=13.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=\d+\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259',
        $rxOptions)

    $badSecond = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=19\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=21\s+B=\d+\s+C=144\s+D=144.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=22\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=22\s+B=22\s+C=2\s+D=5634.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=23\s+B=\d+\s+C=145\s+D=145.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=24\s+B=\d+\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=24\s+B=24\s+C=22\s+D=6166.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+DIVVV\s+A=26\s+B=18\s+C=16\s+D=4624.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+SUBNV\s+A=26\s+B=26\s+C=4\s+D=6660.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=26\s+B=26\s+C=6\s+D=6662.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=24\s+B=2\s+C=2\s+D=514.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=25\s+B=\d+\s+C=146\s+D=146.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=21\s+B=21\s+C=25\s+D=5401.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=19\s+B=1\s+C=3\s+D=259',
        $rxOptions)
    $goodSecond = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=19\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=21\s+B=\d+\s+C=144\s+D=144.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=22\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=22\s+B=22\s+C=2\s+D=5634.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=23\s+B=\d+\s+C=145\s+D=145.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=24\s+B=\d+\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=24\s+B=24\s+C=22\s+D=6166.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+DIVVV\s+A=26\s+B=18\s+C=16\s+D=4624.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+SUBNV\s+A=26\s+B=26\s+C=4\s+D=6660.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=26\s+B=26\s+C=6\s+D=6662.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=24\s+B=2\s+C=2\s+D=514.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=25\s+B=\d+\s+C=146\s+D=146.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=21\s+B=21\s+C=25\s+D=5401.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=\d+\s+C=21\s+D=21.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=\d+\s+C=20\s+D=20.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=\d+\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=20\s+B=1\s+C=3\s+D=259',
        $rxOptions)

    $badThird = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=18\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=20\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=20\s+B=20\s+C=2\s+D=5122.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=21\s+B=\d+\s+C=60\s+D=60.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=\d+\s+C=17\s+D=17.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=23\s+B=\d+\s+C=61\s+D=61.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=20\s+B=20\s+C=23\s+D=5143.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=18\s+B=1\s+C=3\s+D=259',
        $rxOptions)
    $goodThird = [regex]::IsMatch($text,
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=18\s+B=3\s+C=23\s+D=791.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=20\s+B=\d+\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=20\s+B=20\s+C=2\s+D=5122.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=21\s+B=\d+\s+C=60\s+D=60.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=\d+\s+C=17\s+D=17.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=23\s+B=\d+\s+C=61\s+D=61.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=20\s+B=20\s+C=23\s+D=5143.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=\d+\s+C=20\s+D=20.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=\d+\s+C=19\s+D=19.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=18\s+D=18.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=19\s+B=1\s+C=3\s+D=259',
        $rxOptions)

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badFirst) {
        $fails.Add("residual bad proto241 extendtalents biliang bf.Log window #1")
    }
    if (-not $goodFirst) {
        $fails.Add("missing corrected proto241 extendtalents biliang bf.Log window #1")
    }
    if ($badSecond) {
        $fails.Add("residual bad proto241 extendtalents biliang bf.Log window #2")
    }
    if (-not $goodSecond) {
        $fails.Add("missing corrected proto241 extendtalents biliang bf.Log window #2")
    }
    if ($badThird) {
        $fails.Add("residual bad proto241 extendtalents biliang bf.Log window #3")
    }
    if (-not $goodThird) {
        $fails.Add("missing corrected proto241 extendtalents biliang bf.Log window #3")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2GedangLogShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 364 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto364 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $goodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=30\s+D=798.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=31\s+D=31.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=0\s+C=28\s+D=28.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=20\s+B=0\s+C=26\s+D=26.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=17\s+B=2\s+C=3\s+D=515.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=29\s+D=4381.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULNV\s+A=17\s+B=17\s+C=4\s+D=4356.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=32\s+D=32.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=18\s+D=4114.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodCount = [regex]::Matches($disText, $goodPattern).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($goodCount -eq 0) {
        $fails.Add("missing proto364 extendtalents2 first bf.Log window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2MissLogWindow([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 320 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto320 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern = '(?ms)^\d{4}\s+line=6346\s+MOV\s+A=11\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=10\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=12\s+B=1\s+C=27\s+D=283.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=12\s+B=12\s+C=4\s+D=3076.*\r?\n' +
        '^\d{4}\s+line=6346\s+KSTR\s+A=13\s+B=0\s+C=28\s+D=28.*\r?\n' +
        '^\d{4}\s+line=6346\s+CAT\s+A=12\s+B=12\s+C=13\s+D=3085.*\r?\n' +
        '^\d{4}\s+line=6346\s+CALL\s+A=10\s+B=1\s+C=3\s+D=259'
    $goodPattern = '(?ms)^\d{4}\s+line=6346\s+MOV\s+A=12\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=10\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=13\s+B=1\s+C=27\s+D=283.*\r?\n' +
        '^\d{4}\s+line=6346\s+TGETS\s+A=13\s+B=13\s+C=4\s+D=3332.*\r?\n' +
        '^\d{4}\s+line=6346\s+KSTR\s+A=14\s+B=0\s+C=28\s+D=28.*\r?\n' +
        '^\d{4}\s+line=6346\s+CAT\s+A=13\s+B=13\s+C=14\s+D=3342.*\r?\n' +
        '^\d{4}\s+line=6346\s+CALL\s+A=10\s+B=1\s+C=3\s+D=259'
    $badCount = [regex]::Matches($disText, $badPattern).Count
    $goodCount = [regex]::Matches($disText, $goodPattern).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badCount -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 miss bf.Log window")
    }
    if ($goodCount -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 miss bf.Log window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2ChaizhaoLogShift([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 364 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto364 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $goodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=30\s+D=798.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=52\s+D=52.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=17\s+B=0\s+C=16\s+D=16.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=23\s+D=4375.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=17\s+B=2\s+C=2\s+D=514.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=53\s+D=53.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=18\s+D=4114.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodCount = [regex]::Matches($disText, $goodPattern).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($goodCount -eq 0) {
        $fails.Add("missing proto364 extendtalents2 second bf.Log window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2TalentLogWindows([string]$repoRootWsl, [string]$bytecodeWsl, [string]$outDir) {
    $targets = @(
        @{
            proto = 316
            dis = (Join-Path $outDir "attacklogic.proto316.extendtalents2_talentlog.dis.txt")
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=0\s+C=18\s+D=18.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=12\s+C=19\s+D=3091.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=13\s+B=0\s+C=20\s+D=20.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=11\s+B=11\s+C=13\s+D=2829.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=0\s+C=18\s+D=18.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=19\s+D=3347.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=14\s+B=0\s+C=20\s+D=20.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=12\s+B=12\s+C=14\s+D=3086.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
        }
        @{
            proto = 317
            dis = (Join-Path $outDir "attacklogic.proto317.extendtalents2_talentlog.dis.txt")
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=11\s+D=779.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=9\s+B=0\s+C=12\s+D=12.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=0\s+C=13\s+D=13.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=10\s+C=14\s+D=2574.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=15\s+D=15.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=9\s+B=9\s+C=11\s+D=2315.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3\s+D=259'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=11\s+D=779.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=10\s+B=0\s+C=12\s+D=12.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=0\s+C=13\s+D=13.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=11\s+C=14\s+D=2830.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=15\s+D=15.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=10\s+B=10\s+C=12\s+D=2572.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3\s+D=259'
        }
        @{
            proto = 318
            dis = (Join-Path $outDir "attacklogic.proto318.extendtalents2_talentlog.dis.txt")
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=1\s+C=18\s+D=274.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=12\s+C=19\s+D=3091.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=13\s+B=0\s+C=20\s+D=20.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=11\s+B=11\s+C=13\s+D=2829.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=1\s+C=18\s+D=274.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=19\s+D=3347.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=14\s+B=0\s+C=20\s+D=20.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=12\s+B=12\s+C=14\s+D=3086.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
        }
        @{
            proto = 319
            dis = (Join-Path $outDir "attacklogic.proto319.extendtalents2_talentlog.dis.txt")
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=1\s+C=18\s+D=274.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=12\s+B=12\s+C=1\s+D=3073.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=13\s+B=0\s+C=19\s+D=19.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=11\s+B=11\s+C=13\s+D=2829.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=17\s+D=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=1\s+C=18\s+D=274.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=1\s+D=3329.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=14\s+B=0\s+C=19\s+D=19.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=12\s+B=12\s+C=14\s+D=3086.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=1\s+C=3\s+D=259'
        }
        @{
            proto = 320
            dis = (Join-Path $outDir "attacklogic.proto320.extendtalents2_talentlog_line6363.dis.txt")
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=12\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=3\s+C=26\s+D=794.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=1\s+C=27\s+D=283.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=4\s+D=3332.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=14\s+B=0\s+C=40\s+D=40.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=13\s+B=13\s+C=14\s+D=3342.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=11\s+B=1\s+C=3\s+D=259'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=0\s+C=3\s+D=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=3\s+C=26\s+D=794.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=1\s+C=27\s+D=283.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=14\s+C=4\s+D=3588.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=15\s+B=0\s+C=40\s+D=40.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=14\s+B=14\s+C=15\s+D=3599.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=11\s+B=1\s+C=3\s+D=259'
        }
    )

    $fails = New-Object System.Collections.Generic.List[string]
    foreach ($target in $targets) {
        $disWsl = Convert-ToWslPath $target.dis
        $dumpCmd = @(
            "cd '$repoRootWsl'",
            "./tools/bc_dump_proto_wsl '$bytecodeWsl' $($target.proto) > '$disWsl'"
        ) -join " && "
        $code = Invoke-WslBash $dumpCmd
        if ($code -ne 0) {
            $fails.Add("bc_dump_proto_wsl proto$($target.proto) failed exit=$code")
            continue
        }

        $disText = Get-Content $target.dis -Raw
        $badCount = [regex]::Matches($disText, $target.bad).Count
        $goodCount = [regex]::Matches($disText, $target.good).Count
        if ($badCount -ne 0) {
            $fails.Add("residual bad proto$($target.proto) extendtalents2 talent bf.Log window")
        }
        if ($goodCount -eq 0) {
            $fails.Add("missing corrected proto$($target.proto) extendtalents2 talent bf.Log window")
        }
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2CallbackCallWindow([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 320 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto320 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=11\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=12\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=9\s+D=9.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=11\s+B=1\s+C=8\s+D=264'
    $goodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=11\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=9\s+D=9.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=11\s+B=1\s+C=8\s+D=264'
    $badPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=9\s+D=9.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=15\s+B=1\s+C=8\s+D=264'
    $goodPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=14\s+D=14.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=0\s+D=0.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=21\s+B=0\s+C=4\s+D=4.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=22\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=23\s+B=0\s+C=9\s+D=9.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=15\s+B=1\s+C=8\s+D=264'
    $badCount = [regex]::Matches($disText, $badPattern).Count + [regex]::Matches($disText, $badPattern2).Count
    $goodCount = [regex]::Matches($disText, $goodPattern).Count + [regex]::Matches($disText, $goodPattern2).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badCount -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 callback call window")
    }
    if ($goodCount -lt 2) {
        $fails.Add("missing corrected proto320 extendtalents2 callback call window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2RemoveWindows([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 320 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto320 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badFirst = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=4\s+C=14\s+D=1038.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=\d+\s+C=16\s+D=16.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=173\s+D=4269.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=\d+\s+C=15\s+D=15.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=16\s+B=1\s+C=3\s+D=259'
    $goodFirst = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=4\s+C=14\s+D=1038.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=\d+\s+C=16\s+D=16.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=173\s+D=4269.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=15\s+D=15.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=16\s+B=1\s+C=3\s+D=259'
    $badSecond = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=4\s+C=14\s+D=1038.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=\d+\s+C=17\s+D=17.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=173\s+D=4525.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=15\s+D=15.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=17\s+B=1\s+C=3\s+D=259'
    $goodSecond = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=4\s+C=14\s+D=1038.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=\d+\s+C=17\s+D=17.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=173\s+D=4525.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=\d+\s+C=15\s+D=15.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=17\s+B=1\s+C=3\s+D=259'

    $fails = New-Object System.Collections.Generic.List[string]
    if ([regex]::Matches($disText, $badFirst).Count -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 Remove window #1")
    }
    if ([regex]::Matches($disText, $goodFirst).Count -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 Remove window #1")
    }
    if ([regex]::Matches($disText, $badSecond).Count -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 Remove window #2")
    }
    if ([regex]::Matches($disText, $goodSecond).Count -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 Remove window #2")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2XiLogWindow([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 367 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto367 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=10\s+D=778.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=9\s+B=\d+\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=1\s+C=12\s+D=268.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=10\s+C=1\s+D=2561.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=\d+\s+C=13\s+D=13.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=9\s+B=9\s+C=11\s+D=2315.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3\s+D=259'
    $goodPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=\d+\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=10\s+D=778.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=10\s+B=\d+\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=1\s+C=12\s+D=268.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=11\s+C=1\s+D=2817.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=\d+\s+C=13\s+D=13.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=10\s+B=10\s+C=12\s+D=2572.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3\s+D=259'

    $fails = New-Object System.Collections.Generic.List[string]
    if ([regex]::Matches($disText, $badPattern).Count -ne 0) {
        $fails.Add("residual bad proto367 xi bf.Log window")
    }
    if ([regex]::Matches($disText, $goodPattern).Count -eq 0) {
        $fails.Add("missing corrected proto367 xi bf.Log window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2XiValueMinWindows([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 320 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto320 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern1 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=13\s+B=12\s+C=26\s+D=3098.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=14\s+B=0\s+C=44\s+D=44.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=14\s+C=123\s+D=3707.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=15\s+B=0\s+C=30\s+D=30.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=96\s+D=4192.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=14\s+B=2\s+C=3\s+D=515'
    $goodPattern1 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=13\s+B=12\s+C=26\s+D=3098.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=14\s+B=0\s+C=44\s+D=44.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=14\s+C=123\s+D=3707.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=16\s+B=0\s+C=30\s+D=30.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=96\s+D=4448.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=14\s+B=2\s+C=3\s+D=515'
    $badPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=12\s+B=13\s+C=14\s+D=3342.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=13\s+B=0\s+C=44\s+D=44.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=123\s+D=3451.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KNUM\s+A=14\s+B=0\s+C=18\s+D=18.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=2\s+C=3\s+D=515'
    $goodPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=12\s+B=13\s+C=14\s+D=3342.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=13\s+B=0\s+C=44\s+D=44.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=13\s+C=123\s+D=3451.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KNUM\s+A=15\s+B=0\s+C=18\s+D=18.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=0\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=2\s+C=3\s+D=515'
    $badCount1 = [regex]::Matches($disText, $badPattern1).Count
    $goodCount1 = [regex]::Matches($disText, $goodPattern1).Count
    $badCount2 = [regex]::Matches($disText, $badPattern2).Count
    $goodCount2 = [regex]::Matches($disText, $goodPattern2).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badCount1 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 xiValue math.min window")
    }
    if ($goodCount1 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 xiValue math.min window")
    }
    if ($badCount2 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 xiValue clamp math.min window")
    }
    if ($goodCount2 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 xiValue clamp math.min window")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-AttackLogicExtendTalents2YihuaLogWindows([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 320 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto320 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $badPattern0a = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=4\s+D=3844.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=50\s+D=50.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=51\s+D=51.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=15\s+B=15\s+C=18\s+D=3858.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodPattern0a = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=4\s+D=4100.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=50\s+D=50.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=12\s+D=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=19\s+B=0\s+C=51\s+D=51.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=19\s+D=4115.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $badPattern0b = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=4\s+D=3844.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=52\s+D=52.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=15\s+B=15\s+C=16\s+D=3856.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodPattern0b = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=4\s+D=4100.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=52\s+D=52.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=17\s+D=4113.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $badPattern1 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=4\s+D=3844.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=180\s+D=180.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=17\s+B=12\s+C=3\s+D=3075.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=181\s+D=181.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=19\s+B=0\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=15\s+B=15\s+C=19\s+D=3859.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodPattern1 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=4\s+D=4100.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=180\s+D=180.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=18\s+B=12\s+C=3\s+D=3075.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=19\s+B=0\s+C=181\s+D=181.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=20\s+B=0\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=20\s+D=4116.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $badPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=15\s+B=15\s+C=4\s+D=3844.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=183\s+D=183.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=17\s+B=0\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=172\s+D=172.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=15\s+B=15\s+C=18\s+D=3858.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodPattern2 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=0\s+C=27\s+D=27.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=4\s+D=4100.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=183\s+D=183.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=18\s+B=0\s+C=11\s+D=11.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=19\s+B=0\s+C=172\s+D=172.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=19\s+D=4115.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $badPattern3 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=106\s+D=106.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=17\s+B=13\s+C=7\s+D=3335.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=107\s+D=107.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=18\s+D=4114.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=14\s+B=1\s+C=3\s+D=259'
    $goodPattern3 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=16\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=14\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=106\s+D=106.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=18\s+B=13\s+C=7\s+D=3335.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=19\s+B=0\s+C=107\s+D=107.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=17\s+B=17\s+C=19\s+D=4371.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=14\s+B=1\s+C=3\s+D=259'
    $badPattern4 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=15\s+B=0\s+C=110\s+D=110.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=1\s+C=27\s+D=283.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=16\s+B=16\s+C=4\s+D=4100.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=17\s+B=0\s+C=79\s+D=79.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=15\s+B=15\s+C=17\s+D=3857.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $goodPattern4 = '(?ms)^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=3\s+D=3.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=13\s+B=3\s+C=26\s+D=794.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=110\s+D=110.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=1\s+C=27\s+D=283.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=17\s+B=17\s+C=4\s+D=4356.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=18\s+B=0\s+C=79\s+D=79.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=16\s+B=16\s+C=18\s+D=4114.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=13\s+B=1\s+C=3\s+D=259'
    $badCount0a = [regex]::Matches($disText, $badPattern0a).Count
    $goodCount0a = [regex]::Matches($disText, $goodPattern0a).Count
    $badCount0b = [regex]::Matches($disText, $badPattern0b).Count
    $goodCount0b = [regex]::Matches($disText, $goodPattern0b).Count
    $badCount1 = [regex]::Matches($disText, $badPattern1).Count
    $goodCount1 = [regex]::Matches($disText, $goodPattern1).Count
    $badCount2 = [regex]::Matches($disText, $badPattern2).Count
    $goodCount2 = [regex]::Matches($disText, $goodPattern2).Count
    $badCount3 = [regex]::Matches($disText, $badPattern3).Count
    $goodCount3 = [regex]::Matches($disText, $goodPattern3).Count
    $badCount4 = [regex]::Matches($disText, $badPattern4).Count
    $goodCount4 = [regex]::Matches($disText, $goodPattern4).Count

    $fails = New-Object System.Collections.Generic.List[string]
    if ($badCount0a -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 bf.Log window line6384")
    }
    if ($goodCount0a -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 bf.Log window line6384")
    }
    if ($badCount0b -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 bf.Log window line6386")
    }
    if ($goodCount0b -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 bf.Log window line6386")
    }
    if ($badCount1 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 yihua bf.Log window #1")
    }
    if ($goodCount1 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 yihua bf.Log window #1")
    }
    if ($badCount2 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 yihua bf.Log window #2")
    }
    if ($goodCount2 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 yihua bf.Log window #2")
    }
    if ($badCount3 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 bf.Log window line6471/6473")
    }
    if ($goodCount3 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 bf.Log window line6471/6473")
    }
    if ($badCount4 -ne 0) {
        $fails.Add("residual bad proto320 extendtalents2 bf.Log window line6481/6487")
    }
    if ($goodCount4 -eq 0) {
        $fails.Add("missing corrected proto320 extendtalents2 bf.Log window line6481/6487")
    }

    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-BuffOnRoundBuffMaxCallShape([string]$repoRootWsl, [string]$bytecodeWsl, [string]$disPath) {
    $disWsl = Convert-ToWslPath $disPath
    $dumpCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bc_dump_proto_wsl '$bytecodeWsl' 0 > '$disWsl'"
    ) -join " && "
    $code = Invoke-WslBash $dumpCmd
    if ($code -ne 0) {
        return [pscustomobject]@{
            ok = $false
            failures = @("bc_dump_proto_wsl proto0 failed exit=$code")
        }
    }

    $disText = Get-Content $disPath -Raw
    $shapeChecks = @(
        @{
            name = 'recovery min window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=6\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=10\s+D=1546.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=3\s+C=11\s+D=779.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=12\s+D=2060.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=13\s+D=2061.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=9\s+B=1\s+C=144\s+D=400.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=3'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=6\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=10\s+D=1546.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=3\s+C=11\s+D=779.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=12\s+D=2060.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=13\s+D=2061.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=9\s+B=1\s+C=144\s+D=400.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=9\s+D=9.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=3'
        },
        @{
            name = 'poison max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=5\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=5\s+B=5\s+C=30\s+D=1310.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=3\s+C=6\s+D=774.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=6\s+B=6\s+C=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=7\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=9\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=6\s+B=6\s+C=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=7\s+B=0\s+C=6\s+D=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=5\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=5\s+B=5\s+C=30\s+D=1310.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=7\s+B=7\s+C=17.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=11\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=7\s+B=7\s+C=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
        },
        @{
            name = 'hp-percent max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=5\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=5\s+B=5\s+C=30\s+D=1310.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=3\s+C=(?:37|7)\s+D=(?:805|775).*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=6\s+B=6\s+C=4\s+D=1540.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=7\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=47\s+D=1839.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=9\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=6\s+B=6\s+C=7\s+D=1543.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=7\s+B=0\s+C=6\s+D=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=5\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=5\s+B=5\s+C=30\s+D=1310.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=(?:37|7).*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=7\s+B=7\s+C=4.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=11\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=7\s+B=7\s+C=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
        },
        @{
            name = 'attribute max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47\s+D=2095.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+SUBVV\s+A=10\s+B=10\s+C=6\s+D=2566.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TSETS\s+A=8\s+B=3\s+C=16\s+D=784'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47\s+D=2095.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=3\s+C=16\s+D=784.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+SUBVV\s+A=11\s+B=11\s+C=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TSETS\s+A=8\s+B=3\s+C=16\s+D=784'
        },
        @{
            name = 'mid-poison max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=7\s+B=0\s+C=0\s+D=0.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=14\s+D=1806.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=15\s+D=1807.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=7\s+B=7\s+C=3\s+D=1795.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULNV\s+A=7\s+B=7\s+C=8\s+D=1800.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47\s+D=2095.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=10\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=7\s+B=7\s+C=8\s+D=1800.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=6\s+B=6\s+C=7\s+D=1543.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=7\s+B=0\s+C=6\s+D=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=8\s+B=0\s+C=0\s+D=0.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=14.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=15.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=8\s+B=8\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULNV\s+A=8\s+B=8\s+C=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=9\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=12\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=8\s+B=8\s+C=9.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=7\s+B=7\s+C=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=5\s+B=2\s+C=2'
        },
        @{
            name = 'late-poison max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=3\s+C=6\s+D=774.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=6\s+B=6\s+C=4\s+D=1540.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=8\s+B=0\s+C=0\s+D=0.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=14\s+D=2062.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=8\s+B=8\s+C=(?:9|21).*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=8\s+B=8\s+C=5\s+D=2053.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=8\s+B=8\s+C=20\s+D=2068.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=9\s+C=47\s+D=2351.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=10\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=11\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=8\s+B=8\s+C=9\s+D=2057.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=7\s+B=7\s+C=8\s+D=1800.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=2'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=3\s+C=6.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=7\s+B=7\s+C=4.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+UGET\s+A=8\s+B=0\s+C=0.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=14.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=9\s+B=9\s+C=(?:9|21).*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=9\s+B=9\s+C=5.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=9\s+B=9\s+C=20.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=10\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=10\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=12\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=13\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=10\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=9\s+B=9\s+C=10.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+ADDVV\s+A=8\s+B=8\s+C=9.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=2'
        },
        @{
            name = 'toad max window'
            bad = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=7\s+B=7\s+C=2\s+D=1794.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=7\s+B=7\s+C=5\s+D=1797.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=7\s+B=7\s+C=22\s+D=1814.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=8\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=8\s+B=8\s+C=47\s+D=2095.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=10\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=8\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=7\s+B=7\s+C=8\s+D=1800.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=2'
            good = '(?ms)^\d{4}(?:\s+line=\d+)?\s+ADDVN\s+A=8\s+B=8\s+C=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=8\s+B=8\s+C=5.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVN\s+A=8\s+B=8\s+C=22.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+GGET\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=9\s+C=47.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=2\s+D=2.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSHORT\s+A=12\s+B=0\s+C=1\s+D=1.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=9\s+B=2\s+C=3.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MULVV\s+A=8\s+B=8\s+C=9.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=8\s+D=8.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=2\s+C=2'
        }
    )
    $badRecoveryLowHpLogPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=20\s+D=1812.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=21\s+D=1813.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=9\s+B=0\s+C=22\s+D=22.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=3\s+C=4\s+D=772.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=23\s+D=23.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=12\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=13\s+B=0\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=14\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=15\s+B=0\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=9\s+B=9\s+C=15.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3'
    $goodRecoveryLowHpLogPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=20\s+D=1812.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=9\s+B=0\s+C=7\s+D=7.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=7\s+B=7\s+C=21\s+D=1813.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=10\s+B=0\s+C=22\s+D=22.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=11\s+B=3\s+C=4\s+D=772.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=23\s+D=23.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=13\s+B=0\s+C=2\s+D=2.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=14\s+B=0\s+C=24\s+D=24.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=15\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=16\s+B=0\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=10\s+B=10\s+C=16.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=7\s+B=1\s+C=3'
    $badLogPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=20\s+D=1556.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=7\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=21\s+D=1557.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=8\s+B=0\s+C=43\s+D=43.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=9\s+B=3\s+C=4\s+D=772.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=10\s+B=0\s+C=(?:63|69|72|74|76|79|82)\s+D=(?:63|69|72|74|76|79|82).*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=11\s+B=0\s+C=5\s+D=5.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=12\s+B=0\s+C=25\s+D=25.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=8\s+B=8\s+C=12.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=1\s+C=3'
    $goodLogPattern = '(?ms)^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=0\s+C=1\s+D=1.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=20\s+D=1556.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=8\s+B=0\s+C=6\s+D=6.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=6\s+B=6\s+C=21\s+D=1557.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=9\s+B=0\s+C=43\s+D=43.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+TGETS\s+A=10\s+B=3\s+C=4\s+D=772.*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=11\s+B=0\s+C=(?:63|69|72|74|76|79|82)\s+D=(?:63|69|72|74|76|79|82).*\r?\n' +
        '^\d{4}(?:\s+line=\d+)?\s+MOV\s+A=12\s+B=0\s+C=5\s+D=5.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+KSTR\s+A=13\s+B=0\s+C=25\s+D=25.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CAT\s+A=9\s+B=9\s+C=13.*\r?\n' +
                '^\d{4}(?:\s+line=\d+)?\s+CALL\s+A=6\s+B=1\s+C=3'
    $fails = New-Object System.Collections.Generic.List[string]
    foreach ($check in $shapeChecks) {
        if ([regex]::Matches($disText, $check.bad).Count -ne 0) {
            $fails.Add("residual bad proto0 BUFF_OnRoundBuff $($check.name)")
        }
        if ([regex]::Matches($disText, $check.good).Count -eq 0) {
            $fails.Add("missing corrected proto0 BUFF_OnRoundBuff $($check.name)")
        }
    }
    if ([regex]::Matches($disText, $badRecoveryLowHpLogPattern).Count -ne 0) {
        $fails.Add("residual bad proto0 BUFF_OnRoundBuff recovery low-hp bf.Log self window")
    }
    if ([regex]::Matches($disText, $goodRecoveryLowHpLogPattern).Count -eq 0) {
        $fails.Add("missing corrected proto0 BUFF_OnRoundBuff recovery low-hp bf.Log self window")
    }
    if ([regex]::Matches($disText, $badLogPattern).Count -ne 0) {
        $fails.Add("residual bad proto0 BUFF_OnRoundBuff recovery high-hp bf.Log self window")
    }
    if ([regex]::Matches($disText, $goodLogPattern).Count -eq 0) {
        $fails.Add("missing corrected proto0 BUFF_OnRoundBuff recovery high-hp bf.Log self window")
    }
    return [pscustomobject]@{
        ok = ($fails.Count -eq 0)
        failures = @($fails)
    }
}

function Test-BuffOnRoundBuffMaxCallHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' buff_onroundbuff_maxcall > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-BuffOnRoundBuffRecoveryMinHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' buff_onroundbuff_recoverymin > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicTempValueHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_tempvalue > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicTempValueChainHarness([string]$repoRootWsl, [string]$battleBytecodeWsl, [string]$attacklogicBytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$battleBytecodeWsl' attacklogic_tempvalue_chain '$attacklogicBytecodeWsl' > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2GedangLogHarness([string]$repoRootWsl, [string]$battleBytecodeWsl, [string]$attacklogicBytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$battleBytecodeWsl' attacklogic_extendtalents2_gedanglog '$attacklogicBytecodeWsl' > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2MissLogHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_misslog > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2ChaizhaoLogHarness([string]$repoRootWsl, [string]$battleBytecodeWsl, [string]$attacklogicBytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$battleBytecodeWsl' attacklogic_extendtalents2_chaizhaolog '$attacklogicBytecodeWsl' > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2FullProbeHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_fullprobe > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2RemoveProbeHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_removeprobe > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2XiLogHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_xilog > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2XixingCallbackHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_xixingcallback > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    return [pscustomobject]@{
        ok = ($code -eq 0)
        exit_code = $code
    }
}

function Test-AttackLogicExtendTalents2YihuaLogHarness([string]$repoRootWsl, [string]$bytecodeWsl, [string]$logPath) {
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcrun_cli_wsl '$bytecodeWsl' attacklogic_extendtalents2_yihualog > '$logWsl' 2>&1"
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
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcconv_cli.c tolua_fr1_to_fr2.c int64.c uint64.c ./luajit-2.1/src/lj_bc.o ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcconv_cli_wsl_dbg",
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
$battleOutPath = $null

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
    $restHarnessFailures = 0
    $beforeRoleActionFailures = 0
    $beforeSkillAnimationFailures = 0

    if ($name -eq "battle.lua") {
        $battleOutPath = $outPath
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

        $restHarnessLog = Join-Path $outAbs "battle.rest.log"
        $restHarnessCheck = Test-BattleRestHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $restHarnessLog
        $restHarnessFailures = if ($restHarnessCheck.ok) { 0 } else { 1 }
        if (-not $restHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle rest] harness failed exit=$($restHarnessCheck.exit_code): $restHarnessLog"
        }

        $restHuanhunDis = Join-Path $outAbs "battle.proto6.registerskills_entry.dis.txt"
        $restHuanhunShapeCheck = Test-BattleRegisterWorkflowForSkillsEntryShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $restHuanhunDis
        $restHuanhunShapeFailures = @($restHuanhunShapeCheck.failures).Count
        if (-not $restHuanhunShapeCheck.ok) {
            $failed = $true
            foreach ($msg in $restHuanhunShapeCheck.failures) {
                Write-Warning "[battle registerskills entry] $msg"
            }
        }

        $restHuanhunLog = Join-Path $outAbs "battle.rest_huanhun_mincall.log"
        $restHuanhunHarnessCheck = Test-BattleRestHuanhunMinHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $restHuanhunLog
        $restHuanhunHarnessFailures = if ($restHuanhunHarnessCheck.ok) { 0 } else { 1 }
        if (-not $restHuanhunHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle rest huanhun] harness failed exit=$($restHuanhunHarnessCheck.exit_code): $restHuanhunLog"
        }

        $restHarnessFailures += ($restHuanhunShapeFailures + $restHuanhunHarnessFailures)

        $beforeRoleActionDis = Join-Path $outAbs "battle.proto132.beforeroleaction_mincall.dis.txt"
        $beforeRoleActionShapeCheck = Test-BattleBeforeRoleActionMinCallShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $beforeRoleActionDis
        $beforeRoleActionShapeFailures = @($beforeRoleActionShapeCheck.failures).Count
        if (-not $beforeRoleActionShapeCheck.ok) {
            $failed = $true
            foreach ($msg in $beforeRoleActionShapeCheck.failures) {
                Write-Warning "[battle beforeroleaction shape] $msg"
            }
        }

        $beforeRoleActionLog = Join-Path $outAbs "battle.beforeroleaction_mincall.log"
        $beforeRoleActionHarnessCheck = Test-BattleBeforeRoleActionMinCallHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $beforeRoleActionLog
        $beforeRoleActionHarnessFailures = if ($beforeRoleActionHarnessCheck.ok) { 0 } else { 1 }
        if (-not $beforeRoleActionHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle beforeroleaction harness] failed exit=$($beforeRoleActionHarnessCheck.exit_code): $beforeRoleActionLog"
        }

        $beforeRoleActionHasBuffDis = Join-Path $outAbs "battle.proto132.beforeroleaction_hasbuff.dis.txt"
        $beforeRoleActionHasBuffCheck = Test-BattleBeforeRoleActionHasBuffShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $beforeRoleActionHasBuffDis
        $beforeRoleActionHasBuffFailures = @($beforeRoleActionHasBuffCheck.failures).Count
        if (-not $beforeRoleActionHasBuffCheck.ok) {
            $failed = $true
            foreach ($msg in $beforeRoleActionHasBuffCheck.failures) {
                Write-Warning "[battle beforeroleaction hasbuff] $msg"
            }
        }

        $beforeRoleActionNeedRefreshDis = Join-Path $outAbs "battle.proto132.beforeroleaction_needrefresh.dis.txt"
        $beforeRoleActionNeedRefreshCheck = Test-BattleBeforeRoleActionNeedRefreshShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $beforeRoleActionNeedRefreshDis
        $beforeRoleActionNeedRefreshFailures = @($beforeRoleActionNeedRefreshCheck.failures).Count
        if (-not $beforeRoleActionNeedRefreshCheck.ok) {
            $failed = $true
            foreach ($msg in $beforeRoleActionNeedRefreshCheck.failures) {
                Write-Warning "[battle beforeroleaction needrefresh] $msg"
            }
        }

        $minusSkillCdDis = Join-Path $outAbs "battle.proto127.minusskillcd_skilllog.dis.txt"
        $minusSkillCdShapeCheck = Test-BattleMinusSkillCDSkillLogShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $minusSkillCdDis
        $minusSkillCdShapeFailures = @($minusSkillCdShapeCheck.failures).Count
        if (-not $minusSkillCdShapeCheck.ok) {
            $failed = $true
            foreach ($msg in $minusSkillCdShapeCheck.failures) {
                Write-Warning "[battle minusskillcd shape] $msg"
            }
        }

        $minusSkillCdLog = Join-Path $outAbs "battle.minusskillcd_skilllog.log"
        $minusSkillCdHarnessCheck = Test-BattleMinusSkillCDSkillLogHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $minusSkillCdLog
        $minusSkillCdHarnessFailures = if ($minusSkillCdHarnessCheck.ok) { 0 } else { 1 }
        if (-not $minusSkillCdHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle minusskillcd harness] failed exit=$($minusSkillCdHarnessCheck.exit_code): $minusSkillCdLog"
        }

        $beforeRoleActionLogDis = Join-Path $outAbs "battle.proto132.beforeroleaction_logprobe.dis.txt"
        $beforeRoleActionLogShapeCheck = Test-BattleBeforeRoleActionLogShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $beforeRoleActionLogDis
        $beforeRoleActionLogShapeFailures = @($beforeRoleActionLogShapeCheck.failures).Count
        if (-not $beforeRoleActionLogShapeCheck.ok) {
            $failed = $true
            foreach ($msg in $beforeRoleActionLogShapeCheck.failures) {
                Write-Warning "[battle beforeroleaction logshape] $msg"
            }
        }

        $beforeRoleActionLogProbe = Join-Path $outAbs "battle.beforeroleaction_logprobe.log"
        $beforeRoleActionLogHarnessCheck = Test-BattleBeforeRoleActionLogHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $beforeRoleActionLogProbe
        $beforeRoleActionLogHarnessFailures = if ($beforeRoleActionLogHarnessCheck.ok) { 0 } else { 1 }
        if (-not $beforeRoleActionLogHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle beforeroleaction logprobe] failed exit=$($beforeRoleActionLogHarnessCheck.exit_code): $beforeRoleActionLogProbe"
        }

        $beforeRoleActionFailures = $beforeRoleActionShapeFailures + $beforeRoleActionHarnessFailures + $beforeRoleActionHasBuffFailures + $beforeRoleActionNeedRefreshFailures + $minusSkillCdShapeFailures + $minusSkillCdHarnessFailures + $beforeRoleActionLogShapeFailures + $beforeRoleActionLogHarnessFailures

        $beforeSkillAnimationDis = Join-Path $outAbs "battle.proto177.beforeskillanimation.dis.txt"
        $beforeSkillAnimationShapeCheck = Test-BattleBeforeSkillAnimationCallbackShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $beforeSkillAnimationDis
        $beforeSkillAnimationShapeFailures = @($beforeSkillAnimationShapeCheck.failures).Count
        if (-not $beforeSkillAnimationShapeCheck.ok) {
            $failed = $true
            foreach ($msg in $beforeSkillAnimationShapeCheck.failures) {
                Write-Warning "[battle beforeskillanimation shape] $msg"
            }
        }

        $beforeSkillAnimationLog = Join-Path $outAbs "battle.beforeskillanimation_callback.log"
        $beforeSkillAnimationHarnessCheck = Test-BattleBeforeSkillAnimationCallbackHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $beforeSkillAnimationLog
        $beforeSkillAnimationHarnessFailures = if ($beforeSkillAnimationHarnessCheck.ok) { 0 } else { 1 }
        if (-not $beforeSkillAnimationHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[battle beforeskillanimation harness] failed exit=$($beforeSkillAnimationHarnessCheck.exit_code): $beforeSkillAnimationLog"
        }

        $beforeSkillAnimationFailures = $beforeSkillAnimationShapeFailures + $beforeSkillAnimationHarnessFailures
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
        rest_fail           = $restHarnessFailures
        beforeroleaction_fail = $beforeRoleActionFailures
        beforeskillanimation_fail = $beforeSkillAnimationFailures
        log_path            = $logPath
        out_path            = $outPath
    }
}

$gameEngineName = "GameEngine.lua"
$gameEnginePath = Join-Path $BytecodeDir $gameEngineName
if (Test-Path $gameEnginePath) {
    $outPath = Join-Path $outAbs "$gameEngineName.fr2.bin"
    $logPath = Join-Path $outAbs "$gameEngineName.dbg.log"
    $inWsl = Convert-ToWslPath $gameEnginePath
    $outWsl = Convert-ToWslPath $outPath
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcconv_cli_wsl_dbg '$inWsl' '$outWsl' 1 > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    $converted = Get-LogCount $logPath "converted "
    $convFail = Get-LogCount $logPath "bytecode conversion failed"
    $gameEngineDis = Join-Path $outAbs "gameengine.proto108.executenextstoryaction.dis.txt"
    $gameEngineCheck = Test-GameEngineExecuteNextStoryActionCallbackWindows -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $gameEngineDis
    if ($code -ne 0 -or $convFail -gt 0 -or $converted -eq 0 -or -not $gameEngineCheck.ok) {
        $failed = $true
    }
    if (-not $gameEngineCheck.ok) {
        foreach ($msg in $gameEngineCheck.failures) {
            Write-Warning "[gameengine story callback shape] $msg"
        }
    }
}

$buffName = "buff.lua"
$buffPath = Join-Path $BytecodeDir $buffName
if (Test-Path $buffPath) {
    $outPath = Join-Path $outAbs "$buffName.fr2.bin"
    $logPath = Join-Path $outAbs "$buffName.dbg.log"
    $inWsl = Convert-ToWslPath $buffPath
    $outWsl = Convert-ToWslPath $outPath
    $logWsl = Convert-ToWslPath $logPath
    $runCmd = @(
        "cd '$repoRootWsl'",
        "./tools/bcconv_cli_wsl_dbg '$inWsl' '$outWsl' 1 > '$logWsl' 2>&1"
    ) -join " && "
    $code = Invoke-WslBash $runCmd
    $converted = Get-LogCount $logPath "converted "
    $convFail = Get-LogCount $logPath "bytecode conversion failed"
    $copyFallback = Get-LogCount $logPath "copy-fallback"

    $buffShapeDis = Join-Path $outAbs "buff.proto0.onroundbuff_maxcall.dis.txt"
    $buffShapeCheck = Test-BuffOnRoundBuffMaxCallShape -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $buffShapeDis
    $buffShapeFailures = @($buffShapeCheck.failures).Count
    if (-not $buffShapeCheck.ok) {
        $failed = $true
        foreach ($msg in $buffShapeCheck.failures) {
            Write-Warning "[buff onroundbuff shape] $msg"
        }
    }

    $buffHarnessLog = Join-Path $outAbs "buff.onroundbuff_maxcall.log"
    $buffHarnessCheck = Test-BuffOnRoundBuffMaxCallHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $buffHarnessLog
    $buffHarnessFailures = if ($buffHarnessCheck.ok) { 0 } else { 1 }
    if (-not $buffHarnessCheck.ok) {
        $failed = $true
        Write-Warning "[buff onroundbuff harness] failed exit=$($buffHarnessCheck.exit_code): $buffHarnessLog"
    }

    $buffRecoveryHarnessLog = Join-Path $outAbs "buff.onroundbuff_recoverymin.log"
    $buffRecoveryHarnessCheck = Test-BuffOnRoundBuffRecoveryMinHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $buffRecoveryHarnessLog
    $buffRecoveryHarnessFailures = if ($buffRecoveryHarnessCheck.ok) { 0 } else { 1 }
    if (-not $buffRecoveryHarnessCheck.ok) {
        $failed = $true
        Write-Warning "[buff onroundbuff recovery harness] failed exit=$($buffRecoveryHarnessCheck.exit_code): $buffRecoveryHarnessLog"
    }

    if ($code -ne 0 -or $convFail -gt 0 -or $converted -eq 0) {
        $failed = $true
    }

    $summary += [pscustomobject]@{
        file                = $buffName
        exit_code           = $code
        converted_lines     = $converted
        conversion_failed   = $convFail
        register_overflow   = 0
        kstr_tdup_rejects   = 0
        table_style_guards  = 0
        copy_fallback_hits  = $copyFallback
        workflow_shift_hits = 0
        workflow_shift_fail = 0
        doalltrigger_fail   = 0
        registerprevrole_fail = 0
        rest_fail           = 0
        beforeroleaction_fail = 0
        beforeskillanimation_fail = 0
        onroundbuff_fail    = ($buffShapeFailures + $buffHarnessFailures + $buffRecoveryHarnessFailures)
        log_path            = $logPath
        out_path            = $outPath
    }
}

$attacklogicName = "AttackLogic.lua"
$attacklogicPath = Join-Path $BytecodeDir $attacklogicName
if (Test-Path $attacklogicPath) {
    $outPath = Join-Path $outAbs "$attacklogicName.fr2.bin"
    $logPath = Join-Path $outAbs "$attacklogicName.dbg.log"
    $inWsl = Convert-ToWslPath $attacklogicPath
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
    $copyFallback = Get-LogCount $logPath "copy-fallback"

    $shapeDis = Join-Path $outAbs "attacklogic.proto389.tempvalue.dis.txt"
    $shapeCheck = Test-AttackLogicTempValueArgShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $shapeDis
    $shapeFailures = @($shapeCheck.failures).Count
    if (-not $shapeCheck.ok) {
        $failed = $true
        foreach ($msg in $shapeCheck.failures) {
            Write-Warning "[attacklogic tempvalue shape] $msg"
        }
    }

    $extend3ShapeDis = Join-Path $outAbs "attacklogic.proto389.extend3.dis.txt"
    $extend3ShapeCheck = Test-AttackLogicExtend3ArgShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extend3ShapeDis
    $extend3PrevRoleDefDis = Join-Path $outAbs "attacklogic.proto364.extend3_prevroledef_call.dis.txt"
    $extend3PrevRoleDefCheck = Test-AttackLogicExtend3PrevRoleDefCallWindow -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extend3PrevRoleDefDis
    $extend3ShapeFailures = @($extend3ShapeCheck.failures).Count + @($extend3PrevRoleDefCheck.failures).Count
    if (-not $extend3ShapeCheck.ok) {
        $failed = $true
        foreach ($msg in $extend3ShapeCheck.failures) {
            Write-Warning "[attacklogic extend3 shape] $msg"
        }
    }
    if (-not $extend3PrevRoleDefCheck.ok) {
        $failed = $true
        foreach ($msg in $extend3PrevRoleDefCheck.failures) {
            Write-Warning "[attacklogic extend3 prevroledef call] $msg"
        }
    }

    $roleValuesShapeDis = Join-Path $outAbs "attacklogic.proto389.rolevalues.dis.txt"
    $roleValuesShapeCheck = Test-AttackLogicRoleValuesArgShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $roleValuesShapeDis
    $roleValuesDefCallbackDis = Join-Path $outAbs "attacklogic.proto365.rolevalues_defcallback.dis.txt"
    $roleValuesDefCallbackCheck = Test-AttackLogicRoleValuesDefCallbackCallWindow -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $roleValuesDefCallbackDis
    $roleValuesShapeFailures = @($roleValuesShapeCheck.failures).Count + @($roleValuesDefCallbackCheck.failures).Count
    if (-not $roleValuesShapeCheck.ok) {
        $failed = $true
        foreach ($msg in $roleValuesShapeCheck.failures) {
            Write-Warning "[attacklogic rolevalues shape] $msg"
        }
    }
    if (-not $roleValuesDefCallbackCheck.ok) {
        $failed = $true
        foreach ($msg in $roleValuesDefCallbackCheck.failures) {
            Write-Warning "[attacklogic rolevalues def callback shape] $msg"
        }
    }

    $roleValuesLogDis = Join-Path $outAbs "attacklogic.proto365.rolevalueslog.dis.txt"
    $roleValuesLogCheck = Test-AttackLogicRoleValuesLogShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $roleValuesLogDis
    $roleValuesLogFailures = @($roleValuesLogCheck.failures).Count
    if (-not $roleValuesLogCheck.ok) {
        $failed = $true
        foreach ($msg in $roleValuesLogCheck.failures) {
            Write-Warning "[attacklogic rolevalues bf.log shape] $msg"
        }
    }

    $extendTalentsHasBuffDis = Join-Path $outAbs "attacklogic.proto241.extendtalents_hasbuff.dis.txt"
    $extendTalentsHasBuffCheck = Test-AttackLogicExtendTalentsHasBuffShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalentsHasBuffDis
    $extendTalentsHasBuffFailures = @($extendTalentsHasBuffCheck.failures).Count
    if (-not $extendTalentsHasBuffCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalentsHasBuffCheck.failures) {
            Write-Warning "[attacklogic extendtalents hasbuff shape] $msg"
        }
    }

    $extendTalentsBiliangDis = Join-Path $outAbs "attacklogic.proto241.extendtalents_bilianglog.dis.txt"
    $extendTalentsBiliangCheck = Test-AttackLogicExtendTalentsBiliangLogShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalentsBiliangDis
    $extendTalentsBiliangFailures = @($extendTalentsBiliangCheck.failures).Count
    if (-not $extendTalentsBiliangCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalentsBiliangCheck.failures) {
            Write-Warning "[attacklogic extendtalents biliang bf.log shape] $msg"
        }
    }

    $extendTalents2GedangDis = Join-Path $outAbs "attacklogic.proto364.extendtalents2_gedanglog.dis.txt"
    $extendTalents2GedangCheck = Test-AttackLogicExtendTalents2GedangLogShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2GedangDis
    $extendTalents2GedangFailures = @($extendTalents2GedangCheck.failures).Count
    if (-not $extendTalents2GedangCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2GedangCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 bf.log shape] $msg"
        }
    }

    $extendTalents2MissDis = Join-Path $outAbs "attacklogic.proto320.extendtalents2_misslog.dis.txt"
    $extendTalents2MissCheck = Test-AttackLogicExtendTalents2MissLogWindow -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2MissDis
    $extendTalents2MissFailures = @($extendTalents2MissCheck.failures).Count
    if (-not $extendTalents2MissCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2MissCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 miss bf.log shape] $msg"
        }
    }

    $extendTalents2ChaizhaoDis = Join-Path $outAbs "attacklogic.proto364.extendtalents2_chaizhaolog.dis.txt"
    $extendTalents2ChaizhaoCheck = Test-AttackLogicExtendTalents2ChaizhaoLogShift -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2ChaizhaoDis
    $extendTalents2ChaizhaoFailures = @($extendTalents2ChaizhaoCheck.failures).Count
    if (-not $extendTalents2ChaizhaoCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2ChaizhaoCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 chaizhao bf.log shape] $msg"
        }
    }

    $extendTalents2TalentLogCheck = Test-AttackLogicExtendTalents2TalentLogWindows -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -outDir $outAbs
    $extendTalents2TalentLogFailures = @($extendTalents2TalentLogCheck.failures).Count
    if (-not $extendTalents2TalentLogCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2TalentLogCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 talent bf.log shape] $msg"
        }
    }

    $extendTalents2CallbackDis = Join-Path $outAbs "attacklogic.proto320.extendtalents2_callbackcall.dis.txt"
    $extendTalents2CallbackCheck = Test-AttackLogicExtendTalents2CallbackCallWindow -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2CallbackDis
    $extendTalents2CallbackFailures = @($extendTalents2CallbackCheck.failures).Count
    if (-not $extendTalents2CallbackCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2CallbackCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 callback call shape] $msg"
        }
    }

    $extendTalents2RemoveDis = Join-Path $outAbs "attacklogic.proto320.extendtalents2_remove.dis.txt"
    $extendTalents2RemoveCheck = Test-AttackLogicExtendTalents2RemoveWindows -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2RemoveDis
    $extendTalents2RemoveFailures = @($extendTalents2RemoveCheck.failures).Count
    if (-not $extendTalents2RemoveCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2RemoveCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 remove shape] $msg"
        }
    }

    $extendTalents2XiLogDis = Join-Path $outAbs "attacklogic.proto367.extendtalents2_xilog.dis.txt"
    $extendTalents2XiLogCheck = Test-AttackLogicExtendTalents2XiLogWindow -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2XiLogDis
    $extendTalents2XiLogFailures = @($extendTalents2XiLogCheck.failures).Count
    if (-not $extendTalents2XiLogCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2XiLogCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 xi bf.log shape] $msg"
        }
    }

    $extendTalents2XiValueDis = Join-Path $outAbs "attacklogic.proto320.extendtalents2_xivalue_min.dis.txt"
    $extendTalents2XiValueCheck = Test-AttackLogicExtendTalents2XiValueMinWindows -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2XiValueDis
    $extendTalents2XiValueFailures = @($extendTalents2XiValueCheck.failures).Count
    if (-not $extendTalents2XiValueCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2XiValueCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 xivalue min shape] $msg"
        }
    }

    $extendTalents2YihuaDis = Join-Path $outAbs "attacklogic.proto320.extendtalents2_yihua_log.dis.txt"
    $extendTalents2YihuaCheck = Test-AttackLogicExtendTalents2YihuaLogWindows -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -disPath $extendTalents2YihuaDis
    $extendTalents2YihuaFailures = @($extendTalents2YihuaCheck.failures).Count
    if (-not $extendTalents2YihuaCheck.ok) {
        $failed = $true
        foreach ($msg in $extendTalents2YihuaCheck.failures) {
            Write-Warning "[attacklogic extendtalents2 yihua bf.log shape] $msg"
        }
    }

    $harnessLog = Join-Path $outAbs "attacklogic.tempvalue.log"
    $harnessCheck = Test-AttackLogicTempValueHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $harnessLog
    $harnessFailures = if ($harnessCheck.ok) { 0 } else { 1 }
    if (-not $harnessCheck.ok) {
        $failed = $true
        Write-Warning "[attacklogic tempvalue] harness failed exit=$($harnessCheck.exit_code): $harnessLog"
    }

    $chainHarnessFailures = 0
    $extendTalents2LogHarnessFailures = 0
    $extendTalents2MissHarnessFailures = 0
    $extendTalents2YihuaHarnessFailures = 0
    $extendTalents2FullProbeFailures = 0
    $extendTalents2MissLog = Join-Path $outAbs "attacklogic.extendtalents2.misslog.log"
    $extendTalents2MissHarnessCheck = Test-AttackLogicExtendTalents2MissLogHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $extendTalents2MissLog
    $extendTalents2MissHarnessFailures = if ($extendTalents2MissHarnessCheck.ok) { 0 } else { 1 }
    if (-not $extendTalents2MissHarnessCheck.ok) {
        $failed = $true
        Write-Warning "[attacklogic extendtalents2 misslog] harness failed exit=$($extendTalents2MissHarnessCheck.exit_code): $extendTalents2MissLog"
    }
    $extendTalents2FullProbeLog = Join-Path $outAbs "attacklogic.extendtalents2.fullprobe.log"
    $extendTalents2FullProbeCheck = Test-AttackLogicExtendTalents2FullProbeHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $extendTalents2FullProbeLog
    $extendTalents2FullProbeFailures = if ($extendTalents2FullProbeCheck.ok) { 0 } else { 1 }
    if (-not $extendTalents2FullProbeCheck.ok) {
        $failed = $true
        Write-Warning "[attacklogic extendtalents2 fullprobe] harness failed exit=$($extendTalents2FullProbeCheck.exit_code): $extendTalents2FullProbeLog"
    }
    $extendTalents2RemoveLog = Join-Path $outAbs "attacklogic.extendtalents2.removeprobe.log"
    $extendTalents2RemoveHarnessCheck = Test-AttackLogicExtendTalents2RemoveProbeHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $extendTalents2RemoveLog
    $extendTalents2RemoveHarnessFailures = if ($extendTalents2RemoveHarnessCheck.ok) { 0 } else { 1 }
    if (-not $extendTalents2RemoveHarnessCheck.ok) {
        $failed = $true
        Write-Warning "[attacklogic extendtalents2 removeprobe] harness failed exit=$($extendTalents2RemoveHarnessCheck.exit_code): $extendTalents2RemoveLog"
    }
    $extendTalents2XiLog = Join-Path $outAbs "attacklogic.extendtalents2.xilog.log"
    $extendTalents2XiLogHarnessCheck = Test-AttackLogicExtendTalents2XiLogHarness -repoRootWsl $repoRootWsl -bytecodeWsl $outWsl -logPath $extendTalents2XiLog
    $extendTalents2XiLogHarnessFailures = if ($extendTalents2XiLogHarnessCheck.ok) { 0 } else { 1 }
    if (-not $extendTalents2XiLogHarnessCheck.ok) {
        $failed = $true
        Write-Warning "[attacklogic extendtalents2 xilog] harness failed exit=$($extendTalents2XiLogHarnessCheck.exit_code): $extendTalents2XiLog"
    }

    if ($battleOutPath -and (Test-Path $battleOutPath)) {
        $chainHarnessLog = Join-Path $outAbs "attacklogic.tempvalue_chain.log"
        $chainHarnessCheck = Test-AttackLogicTempValueChainHarness -repoRootWsl $repoRootWsl -battleBytecodeWsl (Convert-ToWslPath $battleOutPath) -attacklogicBytecodeWsl $outWsl -logPath $chainHarnessLog
        $chainHarnessFailures = if ($chainHarnessCheck.ok) { 0 } else { 1 }
        if (-not $chainHarnessCheck.ok) {
            $failed = $true
            Write-Warning "[attacklogic tempvalue chain] harness failed exit=$($chainHarnessCheck.exit_code): $chainHarnessLog"
        }
    }

    # The combo harness is back in gating only because it now drives the
    # RoleValuesDef callback path that single-file probes missed; static proto365
    # window checks remain the primary proof point for the exact call shape.
    # The misslog harness stays in gating because it reuses the same
    # single-file stub as fullprobe, but forces result.Hp==0 and proves the
    # early line6346 branch that fullprobe does not pin.
    # The xixing callback harness stays diagnostic-only for now: the single-file
    # loader still aborts early at tmp.lua:26986 before that workflow is
    # registered, so it can prove stub coverage gaps but not runtime regressions.
    # The dedicated yihua harness still stays out of gating because loading
    # AttackLogic alone aborts at tmp.lua:26986 before the later workflow
    # registrations are complete, so the runtime callback for line 6735 is
    # absent from BattleUtil.GetWorkflowForTalent in offline single-file mode.

    if ($code -ne 0 -or $convFail -gt 0 -or $converted -eq 0) {
        $failed = $true
    }

    $summary += [pscustomobject]@{
        file                  = $attacklogicName
        exit_code             = $code
        converted_lines       = $converted
        conversion_failed     = $convFail
        register_overflow     = $regOverflow
        kstr_tdup_rejects     = 0
        table_style_guards    = 0
        copy_fallback_hits    = $copyFallback
        workflow_shift_hits   = 0
        workflow_shift_fail   = 0
        doalltrigger_fail     = 0
        registerprevrole_fail = 0
        rest_fail             = 0
        beforeroleaction_fail = 0
        tempvalue_shape_fail  = $shapeFailures
        extend3_shape_fail    = $extend3ShapeFailures
        rolevalues_shape_fail = $roleValuesShapeFailures
        rolevalues_log_fail   = $roleValuesLogFailures
        extendtalents_shape_fail = ($extendTalentsHasBuffFailures + $extendTalentsBiliangFailures + $extendTalents2GedangFailures + $extendTalents2MissFailures + $extendTalents2ChaizhaoFailures + $extendTalents2TalentLogFailures + $extendTalents2CallbackFailures + $extendTalents2RemoveFailures + $extendTalents2XiLogFailures + $extendTalents2XiValueFailures + $extendTalents2YihuaFailures)
        tempvalue_fail        = $harnessFailures
        tempvalue_chain_fail  = $chainHarnessFailures
        extendtalents2_log_fail = ($extendTalents2LogHarnessFailures + $extendTalents2MissHarnessFailures + $extendTalents2YihuaHarnessFailures + $extendTalents2FullProbeFailures + $extendTalents2RemoveHarnessFailures + $extendTalents2XiLogHarnessFailures)
        log_path              = $logPath
        out_path              = $outPath
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
        if ($row.file -eq "AttackLogic.lua") {
            if ($row.PSObject.Properties.Name -contains 'tempvalue_shape_fail' -and $row.tempvalue_shape_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'extend3_shape_fail' -and $row.extend3_shape_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'rolevalues_shape_fail' -and $row.rolevalues_shape_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'rolevalues_log_fail' -and $row.rolevalues_log_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'extendtalents_shape_fail' -and $row.extendtalents_shape_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'tempvalue_fail' -and $row.tempvalue_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'tempvalue_chain_fail' -and $row.tempvalue_chain_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'extendtalents2_log_fail' -and $row.extendtalents2_log_fail -ne 0) {
                $failed = $true
            }
            continue
        }
        if ($row.file -eq "buff.lua") {
            if ($row.PSObject.Properties.Name -contains 'onroundbuff_fail' -and $row.onroundbuff_fail -ne 0) {
                $failed = $true
            }
            continue
        }
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
            if ($row.PSObject.Properties.Name -contains 'rest_fail' -and $row.rest_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'beforeroleaction_fail' -and $row.beforeroleaction_fail -ne 0) {
                $failed = $true
            }
            if ($row.PSObject.Properties.Name -contains 'beforeskillanimation_fail' -and $row.beforeskillanimation_fail -ne 0) {
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

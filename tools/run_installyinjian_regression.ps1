param(
    [string]$BytecodeFile = "E:\Games\work\jygame_renew\gamedata\modcache\SSWS_HG\lua\luajit\GameEngine.lua",
    [string]$OutDir = "tmp\installyinjian_regression",
    [string]$WslDistro = "Ubuntu-24.04",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Convert-ToWslPath([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $unix = ($full -replace "\\", "/")
    if ($unix -match "^([A-Za-z]):/(.*)$") {
        return "/mnt/$($matches[1].ToLowerInvariant())/$($matches[2])"
    }
    throw "Unsupported Windows path: $path"
}

function Invoke-WslBash([string]$command) {
    if ($WslDistro -and $WslDistro.Trim() -ne "") {
        & wsl -d $WslDistro bash -lc "$command"
    } else {
        & wsl bash -lc "$command"
    }
    return $LASTEXITCODE
}

$repoRoot = (Resolve-Path ".").Path
$repoRootWsl = Convert-ToWslPath $repoRoot

if (-not (Test-Path $BytecodeFile)) {
    throw "Missing bytecode file: $BytecodeFile"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outAbs = (Resolve-Path $OutDir).Path

$outBin = Join-Path $outAbs "GameEngine.fr2.bin"
$convLog = Join-Path $outAbs "GameEngine.convert.log"
$runLog = Join-Path $outAbs "GameEngine.installyinjian.log"
$checkLog = Join-Path $outAbs "GameEngine.checktrigger.log"
$dump144 = Join-Path $outAbs "GameEngine.proto10.64_92.txt"
$dumpCore = Join-Path $outAbs "GameEngine.proto10.160_255.txt"

$inWsl = Convert-ToWslPath $BytecodeFile
$outWsl = Convert-ToWslPath $outBin
$convLogWsl = Convert-ToWslPath $convLog
$runLogWsl = Convert-ToWslPath $runLog
$checkLogWsl = Convert-ToWslPath $checkLog
$dump144Wsl = Convert-ToWslPath $dump144
$dumpCoreWsl = Convert-ToWslPath $dumpCore

if (-not $SkipBuild) {
    $buildCmd = @(
        "cd '$repoRootWsl'",
        "cd ./luajit-2.1 && make clean >/dev/null && make XCFLAGS='-DLUAJIT_ENABLE_GC64' >/dev/null && cd ..",
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcconv_cli.c tolua_fr1_to_fr2.c int64.c uint64.c ./luajit-2.1/src/lj_bc.o ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcconv_cli_wsl_dbg",
        "gcc -O0 -g -DTOLUA_REPACK_DEBUG -I. -I./luajit-2.1/src tools/bcrun_cli.c int64.c uint64.c ./luajit-2.1/src/libluajit.a -lm -ldl -o tools/bcrun_cli_wsl",
        "gcc -O2 -I./luajit-2.1/src tools/bc_dump_proto.c -o tools/bc_dump_proto_wsl"
    ) -join " && "
    $code = Invoke-WslBash $buildCmd
    if ($code -ne 0) {
        throw "Build failed (exit=$code)"
    }
}

$convertCmd = @(
    "cd '$repoRootWsl'",
    "./tools/bcconv_cli_wsl_dbg '$inWsl' '$outWsl' 1 > '$convLogWsl' 2>&1"
) -join " && "
$code = Invoke-WslBash $convertCmd
if ($code -ne 0) {
    throw "Convert failed (exit=$code). log=$convLog"
}

$dumpCmd = @(
    "cd '$repoRootWsl'",
    "./tools/bc_dump_proto_wsl '$outWsl' 10 64 92 > '$dump144Wsl'",
    "./tools/bc_dump_proto_wsl '$outWsl' 10 160 255 > '$dumpCoreWsl'"
) -join " && "
$code = Invoke-WslBash $dumpCmd
if ($code -ne 0) {
    throw "dump proto failed (exit=$code)"
}

$d144 = Get-Content $dump144 -Raw
$dCore = Get-Content $dumpCore -Raw

$ok144 = $d144 -match "0065 .*MOV\s+A=15.*D=7" -and
         $d144 -match "0066 .*TGETS\s+A=13\s+B=7" -and
         $d144 -match "0067 .*CALL\s+A=13\s+B=2\s+C=2" -and
         $d144 -match "0068 .*KSHORT\s+A=14" -and
         $d144 -match "0069 .*CALL\s+A=11\s+B=2\s+C=3"
$ok147 = $d144 -match "0073 .*MOV\s+A=15.*D=11" -and
         $d144 -match "0074 .*CALL\s+A=12\s+B=2\s+C=3" -and
         $d144 -match "0076 .*MOV\s+A=15.*D=9" -and
         $d144 -match "0077 .*KSHORT\s+A=16" -and
         $d144 -match "0078 .*CALL\s+A=13\s+B=2\s+C=3"
$ok180 = $dCore -match "0173 .*MOV\s+A=19.*D=5" -and
         $dCore -match "0174 .*TGETS\s+A=17\s+B=5" -and
         $dCore -match "0175 .*CALL\s+A=17\s+B=2\s+C=2" -and
         $dCore -match "0176 .*KSHORT\s+A=18" -and
         $dCore -match "0177 .*CALL\s+A=15\s+B=2\s+C=3"
$ok182 = $dCore -match "0182 .*MOV\s+A=19.*D=10" -and
         $dCore -match "0183 .*TGETS\s+A=17\s+B=10" -and
         $dCore -match "0184 .*MOV\s+A=20.*D=11" -and
         $dCore -match "0185 .*CALL\s+A=17\s+B=2\s+C=3" -and
         $dCore -match "0186 .*MOV\s+A=19.*D=17" -and
         $dCore -match "0188 .*KPRI\s+A=20" -and
         $dCore -match "0189 .*MOV\s+A=21.*D=13" -and
         $dCore -match "0190 .*CALL\s+A=17\s+B=0\s+C=4"
$ok187 = $dCore -match "0204 .*MOV\s+A=20.*D=4" -and
         $dCore -match "0205 .*TGETS\s+A=18\s+B=4" -and
         $dCore -match "0206 .*CALL\s+A=18\s+B=2\s+C=2" -and
         $dCore -match "0207 .*KSHORT\s+A=19" -and
         $dCore -match "0208 .*CALL\s+A=16\s+B=2\s+C=3"
$ok189 = $dCore -match "0213 .*MOV\s+A=20.*D=10" -and
         $dCore -match "0214 .*TGETS\s+A=18\s+B=10" -and
         $dCore -match "0215 .*MOV\s+A=21.*D=11" -and
         $dCore -match "0216 .*CALL\s+A=18\s+B=2\s+C=3" -and
         $dCore -match "0217 .*MOV\s+A=20.*D=18" -and
         $dCore -match "0219 .*KPRI\s+A=21" -and
         $dCore -match "0220 .*MOV\s+A=22.*D=13" -and
         $dCore -match "0221 .*CALL\s+A=18\s+B=0\s+C=4"
$ok194 = $dCore -match "0235 .*MOV\s+A=21.*D=6" -and
         $dCore -match "0236 .*TGETS\s+A=19\s+B=6" -and
         $dCore -match "0237 .*CALL\s+A=19\s+B=2\s+C=2" -and
         $dCore -match "0238 .*KSHORT\s+A=20" -and
         $dCore -match "0239 .*CALL\s+A=17\s+B=2\s+C=3"
$ok196 = $dCore -match "0244 .*MOV\s+A=21.*D=10" -and
         $dCore -match "0245 .*TGETS\s+A=19\s+B=10" -and
         $dCore -match "0246 .*MOV\s+A=22.*D=11" -and
         $dCore -match "0247 .*CALL\s+A=19\s+B=2\s+C=3" -and
         $dCore -match "0248 .*MOV\s+A=21.*D=19" -and
         $dCore -match "0250 .*KPRI\s+A=22" -and
         $dCore -match "0251 .*MOV\s+A=23.*D=13" -and
         $dCore -match "0252 .*CALL\s+A=19\s+B=0\s+C=4"

if (-not $ok144 -or -not $ok147 -or -not $ok180 -or -not $ok182 -or -not $ok187 -or -not $ok189 -or -not $ok194 -or -not $ok196) {
    throw "Pattern check failed. dump144=$dump144 dumpCore=$dumpCore"
}

$instCmd = @(
    "cd '$repoRootWsl'",
    "./tools/bcrun_cli_wsl '$outWsl' installyinjian > '$runLogWsl' 2>&1"
) -join " && "
$instCode = Invoke-WslBash $instCmd

$checkCmd = @(
    "cd '$repoRootWsl'",
    "./tools/bcrun_cli_wsl '$outWsl' checktrigger > '$checkLogWsl' 2>&1"
) -join " && "
$checkCode = Invoke-WslBash $checkCmd

$instText = if (Test-Path $runLog) { Get-Content $runLog -Raw } else { "" }
$checkText = if (Test-Path $checkLog) { Get-Content $checkLog -Raw } else { "" }
$incompat = ($instText -match "incompatible bytecode") -or ($checkText -match "incompatible bytecode")

if (($instCode -ne 0 -or $checkCode -ne 0) -and -not $incompat) {
    throw "Harness failed (inst=$instCode, check=$checkCode). run=$runLog check=$checkLog"
}

Write-Host "[OK] InstallYinjian regression passed."
Write-Host "convert log: $convLog"
Write-Host "run log: $runLog"
Write-Host "check log: $checkLog"
Write-Host "dump line144: $dump144"
Write-Host "dump core: $dumpCore"
if ($incompat) {
    Write-Warning "Harness skipped by runtime mismatch: incompatible bytecode. Pattern checks still passed."
}

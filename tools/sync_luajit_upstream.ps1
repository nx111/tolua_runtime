param(
  [string]$Upstream = "https://github.com/LuaJIT/LuaJIT",
  [string]$Ref = "v2.1"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Git {
  param(
    [Parameter(Mandatory = $true)][string[]]$Args,
    [string]$WorkDir
  )

  if ($WorkDir) {
    Push-Location $WorkDir
  }

  try {
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & git @Args 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference
    if ($exitCode -ne 0) {
      throw (($output | Out-String).Trim())
    }
    return ($output | Out-String).Trim()
  } finally {
    if ($WorkDir) {
      Pop-Location
    }
  }
}

function Invoke-RobocopyMirror {
  param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Target
  )

  & robocopy $Source $Target /MIR /NFL /NDL /NJH /NJS /NP /XD .git | Out-Null
  if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
  }
}

function Resolve-GitRef {
  param(
    [Parameter(Mandatory = $true)][string]$RepoDir,
    [Parameter(Mandatory = $true)][string]$RefName
  )

  $candidates = @(
    "refs/remotes/origin/$RefName",
    "refs/heads/$RefName",
    $RefName
  )

  foreach ($candidate in $candidates) {
    try {
      return (Invoke-Git -Args @("-C", $RepoDir, "rev-parse", $candidate))
    } catch {
    }
  }

  throw "Unable to resolve git ref: $RefName"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$tmpRoot = Join-Path $repoRoot "tmp"
$cacheDir = Join-Path $tmpRoot "luajit-upstream-cache"
$stageDir = Join-Path $tmpRoot "luajit-upstream-stage"
$archiveZip = Join-Path $tmpRoot "luajit-upstream-sync.zip"
$targetDir = Join-Path $repoRoot "luajit-2.1"
$targetPrefix = Split-Path -Leaf $targetDir
$patchDir = Join-Path $PSScriptRoot "luajit-upstream-patches"
$metaPath = Join-Path $targetDir ".upstream-sync"

if (!(Test-Path $tmpRoot)) {
  New-Item -ItemType Directory -Path $tmpRoot | Out-Null
}

if (Test-Path (Join-Path $cacheDir ".git")) {
  Invoke-Git -Args @("-C", $cacheDir, "remote", "set-url", "origin", $Upstream)
  Invoke-Git -Args @("-C", $cacheDir, "fetch", "--prune", "origin")
} else {
  if (Test-Path $cacheDir) {
    Remove-Item -Recurse -Force $cacheDir
  }
  Invoke-Git -Args @("clone", $Upstream, $cacheDir)
}

$commit = Resolve-GitRef -RepoDir $cacheDir -RefName $Ref
$relver = Invoke-Git -Args @("-C", $cacheDir, "show", "-s", "--format=%ct", $commit)

if (Test-Path $archiveZip) {
  Remove-Item -Force $archiveZip
}
Invoke-Git -Args @("-C", $cacheDir, "archive", "--format=zip", "--output", $archiveZip, $commit)

if (Test-Path $stageDir) {
  Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Path $stageDir | Out-Null
Expand-Archive -Path $archiveZip -DestinationPath $stageDir

Invoke-RobocopyMirror -Source $stageDir -Target $targetDir

$patches = Get-ChildItem -LiteralPath $patchDir -Filter "*.patch" | Sort-Object Name
foreach ($patch in $patches) {
  Invoke-Git -Args @("-C", $repoRoot, "apply", "--whitespace=nowarn", "--ignore-space-change", "--ignore-whitespace", "--directory=$targetPrefix", $patch.FullName)
}

$meta = @(
  "url=$Upstream"
  "ref=$Ref"
  "commit=$commit"
  "relver=$relver"
) -join "`n"
Set-Content -LiteralPath $metaPath -Value $meta -NoNewline

Write-Host "LuaJIT synced to $commit (relver $relver)"

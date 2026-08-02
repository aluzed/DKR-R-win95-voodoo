[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$rspRecompiler = Get-ChildItem `
    -LiteralPath (Join-Path $projectRoot 'build\runtime-tools\n64recomp') `
    -Filter 'RSPRecomp.exe' `
    -File `
    -Recurse | Select-Object -First 1
if (-not $rspRecompiler) {
    throw 'RSPRecomp.exe is missing. Run Prepare-DKR-Runtime.cmd first.'
}

$rom = Join-Path $projectRoot 'extern\dkr-decomp\build\dkr.us.v77.z64'
if (-not (Test-Path -LiteralPath $rom -PathType Leaf)) {
    throw "The matching DKR ROM build is missing: $rom"
}

$outputDirectory = Join-Path $projectRoot 'runtime-recomp\RecompiledRSP'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$configs = @(Get-ChildItem `
    -LiteralPath (Join-Path $projectRoot 'runtime-recomp\rsp') `
    -Filter '*.toml' `
    -File | Sort-Object Name)
if ($configs.Count -eq 0) {
    throw 'No DKR RSP recompilation configurations were found.'
}

foreach ($config in $configs) {
    Write-Host "[RSP] $($config.Name)"
    & $rspRecompiler.FullName $config.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "RSP recompilation failed for $($config.FullName)"
    }
}

$generated = @(Get-ChildItem -LiteralPath $outputDirectory -Filter '*.cpp' -File)
if ($generated.Count -ne $configs.Count) {
    throw "Expected $($configs.Count) generated RSP sources, found $($generated.Count)."
}
Write-Host "[OK] Generated $($generated.Count) DKR RSP translation unit(s)."

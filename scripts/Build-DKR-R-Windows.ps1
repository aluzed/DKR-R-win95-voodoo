[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDirectory = Join-Path $projectRoot 'build\dkr-runtime-rt64'
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$cmakeCandidates = @(
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
)
$cmake = $cmakeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($cmake)) {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw 'Native Windows CMake was not found. Install Visual Studio 2022 Desktop development with C++.'
    }
    $cmake = $command.Source
}
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
    throw "CTest was not found beside CMake: $ctest"
}

foreach ($required in @(
    'runtime-recomp\RecompiledFuncs',
    'runtime-recomp\RecompiledRSP',
    'extern\n64-modern-runtime',
    'extern\rt64'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $required))) {
        throw "Required runtime input is missing: $required. Run Build-DKR-Runtime.cmd first."
    }
}

if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
    $resolvedRoot = (Resolve-Path -LiteralPath $projectRoot).Path
    if (-not $resolvedBuild.StartsWith($resolvedRoot + [IO.Path]::DirectorySeparatorChar,
                                       [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory outside the project: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

Write-Host "Configuring DKR-R $version ($Configuration)..." -ForegroundColor Cyan
& $cmake -S (Join-Path $projectRoot 'runtime-recomp') -B $buildDirectory `
    -G 'Visual Studio 17 2022' -A x64 `
    "-DDKRPORT_ROOT=$projectRoot" `
    "-DDKR_RELEASE_VERSION=$version" `
    -DDKR_RUNTIME_BUILD_GENERATED=ON `
    -DDKR_RUNTIME_BUILD_RT64=ON
if ($LASTEXITCODE -ne 0) { throw 'DKR-R CMake configuration failed.' }

& $cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'DKR-R compilation failed.' }

if (-not $SkipTests) {
    & $ctest --test-dir $buildDirectory -C $Configuration --output-on-failure -R '^DKR'
    if ($LASTEXITCODE -ne 0) { throw 'One or more DKR-R regression tests failed.' }
}

$binary = Join-Path $buildDirectory "bin\$Configuration\DKR-R.exe"
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "The build completed without producing $binary"
}
$pakTest = Join-Path ([IO.Path]::GetTempPath()) ("dkr-r-pak-test-" + [Guid]::NewGuid())
try {
    # DKR-R is a Windows GUI subsystem executable. PowerShell's call operator
    # does not reliably wait for those processes, so use Process directly and
    # validate the real exit code before declaring the build ready.
    $selfTestInfo = [Diagnostics.ProcessStartInfo]::new()
    $selfTestInfo.FileName = $binary
    $selfTestInfo.Arguments = "--self-test-pak `"$pakTest`""
    $selfTestInfo.UseShellExecute = $false
    $selfTestInfo.CreateNoWindow = $true
    $selfTestProcess = [Diagnostics.Process]::Start($selfTestInfo)
    $selfTestProcess.WaitForExit()
    if ($selfTestProcess.ExitCode -ne 0) {
        throw "Virtual Controller Pak self-test failed with exit code $($selfTestProcess.ExitCode)."
    }
} finally {
    if (Test-Path -LiteralPath $pakTest) {
        Remove-Item -LiteralPath $pakTest -Recurse -Force
    }
}

if ($Package) {
    & (Join-Path $PSScriptRoot 'Package-Windows.ps1') `
        -Version $version -Configuration $Configuration `
        -BuildDirectory 'build\dkr-runtime-rt64'
    if ($LASTEXITCODE -ne 0) { throw 'Windows release packaging failed.' }
}

Write-Host "DKR-R is ready: $binary" -ForegroundColor Green

[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build\dkr-runtime-rt64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
}
$distRoot = Join-Path $projectRoot 'dist'
$resolvedBuild = Join-Path $projectRoot $BuildDirectory
$stage = Join-Path $distRoot "DKR-R-$Version-Windows-x64"
$zip = "$stage.zip"
$deniedExtensions = @('.z64', '.v64', '.n64', '.eep', '.mpk', '.sra', '.fla', '.o2r', '.otr')
$runtimeFiles = @('DKR-R.exe', 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll')

if (Test-Path -LiteralPath $stage) {
    throw "Refusing to overwrite existing release directory: $stage"
}
if (Test-Path -LiteralPath $zip) {
    throw "Refusing to overwrite existing release archive: $zip"
}

$bin = Join-Path $resolvedBuild "bin\$Configuration"
foreach ($name in $runtimeFiles) {
    $source = Join-Path $bin $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing release runtime file: $source"
    }
}

New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination (Join-Path $stage $name)
}
$logoDirectory = Join-Path $stage 'assets\ui\Icons'
New-Item -ItemType Directory -Path $logoDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R8.bmp') `
    -Destination (Join-Path $logoDirectory 'DKR-R8.bmp')
$filterDirectory = Join-Path $stage 'assets\filters'
New-Item -ItemType Directory -Path $filterDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot 'assets\filters\*.png') `
    -Destination $filterDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\RELEASE-README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.md') -Destination (Join-Path $stage 'LICENSE.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY.md') -Destination (Join-Path $stage 'THIRD_PARTY.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'runtime-recomp\COPYING-NOTICE.md') -Destination (Join-Path $stage 'COPYING-NOTICE.md')
$noticeDirectory = Join-Path $stage 'ThirdPartyLicenses'
New-Item -ItemType Directory -Path $noticeDirectory | Out-Null
$noticeFiles = [ordered]@{
    'RT64-LICENSE.txt' = 'extern\rt64\LICENSE'
    'Dear-ImGui-LICENSE.txt' = 'extern\rt64\src\contrib\imgui\LICENSE.txt'
    'SDL2-LICENSE.txt' = 'extern\rt64\src\contrib\mupen64plus-win32-deps\SDL2-2.26.3\COPYING.txt'
    'N64ModernRuntime-COPYING.txt' = 'extern\n64-modern-runtime\COPYING'
    'N64Recomp-LICENSE.txt' = 'extern\n64-modern-runtime\N64Recomp\LICENSE'
    'DXC-NOTICE.md' = 'packaging\licenses\DXC-NOTICE.md'
    'Jumpman-LICENSE.txt' = 'packaging\licenses\Jumpman-LICENSE.txt'
    'CRT-FILTERS-NOTICE.md' = 'packaging\licenses\CRT-FILTERS-NOTICE.md'
}
foreach ($entry in $noticeFiles.GetEnumerator()) {
    $source = Join-Path $projectRoot $entry.Value
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing third-party notice: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $noticeDirectory $entry.Key)
}

# Exercise the binary from the exact staged package, not only from the build
# tree. This catches a missing DLL or packaging-path regression before ZIP
# creation while keeping all test data outside the release directory.
$pakTest = Join-Path ([IO.Path]::GetTempPath()) `
    ("dkr-r-packaged-pak-" + [Guid]::NewGuid().ToString('N'))
try {
    $stagedRuntime = Join-Path $stage 'DKR-R.exe'
    $selfTestInfo = [Diagnostics.ProcessStartInfo]::new()
    $selfTestInfo.FileName = $stagedRuntime
    $selfTestInfo.Arguments = "--self-test-pak `"$pakTest`""
    $selfTestInfo.UseShellExecute = $false
    $selfTestInfo.CreateNoWindow = $true
    $selfTestProcess = [Diagnostics.Process]::Start($selfTestInfo)
    $selfTestProcess.WaitForExit()
    if ($selfTestProcess.ExitCode -ne 0) {
        throw "The staged Windows runtime failed its Controller Pak self-test with exit code $($selfTestProcess.ExitCode)."
    }
} finally {
    if (Test-Path -LiteralPath $pakTest) {
        Remove-Item -LiteralPath $pakTest -Recurse -Force
    }
}

$packagedFiles = Get-ChildItem -LiteralPath $stage -Recurse -File
$denied = @($packagedFiles | Where-Object { $deniedExtensions -contains $_.Extension.ToLowerInvariant() })
if ($denied.Count -ne 0) {
    throw "Release staging contains prohibited game data: $($denied.FullName -join ', ')"
}

$n64Headers = @('80371240', '37804012', '40123780')
foreach ($file in $packagedFiles) {
    $stream = [System.IO.File]::OpenRead($file.FullName)
    try {
        $header = New-Object byte[] 4
        $read = $stream.Read($header, 0, 4)
        if ($read -eq 4) {
            $magic = [System.BitConverter]::ToString($header).Replace('-', '')
            if ($n64Headers -contains $magic) {
                throw "Release staging contains an N64 ROM header: $($file.FullName)"
            }
        }
    } finally {
        $stream.Dispose()
    }
}

Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $badEntries = @($archive.Entries | Where-Object {
        $deniedExtensions -contains [System.IO.Path]::GetExtension($_.FullName).ToLowerInvariant()
    })
    if ($badEntries.Count -ne 0) {
        throw "Release ZIP contains prohibited game data: $($badEntries.FullName -join ', ')"
    }
} finally {
    $archive.Dispose()
}

$hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256
Write-Host "Created $zip"
Write-Host "SHA-256 $($hash.Hash)"

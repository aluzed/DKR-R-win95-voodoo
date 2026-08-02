[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$NoInstall,
    [switch]$KeepBuildTree
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptRoot
$LogDirectory = Join-Path $ProjectRoot 'build-logs'
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogPath = Join-Path $LogDirectory "build-windows-$Timestamp.log"
$BuildDirectory = Join-Path $ProjectRoot 'build\windows-x64'
$VcpkgRoot = Join-Path $ProjectRoot '.deps\vcpkg'
$DistributionDirectory = Join-Path $ProjectRoot 'dist\DKRPort-Windows-x64'
$VcpkgTag = '2026.06.24'

New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
Start-Transcript -Path $LogPath -Force | Out-Null

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Fail-Build([string]$Message, [string[]]$Suggestions = @()) {
    Write-Host "`n[BUILD ERROR] $Message" -ForegroundColor Red
    foreach ($Suggestion in $Suggestions) {
        Write-Host "  - $Suggestion" -ForegroundColor Yellow
    }
    Write-Host "`nFull build log: $LogPath" -ForegroundColor White
    throw $Message
}

function Refresh-ProcessPath {
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machinePath;$userPath"
}

function Find-Executable([string]$Name, [string[]]$Fallbacks = @()) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Ensure-Winget {
    $winget = Find-Executable 'winget.exe' @(
        (Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\winget.exe')
    )
    if (-not $winget) {
        Fail-Build 'Windows Package Manager (winget) is not installed.' @(
            'Install or update "App Installer" from the Microsoft Store.',
            'Then run Build-Windows.cmd again.',
            'Alternatively install Git, CMake and Visual Studio 2022 Build Tools manually and run with -NoInstall.'
        )
    }
    return $winget
}

function Install-WingetPackage(
    [string]$Id,
    [string]$DisplayName,
    [string]$Override = ''
) {
    if ($NoInstall) {
        Fail-Build "$DisplayName is missing and automatic installation was disabled." @(
            "Install $DisplayName manually, then run Build-Windows.cmd again without -NoInstall."
        )
    }

    $winget = Ensure-Winget
    Write-Step "Installing $DisplayName"
    $arguments = @(
        'install', '--id', $Id, '--exact',
        '--accept-package-agreements', '--accept-source-agreements',
        '--silent', '--disable-interactivity'
    )
    if ($Override) {
        $arguments += @('--override', $Override)
    }
    & $winget @arguments
    if ($LASTEXITCODE -ne 0) {
        Fail-Build "winget could not install $DisplayName (exit code $LASTEXITCODE)." @(
            'Check the build log for the winget error.',
            'Make sure Windows Update and the Microsoft Store App Installer are working.',
            "Install $DisplayName manually, then re-run this script."
        )
    }
    Refresh-ProcessPath
}

function Upgrade-WingetPackage([string]$Id, [string]$DisplayName) {
    if ($NoInstall) {
        Fail-Build "$DisplayName is out of date and automatic upgrades were disabled." @(
            "Update $DisplayName manually, then run Build-Windows.cmd again without -NoInstall."
        )
    }

    $winget = Ensure-Winget
    Write-Step "Updating $DisplayName"
    & $winget upgrade --id $Id --exact --accept-package-agreements --accept-source-agreements --silent --disable-interactivity
    if ($LASTEXITCODE -ne 0) {
        Fail-Build "winget could not update $DisplayName (exit code $LASTEXITCODE)." @(
            'Check the build log for the winget error.',
            "Update $DisplayName manually, then run Build-Windows.cmd again."
        )
    }
    Refresh-ProcessPath
}

function Get-VsWhere {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) { return $vswhere }
    return $null
}


function ConvertTo-NumericVersion([string]$VersionText, [string]$ToolName = 'tool') {
    if ([string]::IsNullOrWhiteSpace($VersionText)) {
        Fail-Build "Could not determine the $ToolName version." @(
            "Run the selected $ToolName executable manually with --version and include the output when reporting this problem."
        )
    }

    # Some native Windows tool builds append a vendor suffix, for example:
    #   cmake version 3.31.6-msvc6
    # System.Version rejects that suffix, so extract only the leading numeric
    # version (up to four components) before parsing it.
    $match = [regex]::Match($VersionText, '(?<!\d)(\d+(?:\.\d+){1,3})(?!\d)')
    if (-not $match.Success) {
        Fail-Build "Could not parse the $ToolName version from '$VersionText'." @(
            "The build script expected a numeric version such as 3.31.6, optionally followed by a vendor suffix.",
            "Include the full build log when reporting this problem."
        )
    }

    try {
        return [System.Version]::Parse($match.Groups[1].Value)
    }
    catch {
        Fail-Build "Could not parse the $ToolName version from '$VersionText'." @(
            "Detected numeric text: '$($match.Groups[1].Value)'.",
            "Include the full build log when reporting this problem."
        )
    }
}

function Get-CMakeGeneratorNames([string]$CMakePath) {
    try {
        $rawCapabilities = (& $CMakePath -E capabilities 2>$null | Out-String)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($rawCapabilities)) {
            return @()
        }
        $capabilities = $rawCapabilities | ConvertFrom-Json
        return @($capabilities.generators | ForEach-Object { $_.name })
    }
    catch {
        return @()
    }
}

function Get-CMakeCandidates {
    $candidates = @()

    # Prefer native Kitware CMake before anything found on PATH. Git Bash/MSYS can
    # expose its own cmake.exe on PATH, but that build does not provide Visual
    # Studio generators and cannot drive an MSVC build.
    $knownPaths = @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin\cmake.exe')
    )
    foreach ($knownPath in $knownPaths) {
        if (-not [string]::IsNullOrWhiteSpace($knownPath)) {
            $candidates += $knownPath
        }
    }

    $vswhere = Get-VsWhere
    if ($vswhere) {
        $visualStudioCMake = @(& $vswhere -all -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' 2>$null)
        foreach ($candidate in $visualStudioCMake) {
            if (-not [string]::IsNullOrWhiteSpace($candidate)) {
                $candidates += $candidate.Trim()
            }
        }
    }

    $pathCommand = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand -and -not [string]::IsNullOrWhiteSpace($pathCommand.Source)) {
        $candidates += $pathCommand.Source
    }

    return @($candidates)
}

function Find-CMakeForGenerator([string]$Generator) {
    $seen = @{}
    foreach ($candidate in @(Get-CMakeCandidates)) {
        if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path $candidate)) {
            continue
        }

        try {
            $fullPath = [System.IO.Path]::GetFullPath($candidate)
        }
        catch {
            continue
        }

        $key = $fullPath.ToLowerInvariant()
        if ($seen.ContainsKey($key)) {
            continue
        }
        $seen[$key] = $true

        $generators = @(Get-CMakeGeneratorNames $fullPath)
        if ($generators -contains $Generator) {
            return $fullPath
        }

        $versionText = 'unknown version'
        try {
            $versionLine = (& $fullPath --version 2>$null | Select-Object -First 1)
            if (-not [string]::IsNullOrWhiteSpace($versionLine)) {
                $versionText = $versionLine.Trim()
            }
        }
        catch {}

        Write-Warn "Ignoring CMake at '$fullPath' ($versionText): it does not provide the '$Generator' generator."
    }
    return $null
}

function Find-VisualStudio {
    $vswhere = Get-VsWhere
    if (-not $vswhere) { return $null }
    $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) { return $null }
    return $installation.Trim()
}

function Find-AnyVisualStudio {
    $vswhere = Get-VsWhere
    if (-not $vswhere) { return $null }
    $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) { return $null }
    return $installation.Trim()
}

function Add-VisualStudioCppWorkload([string]$InstallationPath) {
    if ($NoInstall) {
        Fail-Build 'Visual Studio is installed, but its Desktop C++ workload is missing and automatic installation was disabled.' @(
            'Open Visual Studio Installer and add "Desktop development with C++".',
            'Include MSVC v143 and a Windows 10 or Windows 11 SDK.',
            'Then run Build-Windows.cmd again.'
        )
    }

    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    if (-not (Test-Path $installer)) {
        $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vs_installer.exe'
    }
    if (-not (Test-Path $installer)) {
        Fail-Build 'Visual Studio Installer could not be found to add the Desktop C++ workload.' @(
            'Open Visual Studio Installer manually.',
            'Add "Desktop development with C++", MSVC v143 and a Windows SDK.',
            'Then run Build-Windows.cmd again.'
        )
    }

    Write-Step 'Adding the Visual Studio Desktop C++ workload'
    # setup.exe/vs_installer.exe is the installed maintenance client, not the bootstrapper.
    # PowerShell's invocation operator already waits for it, and Microsoft documents --wait as
    # bootstrapper-only, so do not pass --wait here.
    & $installer modify --installPath $InstallationPath --passive --norestart `
        --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended
    if ($LASTEXITCODE -eq 3010) {
        Fail-Build 'The Visual Studio C++ workload was installed, but Windows must be restarted before the build can continue.' @(
            'Restart Windows, then run Build-Windows.cmd again.'
        )
    }
    if ($LASTEXITCODE -ne 0) {
        Fail-Build "Visual Studio Installer could not add the Desktop C++ workload (exit code $LASTEXITCODE)." @(
            'Open Visual Studio Installer manually and modify the detected installation.',
            'Add "Desktop development with C++", MSVC v143 and a Windows SDK.',
            'Restart Windows if the installer requests it, then retry.'
        )
    }
}

try {
    Write-Host 'DKR Port native launcher build' -ForegroundColor White
    Write-Host "Project: $ProjectRoot"
    Write-Host "Configuration: $Configuration"
    Write-Host "Log: $LogPath"

    if (-not [Environment]::Is64BitOperatingSystem) {
        Fail-Build 'A 64-bit version of Windows is required.'
    }

    Write-Step 'Checking required build tools'
    $git = Find-Executable 'git.exe' @(
        "$env:ProgramFiles\Git\cmd\git.exe",
        "${env:ProgramFiles(x86)}\Git\cmd\git.exe"
    )
    if (-not $git) {
        Install-WingetPackage 'Git.Git' 'Git for Windows'
        $git = Find-Executable 'git.exe' @("$env:ProgramFiles\Git\cmd\git.exe")
    }
    if (-not $git) {
        Fail-Build 'Git was installed but could not be located.' @('Restart Windows and run Build-Windows.cmd again.')
    }
    Write-Ok "Git: $(& $git --version)"

    # Regression check for Visual Studio's vendor-suffixed CMake version text.
    $versionParserCheck = ConvertTo-NumericVersion 'cmake version 3.31.6-msvc6' 'CMake version parser self-test'
    if ($versionParserCheck -ne [version]'3.31.6') {
        Fail-Build "The internal CMake version parser self-test failed. Parsed '$versionParserCheck' instead of '3.31.6'."
    }

    $requiredCMakeGenerator = 'Visual Studio 17 2022'
    $cmake = Find-CMakeForGenerator $requiredCMakeGenerator
    if (-not $cmake) {
        if ($NoInstall) {
            Fail-Build "A native Windows CMake supporting '$requiredCMakeGenerator' could not be found." @(
                'The cmake.exe currently on PATH may be the Git Bash/MSYS build, which only provides Unix and Ninja generators.',
                'Install the official Kitware CMake for Windows, then run Build-Windows.cmd again.',
                'Alternatively remove -NoInstall so this script can install Kitware CMake with winget.'
            )
        }
        Write-Warn "No suitable native CMake was found. A PATH copy without Visual Studio generators will not be used."
        Install-WingetPackage 'Kitware.CMake' 'Kitware CMake for Windows'
        $cmake = Find-CMakeForGenerator $requiredCMakeGenerator
    }
    if (-not $cmake) {
        Fail-Build "Kitware CMake was installed, but no CMake executable supporting '$requiredCMakeGenerator' could be located." @(
            'Restart Windows so newly installed paths are available, then run Build-Windows.cmd again.',
            'Expected location: C:\Program Files\CMake\bin\cmake.exe',
            'Do not use the cmake.exe supplied inside Git Bash, MSYS2 or Cygwin for this build.'
        )
    }

    $cmakeVersion = (& $cmake --version | Select-Object -First 1)
    $cmakeVersionNumber = ConvertTo-NumericVersion $cmakeVersion 'CMake'
    if ($cmakeVersionNumber -lt [version]'3.24.0') {
        if ($NoInstall) {
            Fail-Build "CMake $cmakeVersionNumber is too old; CMake 3.24 or newer is required." @(
                'Update the official Kitware CMake installation, then run Build-Windows.cmd again.'
            )
        }
        Upgrade-WingetPackage 'Kitware.CMake' 'Kitware CMake for Windows'
        $cmake = Find-CMakeForGenerator $requiredCMakeGenerator
        if (-not $cmake) {
            Fail-Build 'CMake was updated, but a compatible native executable still could not be found.' @(
                'Restart Windows, then run Build-Windows.cmd again.'
            )
        }
        $cmakeVersion = (& $cmake --version | Select-Object -First 1)
        $cmakeVersionNumber = ConvertTo-NumericVersion $cmakeVersion 'CMake'
        if ($cmakeVersionNumber -lt [version]'3.24.0') {
            Fail-Build "CMake $cmakeVersionNumber is still too old after installation." @(
                'Restart Windows so the new installation takes precedence, then run Build-Windows.cmd again.'
            )
        }
    }

    Write-Ok "CMake: $cmakeVersion"
    Write-Ok "CMake executable: $cmake"
    Write-Ok "CMake generator: $requiredCMakeGenerator"

    $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    if (-not (Test-Path $ctest)) {
        Fail-Build "ctest.exe could not be found beside the selected CMake executable '$cmake'." @(
            'Repair or reinstall the official Kitware CMake package, then run Build-Windows.cmd again.'
        )
    }

    $vsInstallation = Find-VisualStudio
    if (-not $vsInstallation) {
        $existingVisualStudio = Find-AnyVisualStudio
        if ($existingVisualStudio) {
            Write-Warn "Visual Studio was found at '$existingVisualStudio', but its C++ tools are missing."
            Add-VisualStudioCppWorkload $existingVisualStudio
        } else {
            $vsOverride = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
            Install-WingetPackage 'Microsoft.VisualStudio.2022.BuildTools' 'Visual Studio 2022 C++ Build Tools' $vsOverride
        }
        $vsInstallation = Find-VisualStudio
    }
    if (-not $vsInstallation) {
        Fail-Build 'Visual Studio 2022 C++ Build Tools could not be detected.' @(
            'Open Visual Studio Installer.',
            'Modify Visual Studio 2022 Build Tools.',
            'Install "Desktop development with C++" including MSVC v143 and a Windows SDK.',
            'Run Build-Windows.cmd again.'
        )
    }
    Write-Ok "Visual Studio C++ tools: $vsInstallation"

    Write-Step "Preparing pinned vcpkg registry ($VcpkgTag)"
    if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $VcpkgRoot) | Out-Null
        & $git clone --depth 1 --branch $VcpkgTag https://github.com/microsoft/vcpkg.git $VcpkgRoot
        if ($LASTEXITCODE -ne 0) {
            Fail-Build 'Could not download vcpkg.' @(
                'Check that GitHub is reachable from this computer.',
                'Temporarily disable any network filter blocking github.com.',
                'Delete the .deps\vcpkg folder before retrying.'
            )
        }
    } else {
        $currentTag = (& $git -C $VcpkgRoot describe --tags --exact-match 2>$null)
        if ($currentTag -ne $VcpkgTag) {
            Write-Warn "The existing vcpkg checkout is '$currentTag'; resetting it to '$VcpkgTag'."
            & $git -C $VcpkgRoot fetch --depth 1 origin "refs/tags/$VcpkgTag:refs/tags/$VcpkgTag"
            if ($LASTEXITCODE -ne 0) { Fail-Build 'Could not fetch the pinned vcpkg tag.' }
            & $git -C $VcpkgRoot checkout --force $VcpkgTag
            if ($LASTEXITCODE -ne 0) { Fail-Build 'Could not switch vcpkg to the pinned tag.' }
        }
    }

    $vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path $vcpkgExe)) {
        & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $vcpkgExe)) {
            Fail-Build 'vcpkg failed to bootstrap.' @(
                'Check the log for compiler or network errors.',
                'Delete .deps\vcpkg and run Build-Windows.cmd again.'
            )
        }
    }
    Write-Ok "vcpkg: $(& $vcpkgExe version | Select-Object -First 1)"

    if ($Clean -and (Test-Path $BuildDirectory)) {
        Write-Step 'Removing the previous build directory'
        Remove-Item -Recurse -Force $BuildDirectory
    }

    $cmakeCache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path $cmakeCache) {
        $cachedGeneratorLine = Get-Content $cmakeCache | Where-Object { $_ -like 'CMAKE_GENERATOR:INTERNAL=*' } | Select-Object -First 1
        if ($cachedGeneratorLine) {
            $cachedGenerator = ($cachedGeneratorLine -split '=', 2)[1]
            if ($cachedGenerator -ne $requiredCMakeGenerator) {
                Write-Warn "The existing build directory uses '$cachedGenerator'; '$requiredCMakeGenerator' is required. Removing the stale build directory."
                Remove-Item -Recurse -Force $BuildDirectory
            }
        }
    }

    Write-Step 'Configuring the native launcher and installing C++ libraries'
    $toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    & $cmake --no-warn-unused-cli -S $ProjectRoot -B $BuildDirectory `
        -G $requiredCMakeGenerator -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
        '-DDKRPORT_BUILD_NATIVE_UI=ON' `
        '-DDKRPORT_BUILD_TESTS=ON' `
        '-DDKRPORT_ENABLE_WARNINGS_AS_ERRORS=ON'
    if ($LASTEXITCODE -ne 0) {
        Fail-Build 'CMake configuration failed.' @(
            'The most useful error is usually 10-30 lines above this message.',
            'The selected native CMake path and generator are printed near the top of the log.',
            'Retry with Build-Windows.cmd -Clean if a previous generator cache exists.',
            'Check that the Visual Studio C++ workload and Windows SDK are installed.'
        )
    }

    # main.cpp directly includes SDL3/SDL_main.h. Verify that the generated
    # executable project inherited the vcpkg include directory before invoking
    # MSBuild, so a CMake usage-requirement regression produces a clear error.
    $sdlMainHeader = Join-Path $BuildDirectory 'vcpkg_installed\x64-windows-static-md\include\SDL3\SDL_main.h'
    if (-not (Test-Path $sdlMainHeader)) {
        Fail-Build "SDL3 was installed, but SDL_main.h was not found at '$sdlMainHeader'." @(
            'Delete build\windows-x64 and rerun Build-Windows.cmd -Clean.',
            'If the header is still missing, delete .deps\vcpkg and retry so SDL3 is reinstalled.'
        )
    }

    $mainProjectFile = Join-Path $BuildDirectory 'DKRPort.vcxproj'
    if (-not (Test-Path $mainProjectFile)) {
        Fail-Build "CMake did not generate the expected Visual Studio project '$mainProjectFile'."
    }
    $mainProjectText = Get-Content -Raw $mainProjectFile
    $sdlIncludeFragment = 'vcpkg_installed\x64-windows-static-md\include'
    if ($mainProjectText -notmatch [regex]::Escape($sdlIncludeFragment)) {
        Fail-Build 'The generated DKRPort project did not inherit the SDL3 include directory.' @(
            'This indicates a CMake target dependency regression, not a missing SDL installation.',
            'Confirm that CMakeLists.txt links DKRPort directly to SDL3::SDL3.',
            'Use the latest project package and rerun Build-Windows.cmd without deleting .deps.'
        )
    }
    Write-Ok 'Generated executable project inherits the SDL3 include directory.'

    Write-Step "Building DKR Port ($Configuration)"
    $MsBuildTextLog = Join-Path $LogDirectory "msbuild-$Timestamp.log"
    $MsBuildBinaryLog = Join-Path $LogDirectory "msbuild-$Timestamp.binlog"
    $MsBuildFileLogger = "/flp:LogFile=$MsBuildTextLog;Verbosity=diagnostic;Encoding=UTF-8"
    $MsBuildBinaryLogger = "/bl:$MsBuildBinaryLog"

    Write-Host "MSBuild text log: $MsBuildTextLog"
    Write-Host "MSBuild binary log: $MsBuildBinaryLog"

    # Windows PowerShell 5.1 Start-Transcript does not reliably capture output emitted
    # by MSBuild through CMake. Ask MSBuild to write its own diagnostic text and binary
    # logs, then print the useful error lines back into the main transcript on failure.
    & $cmake --build $BuildDirectory --config $Configuration --parallel -- `
        /nologo /verbosity:minimal /fileLogger $MsBuildFileLogger $MsBuildBinaryLogger
    $BuildExitCode = $LASTEXITCODE

    if ($BuildExitCode -ne 0) {
        Write-Host "`n==> Compiler/linker error summary" -ForegroundColor Red
        if (Test-Path $MsBuildTextLog) {
            $ErrorPatterns = @(
                'fatal error C\d+',
                'error C\d+',
                'error LNK\d+',
                'fatal error LNK\d+',
                'error MSB\d+',
                ': error :'
            )
            $ErrorLines = @(Select-String -Path $MsBuildTextLog -Pattern $ErrorPatterns |
                ForEach-Object { $_.Line.Trim() } |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                Select-Object -Unique -First 40)

            if ($ErrorLines.Count -gt 0) {
                foreach ($Line in $ErrorLines) { Write-Host $Line -ForegroundColor Red }
            } else {
                Write-Warn 'MSBuild returned a failure code but no standard compiler-error line was found.'
                Write-Host 'Last 100 lines of the MSBuild diagnostic log:' -ForegroundColor Yellow
                Get-Content $MsBuildTextLog -Tail 100 | ForEach-Object { Write-Host $_ }
            }
        } else {
            Write-Warn 'MSBuild did not create its requested diagnostic text log.'
        }

        Fail-Build "Compilation failed with exit code $BuildExitCode." @(
            "The compiler error summary is printed immediately above this message.",
            "Full MSBuild text log: $MsBuildTextLog",
            "Visual Studio binary log: $MsBuildBinaryLog",
            'The .binlog can be opened with Microsoft Build Logs Viewer if deeper inspection is needed.',
            'Do not delete build\windows-x64; it allows the next retry to reuse installed dependencies.'
        )
    }

    if (-not $SkipTests) {
        Write-Step 'Running automated tests'
        & $ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            Fail-Build 'The project compiled, but one or more automated tests failed.' @(
                'Read the failed test output above.',
                'The executable may exist, but it should not be trusted until the failing test is fixed.'
            )
        }
    }

    $builtExe = Join-Path $BuildDirectory "bin\$Configuration\DKRPort.exe"
    if (-not (Test-Path $builtExe)) {
        Fail-Build "The build reported success, but DKRPort.exe was not found at '$builtExe'."
    }

    Write-Step 'Creating a clean runnable folder'
    if (Test-Path $DistributionDirectory) { Remove-Item -Recurse -Force $DistributionDirectory }
    New-Item -ItemType Directory -Force -Path $DistributionDirectory | Out-Null
    Copy-Item $builtExe (Join-Path $DistributionDirectory 'DKRPort.exe')
    Copy-Item (Join-Path $ProjectRoot 'portable.txt') (Join-Path $DistributionDirectory 'portable.txt')
    Copy-Item -Recurse (Join-Path $ProjectRoot 'assets') (Join-Path $DistributionDirectory 'assets')
    @"
DKR Port - Native Launcher Development Build

Run DKRPort.exe. The application opens its own native window; it does not start a browser or web server.
The launcher validates a legally obtained Diddy Kong Racing US 1.0 ROM, supports persistent remappable controls and provides a ROM-backed host/input screen.
The source repository also includes Build-DKR-Runtime.cmd, which prepares the matching DKR ELF and first N64Recomp/N64ModernRuntime compilation boundary.
The DKR title screen, original graphics and original audio are not running yet.
"@ | Set-Content -Encoding UTF8 (Join-Path $DistributionDirectory 'README.txt')

    Write-Ok 'Build and tests completed successfully.'
    Write-Host "`nRunnable application:" -ForegroundColor White
    Write-Host "  $DistributionDirectory\DKRPort.exe" -ForegroundColor Green
    Write-Host "`nBuild log:" -ForegroundColor White
    Write-Host "  $LogPath"

    if ($KeepBuildTree) {
        Write-Host "`nBuild caches were retained under .deps and build for diagnostics and faster rebuilds."
    } else {
        Write-Host "`nBuild caches remain under .deps and build so later builds are faster. Delete them manually for a completely fresh build."
    }

    Stop-Transcript | Out-Null
    exit 0
}
catch {
    $message = $_.Exception.Message
    Write-Host "`nThe build stopped: $message" -ForegroundColor Red
    Write-Host "Full log: $LogPath" -ForegroundColor Yellow
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

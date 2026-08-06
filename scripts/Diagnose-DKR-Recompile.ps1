[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LogDirectory = Join-Path $ProjectRoot 'build-logs'
$RuntimeRoot = Join-Path $ProjectRoot 'runtime-recomp'
$TomlPath = Join-Path $RuntimeRoot 'dkr.us.v77.generated.toml'
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$StdoutLog = Join-Path $LogDirectory "n64recomp-$Timestamp.stdout.log"
$StderrLog = Join-Path $LogDirectory "n64recomp-$Timestamp.stderr.log"

New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null

function Fail([string]$Message) {
    throw $Message
}

function Convert-ToWslPath([string]$WindowsPath) {
    $resolvedWindowsPath = (Resolve-Path -LiteralPath $WindowsPath).Path
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& wsl.exe --exec wslpath -a -u $resolvedWindowsPath 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $translatedPath = ($output | Out-String).Trim()
    if ($exitCode -ne 0 -or [string]::IsNullOrWhiteSpace($translatedPath)) {
        Fail "WSL could not translate '$resolvedWindowsPath': $translatedPath"
    }
    return $translatedPath
}

function Refresh-DkrEntrypoint([string]$ElfPath, [string]$ConfigPath) {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        Fail 'WSL is required to inspect the matching DKR ELF.'
    }
    if (-not (Test-Path -LiteralPath $ElfPath -PathType Leaf)) {
        Fail "The matching DKR ELF is missing: $ElfPath"
    }

    $wslElf = Convert-ToWslPath $ElfPath
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $symbolOutput = @(& wsl.exe --exec mips-linux-gnu-nm -n --defined-only $wslElf 2>&1)
        $symbolExitCode = $LASTEXITCODE
        $sectionOutput = @(& wsl.exe --exec mips-linux-gnu-readelf -S -W $wslElf 2>&1)
        $sectionExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($symbolExitCode -ne 0) {
        Fail "Could not inspect DKR ELF symbols: $(($symbolOutput | Out-String).Trim())"
    }
    if ($sectionExitCode -ne 0) {
        Fail "Could not inspect DKR ELF sections: $(($sectionOutput | Out-String).Trim())"
    }

    $mainprocAddress = $null
    foreach ($rawLine in $symbolOutput) {
        $line = [string]$rawLine
        if ($line -match '^\s*([0-9A-Fa-f]+)\s+[A-Za-z]\s+mainproc\s*$') {
            $mainprocAddress = [Convert]::ToUInt64($Matches[1], 16)
            break
        }
    }
    if ($null -eq $mainprocAddress) {
        Fail 'The matching DKR ELF does not expose the expected mainproc symbol.'
    }

    $entrypointText = '0x{0:X8}' -f ([uint32]$mainprocAddress)
    $useMdebugText = (($sectionOutput | Out-String) -match '(?m)\s\.mdebug\s').ToString().ToLowerInvariant()
    $configText = [System.IO.File]::ReadAllText($ConfigPath)

    $policyPath = Join-Path $ProjectRoot 'runtime-recomp\dkr.us.v77.recomp-policy.json'
    if (-not (Test-Path -LiteralPath $policyPath -PathType Leaf)) {
        Fail "DKR recompilation policy is missing: $policyPath"
    }
    $policy = Get-Content -LiteralPath $policyPath -Raw | ConvertFrom-Json
    if ([int]$policy.schemaVersion -ne 1) { Fail 'Unsupported DKR recompilation policy schema.' }
    $stubToml = (@($policy.stubs | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $renamedToml = (@($policy.renamed | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $ignoredToml = (@($policy.ignored | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $functionSizesToml = (@($policy.functionSizes | ForEach-Object {
        "{ name = `"$([string]$_.name)`", size = $([string]$_.size) }"
    }) -join ', ')
    $instructionPatchToml = (@($policy.instructionPatches | ForEach-Object {
        "[[patches.instruction]]`nfunc = `"$([string]$_.function)`"`nvram = $([string]$_.vram)`nvalue = $([string]$_.value)"
    }) -join "`n`n")
    $manualFunctionsToml = (@($policy.manualFunctions | ForEach-Object {
        "{ name = `"$([string]$_.name)`", section = `"$([string]$_.section)`", vram = $([string]$_.vram), size = $([string]$_.size) }"
    }) -join ', ')
    $functionHookToml = (@($policy.functionHooks | ForEach-Object {
        $hookText = ([string]$_.text).Replace('\', '\\').Replace('"', '\"')
        "[[patches.hook]]`nfunc = `"$([string]$_.function)`"`nbefore_vram = $([string]$_.beforeVram)`ntext = `"$hookText`""
    }) -join "`n`n")

    if ($configText -notmatch '(?m)^entrypoint\s*=') {
        Fail "The generated config does not contain an entrypoint key: $ConfigPath"
    }
    $configText = [regex]::Replace($configText, '(?m)^entrypoint\s*=.*$', "entrypoint = $entrypointText")
    if ($configText -match '(?m)^use_mdebug\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^use_mdebug\s*=.*$', "use_mdebug = $useMdebugText")
    } else {
        $configText = [regex]::Replace($configText, '(?m)^(entrypoint\s*=.*)$', "`$1`nuse_mdebug = $useMdebugText")
    }
    if ($configText -match '(?m)^manual_funcs\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^manual_funcs\s*=.*$', "manual_funcs = [$manualFunctionsToml]")
    } else {
        $configText = [regex]::Replace($configText, '(?m)^(output_func_path\s*=.*)$', "`$1`nmanual_funcs = [$manualFunctionsToml]")
    }
    if ($configText -match '(?m)^function_sizes\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^function_sizes\s*=.*$', "function_sizes = [$functionSizesToml]")
    } else {
        $configText = [regex]::Replace($configText, '(?m)^(manual_funcs\s*=.*)$', "`$1`nfunction_sizes = [$functionSizesToml]")
    }
    if ($configText -match '(?m)^stubs\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^stubs\s*=.*$', "stubs = [$stubToml]")
    } else {
        Fail 'The generated config does not contain a stubs array.'
    }
    if ($configText -match '(?m)^ignored\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^ignored\s*=.*$', "ignored = [$ignoredToml]")
    } else {
        Fail 'The generated config does not contain an ignored array.'
    }
    if ($configText -match '(?m)^renamed\s*=') {
        $configText = [regex]::Replace($configText, '(?m)^renamed\s*=.*$', "renamed = [$renamedToml]")
    } else {
        $configText = [regex]::Replace($configText, '(?m)^(stubs\s*=.*)$', "`$1`nrenamed = [$renamedToml]")
    }
    $instructionPatchBlock = "# BEGIN DKR_INSTRUCTION_PATCHES`n$instructionPatchToml`n# END DKR_INSTRUCTION_PATCHES"
    if ($configText -match '(?s)# BEGIN DKR_INSTRUCTION_PATCHES.*?# END DKR_INSTRUCTION_PATCHES') {
        $configText = [regex]::Replace(
            $configText,
            '(?s)# BEGIN DKR_INSTRUCTION_PATCHES.*?# END DKR_INSTRUCTION_PATCHES',
            $instructionPatchBlock)
    } else {
        $configText = $configText.TrimEnd() + "`n`n$instructionPatchBlock`n"
    }
    $functionHookBlock = "# BEGIN DKR_FUNCTION_HOOKS`n$functionHookToml`n# END DKR_FUNCTION_HOOKS"
    if ($configText -match '(?s)# BEGIN DKR_FUNCTION_HOOKS.*?# END DKR_FUNCTION_HOOKS') {
        $configText = [regex]::Replace(
            $configText,
            '(?s)# BEGIN DKR_FUNCTION_HOOKS.*?# END DKR_FUNCTION_HOOKS',
            $functionHookBlock)
    } else {
        $configText = $configText.TrimEnd() + "`n`n$functionHookBlock`n"
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($ConfigPath, $configText, $utf8NoBom)
    Write-Host "[OK] Static recompilation entry function: mainproc at $entrypointText" -ForegroundColor Green
    Write-Host "[OK] use_mdebug: $useMdebugText" -ForegroundColor Green
}

try {
    Write-Host 'DKR-R - regenerate the prepared N64Recomp boundary'
    Write-Host "Project: $ProjectRoot"
    Write-Host ''

    if (-not (Test-Path -LiteralPath $TomlPath -PathType Leaf)) {
        Fail "The generated N64Recomp configuration is missing: $TomlPath Run Build-DKR-Runtime.cmd first."
    }

    $Recompiler = Get-ChildItem `
        -LiteralPath (Join-Path $ProjectRoot 'build\runtime-tools\n64recomp') `
        -Filter 'N64Recomp.exe' `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if (-not $Recompiler) {
        Fail 'N64Recomp.exe is missing. Run Build-DKR-Runtime.cmd first.'
    }

    $ElfPath = Join-Path $ProjectRoot 'extern\dkr-decomp\build\dkr.us.v77.elf'
    Refresh-DkrEntrypoint $ElfPath $TomlPath

    Write-Host "[OK] N64Recomp: $($Recompiler.FullName)" -ForegroundColor Green
    Write-Host "[OK] Configuration: $TomlPath" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Running N64Recomp. This command reuses the already-built DKR ELF and does not rerun WSL setup.' -ForegroundColor Cyan

    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $Recompiler.FullName
    $StartInfo.Arguments = '"' + $TomlPath + '"'
    $StartInfo.WorkingDirectory = $RuntimeRoot
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true

    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) {
        Fail 'N64Recomp could not be started.'
    }

    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    $Process.WaitForExit()
    $ExitCode = $Process.ExitCode
    $Stdout = $StdoutTask.Result
    $Stderr = $StderrTask.Result
    $Process.Dispose()

    [System.IO.File]::WriteAllText($StdoutLog, $Stdout, $Utf8NoBom)
    [System.IO.File]::WriteAllText($StderrLog, $Stderr, $Utf8NoBom)

    if (-not [string]::IsNullOrWhiteSpace($Stdout)) {
        Write-Host ''
        Write-Host '==> N64Recomp standard output'
        Write-Host $Stdout.TrimEnd()
    }
    if (-not [string]::IsNullOrWhiteSpace($Stderr)) {
        Write-Host ''
        Write-Host '==> N64Recomp diagnostic output' -ForegroundColor Yellow
        Write-Host $Stderr.TrimEnd() -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host "stdout log: $StdoutLog"
    Write-Host "stderr log: $StderrLog"

    if ($ExitCode -ne 0) {
        Write-Host ''
        Write-Host "[FAILED] N64Recomp returned exit code $ExitCode." -ForegroundColor Red
        exit $ExitCode
    }

    Write-Host ''
    Write-Host '[OK] N64Recomp completed successfully.' -ForegroundColor Green
    exit 0
} catch {
    Write-Host ''
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "stdout log: $StdoutLog"
    Write-Host "stderr log: $StderrLog"
    exit 1
}

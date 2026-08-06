[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LogDirectory = Join-Path $ProjectRoot 'build-logs'
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogPath = Join-Path $LogDirectory "prepare-game-source-$Timestamp.log"
Start-Transcript -Path $LogPath -Force | Out-Null

function Fail([string]$Message) {
    throw $Message
}

try {
    Write-Host 'DKR-R game-source preparation'
    Write-Host "Project: $ProjectRoot"
    Write-Host "Log: $LogPath"
    Write-Host ''

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }
    if (-not $git) {
        Fail 'Git was not found. Run Build-Windows.cmd once, or install Git for Windows and retry.'
    }

    $lockPath = Join-Path $ProjectRoot 'dependencies.lock.json'
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        Fail "Dependency lock file was not found: $lockPath"
    }
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    $entry = $lock.dependencies | Where-Object { $_.name -eq 'dkr-decomp' } | Select-Object -First 1
    if (-not $entry) { Fail 'The dkr-decomp entry is missing from dependencies.lock.json.' }

    $destination = Join-Path $ProjectRoot ([string]$entry.destination)
    $repository = [string]$entry.repository
    $commit = [string]$entry.commit
    Write-Host "Pinned commit: $commit"
    Write-Host "Destination: $destination"

    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath (Join-Path $destination '.git'))) {
            Fail "$destination exists but is not a Git checkout. Move or remove it and retry."
        }
        $dirty = & $git.Source -C $destination status --porcelain
        if ($LASTEXITCODE -ne 0) { Fail 'Git could not inspect the existing DKR checkout.' }
        if ($dirty -and -not $Force) {
            Fail 'The existing DKR checkout contains local changes. Commit/stash them, or rerun Prepare-Game-Source.cmd -Force to discard them.'
        }
        if ($dirty -and $Force) {
            & $git.Source -C $destination reset --hard
            if ($LASTEXITCODE -ne 0) { Fail 'Git could not reset the DKR checkout.' }
            & $git.Source -C $destination clean -fd
            if ($LASTEXITCODE -ne 0) { Fail 'Git could not clean the DKR checkout.' }
        }
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        & $git.Source clone --filter=blob:none --no-checkout $repository $destination
        if ($LASTEXITCODE -ne 0) { Fail 'Git could not clone the DKR decomp repository. Check the internet connection and GitHub access.' }
    }

    & $git.Source -C $destination fetch --no-tags origin $commit
    if ($LASTEXITCODE -ne 0) { Fail "Git could not fetch pinned DKR commit $commit." }
    & $git.Source -C $destination checkout --detach $commit
    if ($LASTEXITCODE -ne 0) { Fail "Git could not check out pinned DKR commit $commit." }
    & $git.Source -C $destination submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { Fail 'Git could not initialise the DKR source submodules.' }

    $resolved = (& $git.Source -C $destination rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $resolved -ne $commit) {
        Fail "Pinned revision verification failed. Expected $commit but resolved $resolved."
    }

    foreach ($required in @('src', 'include', 'libultra', 'Makefile')) {
        if (-not (Test-Path -LiteralPath (Join-Path $destination $required))) {
            Fail "The checkout is incomplete; required path is missing: $required"
        }
    }

    $recordPath = Join-Path $ProjectRoot 'extern\.resolved-dkr-source.json'
    @{
        schemaVersion = 1
        repository = $repository
        commit = $resolved
        destination = [string]$entry.destination
        preparedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8

    Write-Host ''
    Write-Host '[OK] The pinned DKR decomp source is ready.' -ForegroundColor Green
    Write-Host "Source: $destination"
    Write-Host "Resolution record: $recordPath"
    Write-Host 'This command does not copy or download a game ROM or extracted assets.'
    Stop-Transcript | Out-Null
    exit 0
} catch {
    Write-Host ''
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Full log: $LogPath"
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

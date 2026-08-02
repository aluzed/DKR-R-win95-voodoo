[CmdletBinding()]
param(
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ManifestPath = Join-Path $ProjectRoot 'patches\manifest.json'
$ResolvedPath = Join-Path $ProjectRoot 'runtime-recomp\applied-patches.json'

function Fail([string]$Message) {
    throw $Message
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Fail "Patch manifest is missing: $ManifestPath"
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.schemaVersion -ne 1) {
    Fail "Unsupported patch manifest schema: $($manifest.schemaVersion)"
}

# A later patch is allowed to overlap and supersede hunks from an earlier
# patch. In that case `git apply --reverse --check` can no longer prove that
# the earlier patch was applied. Retain the checksummed applied-patch record as
# the second source of truth, but only when the earlier patch is neither
# reversible nor currently applicable. A clean checkout still reports the
# patch as applicable, so a stale record cannot cause patches to be skipped.
$recordedAppliedPatches = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$recordedWorktreeFingerprints = @{}
if (Test-Path -LiteralPath $ResolvedPath -PathType Leaf) {
    $previouslyResolved = Get-Content -LiteralPath $ResolvedPath -Raw | ConvertFrom-Json
    if ([int]$previouslyResolved.schemaVersion -ne 1) {
        Fail "Unsupported applied-patch record schema: $($previouslyResolved.schemaVersion)"
    }
    foreach ($recordedDependency in $previouslyResolved.dependencies) {
        if ($recordedDependency.PSObject.Properties.Name -contains 'worktreeDiffFingerprint') {
            $dependencyKey = '{0}|{1}' -f @(
                [string]$recordedDependency.name,
                [string]$recordedDependency.commit
            )
            $recordedWorktreeFingerprints[$dependencyKey] =
                [string]$recordedDependency.worktreeDiffFingerprint
        }
        foreach ($recordedPatch in $recordedDependency.patches) {
            $recordKey = '{0}|{1}|{2}|{3}' -f @(
                [string]$recordedDependency.name,
                [string]$recordedDependency.commit,
                [string]$recordedPatch.path,
                [string]$recordedPatch.sha256
            )
            [void]$recordedAppliedPatches.Add($recordKey)
        }
    }
}

function Get-DependencyDiffFingerprint([string]$RepositoryPath) {
    $fingerprint = (& $git.Source -C $RepositoryPath diff --binary --full-index |
        & $git.Source hash-object --stdin).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($fingerprint)) {
        Fail "Could not fingerprint dependency worktree: $RepositoryPath"
    }
    return $fingerprint
}

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) { $git = Get-Command git -ErrorAction Stop }
$resolvedDependencies = New-Object System.Collections.Generic.List[object]

foreach ($dependency in $manifest.dependencies) {
    $repoPath = Join-Path $ProjectRoot ([string]$dependency.repositoryPath)
    if (-not (Test-Path -LiteralPath (Join-Path $repoPath '.git'))) {
        Fail "Dependency checkout is missing or is not a Git worktree: $repoPath"
    }

    $actualCommit = (& $git.Source -C $repoPath rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { Fail "Could not resolve dependency commit: $repoPath" }
    if ($actualCommit -ne [string]$dependency.expectedCommit) {
        Fail "Patch set for $($dependency.name) expects $($dependency.expectedCommit), but checkout is $actualCommit"
    }

    $dependencyKey = '{0}|{1}' -f @([string]$dependency.name, $actualCommit)
    $initialDiffFingerprint = Get-DependencyDiffFingerprint $repoPath
    $recordedStateTrusted =
        $recordedWorktreeFingerprints.ContainsKey($dependencyKey) -and
        $recordedWorktreeFingerprints[$dependencyKey] -eq $initialDiffFingerprint

    $resolvedPatches = New-Object System.Collections.Generic.List[object]
    $allowedChangedFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)

    # Patches later in the ordered set can legitimately overlap files changed
    # by an earlier patch. Pre-authorize only paths declared by a checksummed
    # manifest entry so an idempotent rerun does not mistake those overlaps for
    # unrelated dependency edits.
    foreach ($declaredPatchEntry in $dependency.patches) {
        $declaredPatchPath = Join-Path $ProjectRoot ([string]$declaredPatchEntry.path)
        if (-not (Test-Path -LiteralPath $declaredPatchPath -PathType Leaf)) {
            Fail "Patch file is missing: $declaredPatchPath"
        }
        $declaredNumstat = @(& $git.Source -C $repoPath apply --numstat $declaredPatchPath)
        if ($LASTEXITCODE -ne 0) {
            Fail "Could not inspect patch paths: $($declaredPatchEntry.path)"
        }
        foreach ($declaredLine in $declaredNumstat) {
            $declaredColumns = ([string]$declaredLine) -split "`t", 3
            if ($declaredColumns.Count -eq 3 -and $declaredColumns[2]) {
                [void]$allowedChangedFiles.Add([string]$declaredColumns[2])
            }
        }
    }

    foreach ($patchEntry in $dependency.patches) {
        $patchPath = Join-Path $ProjectRoot ([string]$patchEntry.path)
        if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
            Fail "Patch file is missing: $patchPath"
        }

        $actualHash = (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = ([string]$patchEntry.sha256).ToLowerInvariant()
        if ($expectedHash -eq 'pending') {
            Fail "Patch manifest contains a pending checksum for $($patchEntry.path)"
        }
        if ($actualHash -ne $expectedHash) {
            Fail "Patch checksum mismatch for $($patchEntry.path): expected $expectedHash, got $actualHash"
        }

        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $git.Source -C $repoPath apply --reverse --check $patchPath 2>$null
            $alreadyApplied = $LASTEXITCODE -eq 0
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }

        $appliedThroughOverlappingRecord = $false
        if (-not $alreadyApplied) {
            $previousErrorActionPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & $git.Source -C $repoPath apply --check $patchPath 2>$null
                $forwardApplicable = $LASTEXITCODE -eq 0
            } finally {
                $ErrorActionPreference = $previousErrorActionPreference
            }

            $recordKey = '{0}|{1}|{2}|{3}' -f @(
                [string]$dependency.name,
                $actualCommit,
                [string]$patchEntry.path,
                $actualHash
            )
            if ($recordedStateTrusted -and $recordedAppliedPatches.Contains($recordKey)) {
                $alreadyApplied = $true
                $appliedThroughOverlappingRecord = $true
            } elseif (-not $forwardApplicable -and $recordedAppliedPatches.Contains($recordKey)) {
                $alreadyApplied = $true
                $appliedThroughOverlappingRecord = $true
            }
        }

        $numstat = @(& $git.Source -C $repoPath apply --numstat $patchPath)
        if ($LASTEXITCODE -ne 0) { Fail "Could not inspect patch paths: $($patchEntry.path)" }
        $patchFiles = @($numstat | ForEach-Object {
            $columns = ([string]$_) -split "`t", 3
            if ($columns.Count -eq 3) { $columns[2] }
        } | Where-Object { $_ })

        if (-not $alreadyApplied) {
            $trackedChanges = @(& $git.Source -C $repoPath status --short --untracked-files=no --ignore-submodules=dirty)
            if ($LASTEXITCODE -ne 0) { Fail "Could not inspect dependency worktree: $repoPath" }
            $unexpectedChanges = @($trackedChanges | Where-Object {
                $changedPath = ([string]$_).Substring(3).Trim()
                -not $allowedChangedFiles.Contains($changedPath)
            })
            if ($unexpectedChanges.Count -ne 0) {
                Fail "Refusing to patch a dependency with unrelated tracked changes: $repoPath`n$($unexpectedChanges -join "`n")"
            }

            & $git.Source -C $repoPath apply --check $patchPath
            if ($LASTEXITCODE -ne 0) { Fail "Patch does not apply cleanly: $($patchEntry.path)" }
            if (-not $CheckOnly) {
                & $git.Source -C $repoPath apply $patchPath
                if ($LASTEXITCODE -ne 0) { Fail "Failed to apply patch: $($patchEntry.path)" }
            }
        }

        foreach ($patchFile in $patchFiles) { [void]$allowedChangedFiles.Add([string]$patchFile) }

        $state = if ($appliedThroughOverlappingRecord) { 'already-applied-overlapped' } elseif ($alreadyApplied) { 'already-applied' } elseif ($CheckOnly) { 'applicable' } else { 'applied' }
        Write-Host "[OK] $($dependency.name): $($patchEntry.path) ($state)" -ForegroundColor Green
        $resolvedPatches.Add([ordered]@{
            path = [string]$patchEntry.path
            sha256 = $actualHash
            state = $state
        })
    }

    $resolvedDependencies.Add([ordered]@{
        name = [string]$dependency.name
        commit = $actualCommit
        worktreeDiffFingerprint = if ($CheckOnly) {
            $initialDiffFingerprint
        } else {
            Get-DependencyDiffFingerprint $repoPath
        }
        patches = $resolvedPatches.ToArray()
    })
}

if (-not $CheckOnly) {
    $resolvedDirectory = Split-Path -Parent $ResolvedPath
    New-Item -ItemType Directory -Force -Path $resolvedDirectory | Out-Null
    $resolved = [ordered]@{
        schemaVersion = 1
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        dependencies = $resolvedDependencies.ToArray()
    }
    $json = $resolved | ConvertTo-Json -Depth 8
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($ResolvedPath, $json + "`n", $utf8NoBom)
    Write-Host "[OK] Applied patch record: $ResolvedPath" -ForegroundColor Green
}

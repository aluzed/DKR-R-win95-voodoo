param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateRange(25, 5000)]
    [int]$SampleIntervalMilliseconds = 100
)

$ErrorActionPreference = 'Stop'

$mappingName = 'RTSSSharedMemoryV2'
$signatureRtss = 0x52545353
$signatureDead = 0x0000DEAD

$mapping = $null
$view = $null
$writer = $null

try {
    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    }

    $writer = [System.IO.StreamWriter]::new($OutputPath, $false, [System.Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    $writer.WriteLine('utc_time,pid,frame_time_us,instant_fps,period_fps,period_frames,period_ms')

    while ($null -eq $mapping) {
        if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
            throw "Process $ProcessId exited before RivaTuner published its shared-memory entry."
        }

        try {
            $mapping = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
                $mappingName,
                [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read
            )
        }
        catch [System.IO.FileNotFoundException] {
            Start-Sleep -Milliseconds $SampleIntervalMilliseconds
        }
    }

    $view = $mapping.CreateViewAccessor(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)

    while (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
        $signature = $view.ReadUInt32(0)
        if (($signature -eq $signatureDead) -or ($signature -ne $signatureRtss)) {
            Start-Sleep -Milliseconds $SampleIntervalMilliseconds
            continue
        }

        $appEntrySize = $view.ReadUInt32(8)
        $appArrayOffset = $view.ReadUInt32(12)
        $appArraySize = $view.ReadUInt32(16)

        for ($index = 0; $index -lt $appArraySize; $index++) {
            $entryOffset = [long]$appArrayOffset + ([long]$index * [long]$appEntrySize)
            if ($view.ReadUInt32($entryOffset) -ne [uint32]$ProcessId) {
                continue
            }

            $time0 = $view.ReadUInt32($entryOffset + 268)
            $time1 = $view.ReadUInt32($entryOffset + 272)
            $frames = $view.ReadUInt32($entryOffset + 276)
            $frameTimeMicroseconds = $view.ReadUInt32($entryOffset + 280)
            $periodMilliseconds = [uint32]($time1 - $time0)

            $instantFps = if ($frameTimeMicroseconds -gt 0) {
                1000000.0 / [double]$frameTimeMicroseconds
            }
            else {
                0.0
            }

            $periodFps = if (($time0 -ne 0) -and ($periodMilliseconds -gt 0)) {
                1000.0 * [double]$frames / [double]$periodMilliseconds
            }
            else {
                0.0
            }

            $writer.WriteLine(
                '{0},{1},{2},{3:F3},{4:F3},{5},{6}',
                [DateTime]::UtcNow.ToString('O'),
                $ProcessId,
                $frameTimeMicroseconds,
                $instantFps,
                $periodFps,
                $frames,
                $periodMilliseconds
            )
            break
        }

        Start-Sleep -Milliseconds $SampleIntervalMilliseconds
    }
}
finally {
    if ($null -ne $view) {
        $view.Dispose()
    }
    if ($null -ne $mapping) {
        $mapping.Dispose()
    }
    if ($null -ne $writer) {
        $writer.Dispose()
    }
}

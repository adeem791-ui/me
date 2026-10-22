# Windows PowerShell 5.1+; no external Python packages required.
# This is a *sampler*, not a CPU-affinity optimizer. NVIDIA counters are whole-GPU.
[CmdletBinding()]
param(
    [ValidateRange(1, 60)][int]$IntervalSeconds = 2,
    [ValidateRange(10, 86400)][int]$DurationSeconds = 900,
    [string]$ProcessName = 'kyty_emulator',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
    $root = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $PSScriptRoot }
    $OutputDirectory = Join-Path $root 'KytyPS5\Telemetry'
}
[void][System.IO.Directory]::CreateDirectory($OutputDirectory)
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$output = Join-Path $OutputDirectory "performance-$stamp.csv"
$logicalProcessors = [Environment]::ProcessorCount
$previous = @{}
$sampleCount = 0

# NVIDIA's GPU utilization and VRAM numbers describe the *entire* GPU, not this process.
$smiCommand = Get-Command 'nvidia-smi.exe' -ErrorAction SilentlyContinue
$smi = if ($smiCommand) { $smiCommand.Source } else { '' }
if (-not $smi) {
    $fallback = 'C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe'
    if (Test-Path -LiteralPath $fallback) { $smi = $fallback }
}
function Get-WholeGpuSample {
    param([string]$Executable)
    $result = [ordered]@{
        gpu_name = ''
        gpu_utilization_whole_pct = $null
        vram_used_whole_mib = $null
        vram_total_whole_mib = $null
    }
    if (-not $Executable) { return [pscustomobject]$result }
    try {
        $lines = @(& $Executable '--query-gpu=index,name,utilization.gpu,memory.used,memory.total' '--format=csv,noheader,nounits' 2>$null)
        $candidates = @()
        foreach ($line in $lines) {
            $fields = @("$line" -split ',\s*')
            if ($fields.Count -lt 5) { continue }
            $candidates += ,$fields
        }
        if ($candidates.Count -eq 0) { return [pscustomobject]$result }
        # Prefer the user's RTX 5080 when multiple NVIDIA GPUs exist.
        $chosen = $candidates[0]
        foreach ($candidate in $candidates) {
            if ($candidate[1] -match '5080') { $chosen = $candidate; break }
        }
        $result.gpu_name = $chosen[1].Trim()
        $numeric = @(2, 3, 4)
        $keys = @('gpu_utilization_whole_pct', 'vram_used_whole_mib', 'vram_total_whole_mib')
        for ($i = 0; $i -lt $keys.Count; $i++) {
            [double]$value = 0
            if ([double]::TryParse($chosen[$numeric[$i]].Trim(),
                    [System.Globalization.NumberStyles]::Float,
                    [System.Globalization.CultureInfo]::InvariantCulture,
                    [ref]$value)) {
                $result[$keys[$i]] = $value
            }
        }
    } catch {
        # Keep CPU logging alive when NVML is absent, blocked, or the GPU driver resets.
    }
    return [pscustomobject]$result
}

Write-Host "Sampling process '$ProcessName' every $IntervalSeconds second(s)."
Write-Host "CSV: $output"
Write-Host 'GPU percentage is WHOLE-GPU activity, not verified emulator-specific GPU use.'
Write-Host 'Thread percentages are percentages of ONE logical CPU core.'
$stopAt = (Get-Date).AddSeconds($DurationSeconds)
try {
    while ((Get-Date) -lt $stopAt) {
        $processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
        if ($processes.Count -eq 0) {
            if ($sampleCount -eq 0) { Write-Host "Waiting for $ProcessName to start..." }
            Start-Sleep -Seconds $IntervalSeconds
            continue
        }
        $gpu = Get-WholeGpuSample -Executable $smi
        foreach ($proc in $processes) {
            try {
                $observed = (Get-Date).ToUniversalTime()
                $proc.Refresh()
                $threads = @($proc.Threads)
                $totalCpuMs = $proc.TotalProcessorTime.TotalMilliseconds
                $currentThreads = @{}
                foreach ($thread in $threads) {
                    try { $currentThreads[[string]$thread.Id] = $thread.TotalProcessorTime.TotalMilliseconds }
                    catch { } # An exited/inaccessible thread should not end the sampler.
                }

                $coreEquivalents = $null
                $machineCpuPct = $null
                $hotThreads = ''
                $processKey = [string]$proc.Id
                if ($previous.ContainsKey($processKey)) {
                    $before = $previous[$processKey]
                    $wallMs = ($observed - $before.wall).TotalMilliseconds
                    $deltaMs = $totalCpuMs - $before.cpu_ms
                    if ($wallMs -gt 0 -and $deltaMs -ge 0) {
                        $coreEquivalents = [Math]::Round($deltaMs / $wallMs, 3)
                        $machineCpuPct = [Math]::Round(100.0 * $deltaMs / ($wallMs * $logicalProcessors), 2)
                        $perThread = @(
                            foreach ($id in $currentThreads.Keys) {
                                if ($before.thread_ms.ContainsKey($id)) {
                                    $threadDelta = $currentThreads[$id] - $before.thread_ms[$id]
                                    if ($threadDelta -ge 0) {
                                        [pscustomobject]@{
                                            id = $id
                                            pct_one_core = [Math]::Round(100.0 * $threadDelta / $wallMs, 1)
                                        }
                                    }
                                }
                            }
                        )
                        $hotThreads = (@($perThread | Sort-Object pct_one_core -Descending |
                            Select-Object -First 3 | ForEach-Object {
                                "$($_.id):$($_.pct_one_core)%"
                            })) -join ';'
                    }
                }
                $previous[$processKey] = @{
                    wall = $observed
                    cpu_ms = $totalCpuMs
                    thread_ms = $currentThreads
                }
                $row = [pscustomobject][ordered]@{
                    utc_timestamp = $observed.ToString('o')
                    process_name = $proc.ProcessName
                    pid = $proc.Id
                    cpu_core_equivalents = $coreEquivalents
                    cpu_percent_whole_machine = $machineCpuPct
                    busy_threads_top3_pct_one_core = $hotThreads
                    thread_count = $threads.Count
                    ram_working_set_mib = [Math]::Round($proc.WorkingSet64 / 1MB, 1)
                    private_memory_mib = [Math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
                    gpu_name_whole = $gpu.gpu_name
                    gpu_utilization_whole_pct = $gpu.gpu_utilization_whole_pct
                    vram_used_whole_mib = $gpu.vram_used_whole_mib
                    vram_total_whole_mib = $gpu.vram_total_whole_mib
                }
                $row | Export-Csv -LiteralPath $output -NoTypeInformation -Append -Encoding UTF8
                $sampleCount++
            } catch {
                Write-Warning "Skipping one process sample: $($_.Exception.Message)"
            }
        }
        Start-Sleep -Seconds $IntervalSeconds
    }
} finally {
    Write-Host "Finished. Wrote $sampleCount process samples."
    if ($sampleCount -gt 0) {
        Write-Host "Local CSV: $output"
        Write-Host 'Keep game files and shader archives private. Remove sensitive paths before sharing logs.'
    } else {
        Write-Host "No matching '$ProcessName' process was observed. Check the process name in Task Manager."
    }
}

# Read-only CPU and GPU monitor for the Kyty emulator (Windows PowerShell 5.1, no admin).
#
# Every second it writes one CSV row with:
#   - process CPU: total as % of the whole machine, and "cores used" (1.0 = one full core)
#   - threads: how many threads are busy (>= 10% of one core) and the busiest few
#   - GPU (NVIDIA): GPU busy %, memory-controller busy %, VRAM used/total, GPU power
#   - VRAM used by the emulator process itself, when the driver reports it
#
# Run it, then start the game. It stops when the emulator exits or after -DurationSeconds.
# Rows are appended as they are measured, so the file is still complete if the game crashes.
#
# Examples:
#   powershell -ExecutionPolicy Bypass -File MONITOR_CPU_GPU.ps1
#   powershell -ExecutionPolicy Bypass -File MONITOR_CPU_GPU.ps1 -DurationSeconds 600 -ProcessName kyty_emulator

param(
    [string]$ProcessName = 'kyty_emulator',
    [int]$DurationSeconds = 0,          # 0 = until the process exits
    [int]$IntervalMs = 1000,
    [int]$WaitForProcessSeconds = 300
)

$ErrorActionPreference = 'Stop'
$cores = [Environment]::ProcessorCount
$OutFile = Join-Path $PSScriptRoot ('Kyty_CPU_GPU_Monitor_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.csv')

$header = 'time,elapsed_s,proc_cpu_percent_of_machine,proc_cores_used,threads_total,threads_busy_10pct,busiest_thread_percent_of_core,top_threads,gpu_busy_percent,gpu_mem_busy_percent,vram_used_mib,vram_total_mib,gpu_power_w,emulator_vram_mib'
Set-Content -LiteralPath $OutFile -Value $header -Encoding UTF8

function Get-GpuSample {
    # nvidia-smi ships with the NVIDIA driver. If it is missing, GPU columns stay empty.
    $smi = Join-Path $env:SystemRoot 'System32\nvidia-smi.exe'
    if (-not (Test-Path -LiteralPath $smi)) { return $null }
    try {
        $line = (& $smi --query-gpu=utilization.gpu,utilization.memory,memory.used,memory.total,power.draw --format=csv,noheader,nounits 2>$null | Select-Object -First 1)
        if (-not $line) { return $null }
        $v = @($line -split ',' | ForEach-Object { $_.Trim() })
        return [pscustomobject]@{
            GpuBusy = $v[0]; MemBusy = $v[1]; VramUsed = $v[2]; VramTotal = $v[3]; Power = $v[4]
        }
    } catch { return $null }
}

function Get-EmulatorVram {
    # Per-process VRAM. Windows graphics apps may show nothing here; that is not an error.
    $smi = Join-Path $env:SystemRoot 'System32\nvidia-smi.exe'
    if (-not (Test-Path -LiteralPath $smi)) { return '' }
    try {
        $rows = & $smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>$null
        foreach ($r in $rows) {
            $v = @($r -split ',' | ForEach-Object { $_.Trim() })
            if ($v.Count -ge 2 -and $v[0] -eq [string]$script:targetId) { return $v[1] }
        }
    } catch { }
    return ''
}

# Wait for the emulator to start.
$deadline = [DateTime]::UtcNow.AddSeconds($WaitForProcessSeconds)
$proc = $null
Write-Host "Waiting for process '$ProcessName' ... start the game in Kyty."
while ($null -eq $proc -and [DateTime]::UtcNow -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Sort-Object CPU -Descending | Select-Object -First 1
    if ($null -eq $proc) { Start-Sleep -Seconds 1 }
}
if ($null -eq $proc) { Write-Host 'Process not found. Nothing recorded.'; exit 2 }

$script:targetId = [int]$proc.Id
Write-Host "Recording process $($proc.Id) to: $OutFile"
Write-Host 'Stop with Ctrl+C. Rows are saved as they are measured.'

$previousThreads = @{}
$previousProcMs = $null
$previousTime = $null
$start = [DateTime]::UtcNow
$rows = 0

try {
    while ($true) {
        $proc = Get-Process -Id $script:targetId -ErrorAction SilentlyContinue
        if ($null -eq $proc) { Write-Host 'Emulator exited. Stopping.'; break }
        if ($DurationSeconds -gt 0 -and ([DateTime]::UtcNow - $start).TotalSeconds -ge $DurationSeconds) { break }

        $now = [DateTime]::UtcNow
        $procMs = $proc.TotalProcessorTime.TotalMilliseconds
        $threadMs = @{}
        foreach ($t in $proc.Threads) {
            try { $threadMs[[int]$t.Id] = [double]$t.TotalProcessorTime.TotalMilliseconds } catch { }
        }

        if ($null -ne $previousTime) {
            $elapsedMs = ($now - $previousTime).TotalMilliseconds
            if ($elapsedMs -gt 0) {
                $procCoresUsed = [math]::Max(0.0, ($procMs - $previousProcMs) / $elapsedMs)
                $procPercentMachine = [math]::Round($procCoresUsed / $cores * 100, 1)

                $deltas = @()
                foreach ($id in $threadMs.Keys) {
                    if ($previousThreads.ContainsKey($id)) {
                        $deltas += [pscustomobject]@{ Id = $id; Pct = [math]::Round([math]::Max(0.0, $threadMs[$id] - $previousThreads[$id]) / $elapsedMs * 100, 1) }
                    }
                }
                $sorted = @($deltas | Sort-Object Pct -Descending)
                $busy = @($sorted | Where-Object { $_.Pct -ge 10 }).Count
                $busiest = if ($sorted.Count -gt 0) { $sorted[0].Pct } else { 0 }
                $top = (@($sorted | Select-Object -First 5) | ForEach-Object { "$($_.Id):$($_.Pct)" }) -join ';'

                $gpu = Get-GpuSample
                $emuVram = Get-EmulatorVram

                $line = @(
                    $now.ToLocalTime().ToString('HH:mm:ss.fff'),
                    [math]::Round(($now - $start).TotalSeconds, 1),
                    $procPercentMachine,
                    [math]::Round($procCoresUsed, 2),
                    $threadMs.Count,
                    $busy,
                    $busiest,
                    $top,
                    $(if ($gpu) { $gpu.GpuBusy } else { '' }),
                    $(if ($gpu) { $gpu.MemBusy } else { '' }),
                    $(if ($gpu) { $gpu.VramUsed } else { '' }),
                    $(if ($gpu) { $gpu.VramTotal } else { '' }),
                    $(if ($gpu) { $gpu.Power } else { '' }),
                    $emuVram
                ) -join ','
                Add-Content -LiteralPath $OutFile -Value $line -Encoding UTF8
                $rows++
                if ($rows % 10 -eq 0) {
                    $gpuText = if ($gpu) { "GPU $($gpu.GpuBusy)% VRAM $($gpu.VramUsed)/$($gpu.VramTotal) MiB" } else { 'GPU n/a' }
                    Write-Host ("[{0}] CPU {1}% of machine ({2} cores), busy threads {3}, {4}" -f (Get-Date -Format 'HH:mm:ss'), $procPercentMachine, [math]::Round($procCoresUsed, 2), $busy, $gpuText)
                }
            }
        }

        $previousThreads = $threadMs
        $previousProcMs = $procMs
        $previousTime = $now
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host "Saved $rows rows: $OutFile"
}

# Read-only, no-administrator Windows CPU sampling for the Kyty emulator.
# 100% CPU here is one logical core, not 100% of the whole Ryzen.
$ErrorActionPreference = 'Stop'
$Samples = 30
$SampleSeconds = 2
$OutFile = Join-Path $PSScriptRoot ("Kyty_CPU_Thread_Profile_" + (Get-Date -Format 'yyyyMMdd_HHmmss') + ".csv")
$rows = [System.Collections.Generic.List[object]]::new()
$previous = @{}
$lastProcessCpuMs = $null
$lastTime = $null
Write-Host 'Open Ghost of Yotei in Kyty; waiting for kyty_emulator.exe...'
$deadline = [DateTime]::UtcNow.AddMinutes(2)
$p = $null
while ([DateTime]::UtcNow -lt $deadline) {
    $p = Get-Process -Name kyty_emulator -ErrorAction SilentlyContinue | Sort-Object CPU -Descending | Select-Object -First 1
    if ($null -ne $p) { break }
    Start-Sleep -Seconds 2
}
if ($null -eq $p) { Write-Host 'Emulator process not found.'; exit 2 }
$targetId = [int]$p.Id
Write-Host "Sampling process ID $targetId for approximately 60 seconds..."
try {
    for ($i = 0; $i -le $Samples; $i++) {
        $p = Get-Process -Id $targetId -ErrorAction Stop
        $now = [DateTime]::UtcNow
        $cpuMs = $p.TotalProcessorTime.TotalMilliseconds
        $threadTimes = @{}
        foreach ($t in $p.Threads) {
            try { $threadTimes[[int]$t.Id] = [double]$t.TotalProcessorTime.TotalMilliseconds }
            catch { }
        }
        if ($null -ne $lastTime) {
            $elapsedMs = ($now - $lastTime).TotalMilliseconds
            if ($elapsedMs -gt 0) {
                $threadDeltas = @(
                    foreach ($id in $threadTimes.Keys) {
                        if ($previous.ContainsKey($id)) {
                            [pscustomobject]@{ Id = $id; DeltaMs = [math]::Max(0.0, $threadTimes[$id] - $previous[$id]) }
                        }
                    }
                )
                $hot = @($threadDeltas | Sort-Object DeltaMs -Descending | Select-Object -First 6)
                $hotThreads = ($hot | ForEach-Object {
                    "$($_.Id):$([math]::Round($_.DeltaMs / $elapsedMs * 100, 1))%"
                }) -join ';'
                $procPercent = [math]::Round([math]::Max(0, ($cpuMs - $lastProcessCpuMs) / $elapsedMs * 100), 1)
                $peakPercent = if ($hot.Count -gt 0) { [math]::Round($hot[0].DeltaMs / $elapsedMs * 100, 1) } else { 0 }
                $activeCount = @($threadDeltas | Where-Object { $_.DeltaMs -ge ($elapsedMs * 0.10) }).Count
                $row = [pscustomobject]@{
                    Time = $now.ToLocalTime().ToString('HH:mm:ss')
                    ProcessCpuPercentOneCore100 = $procPercent
                    BusiestThreadPercent = $peakPercent
                    ThreadsOver10Percent = $activeCount
                    ThreadCount = $threadTimes.Count
                    LogicalCoresVisible = [Environment]::ProcessorCount
                    TopThreads = $hotThreads
                }
                $rows.Add($row)
                Write-Host "[$($row.Time)] total=$procPercent% busiest=$peakPercent% active_threads=$activeCount"
            }
        }
        $previous = $threadTimes
        $lastProcessCpuMs = $cpuMs
        $lastTime = $now
        if ($i -lt $Samples) { Start-Sleep -Seconds $SampleSeconds }
    }
}
catch { Write-Host "Profiling stopped: $($_.Exception.Message)" }
finally {
    if ($rows.Count -gt 0) {
        $rows | Export-Csv -LiteralPath $OutFile -NoTypeInformation -Encoding UTF8
        Write-Host "Saved CPU profiling CSV: $OutFile"
        Write-Host 'Share this CSV and GpuCpuProfile lines in _kyty log, not game files.'
    } else { Write-Host 'No complete sample captured.' }
}

@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0kyty_launcher.exe" (
  echo ERROR: kyty_launcher.exe must be in the same folder.
  pause
  exit /b 1
)
if not defined LOCALAPPDATA (
  echo ERROR: LOCALAPPDATA is not set.
  pause
  exit /b 1
)
set "KYTY_PIPELINE_CACHE_DIR=%LOCALAPPDATA%\KytyPS5\PipelineCache"
set "KYTY_GPU_CPU_PROFILE=1"
rem Read-only CPU-side timing, no scheduling/affinity/priority changes.
echo GPU/CPU diagnostics enabled. Search emulator log for GpuCpuProfile:
echo Play for 20 seconds, then run PROFILE_CPU_THREADS.cmd while game is running.
start "" /D "%~dp0" "%~dp0kyty_launcher.exe"

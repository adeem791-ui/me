@echo off
setlocal
rem Launch the portable KytyPS5 build with persistent cache and opt-in telemetry.
rem All measurements are saved locally. No results are uploaded.
if not exist "%~dp0kyty_launcher.exe" (
  echo kyty_launcher.exe is missing. Run this from an extracted portable build folder.
  pause
  exit /b 1
)
if not exist "%~dp0SAMPLE_EMULATOR_PERFORMANCE.ps1" (
  echo SAMPLE_EMULATOR_PERFORMANCE.ps1 is missing.
  pause
  exit /b 1
)
if exist "%~dp0START_SHARED_SHADER_CACHE.cmd" (
  call "%~dp0START_SHARED_SHADER_CACHE.cmd"
) else (
  start "" /D "%~dp0" "%~dp0kyty_launcher.exe"
)
echo.
echo Starting performance sampler for 15 minutes.
echo Choose a game in the launcher and let the sampler run while testing.
echo CPU samples are per-process. NVIDIA GPU utilization is WHOLE-GPU.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SAMPLE_EMULATOR_PERFORMANCE.ps1" -IntervalSeconds 2 -DurationSeconds 900
echo.
echo Diagnostic session complete.
pause

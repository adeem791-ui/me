@echo off
setlocal
cd /d "%~dp0"
if not defined LOCALAPPDATA (
  echo ERROR: LOCALAPPDATA is not set.
  pause
  exit /b 1
)
set "KYTY_PIPELINE_CACHE_DIR=%LOCALAPPDATA%\KytyPS5\PipelineCache"
if not exist "%KYTY_PIPELINE_CACHE_DIR%" mkdir "%KYTY_PIPELINE_CACHE_DIR%"
set "KYTY_RECORD_COMPUTE=1"
echo Recording successfully compiled compute pipelines into:
echo   %KYTY_PIPELINE_CACHE_DIR%\compute_recordings\PPSA26344
if not exist "%~dp0kyty_launcher.exe" (
  echo ERROR: kyty_launcher.exe not found here. Put this script next to the executable.
  pause
  exit /b 1
)
echo Launching the emulator. Data is recorded only when Vulkan successfully compiles pipelines.
start "" /D "%~dp0" "%~dp0kyty_launcher.exe"
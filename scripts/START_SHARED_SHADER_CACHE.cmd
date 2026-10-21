@echo off
setlocal
rem Opt-in persistent driver pipeline cache shared by successive portable builds.
rem Existing Vulkan driver UUID/version and integrity checks are still applied.
if not defined LOCALAPPDATA (
  echo LOCALAPPDATA is not defined. Cannot use a shared cache.
  pause
  exit /b 1
)
set "KYTY_PIPELINE_CACHE_DIR=%LOCALAPPDATA%\KytyPS5\PipelineCache"
if not exist "%KYTY_PIPELINE_CACHE_DIR%" mkdir "%KYTY_PIPELINE_CACHE_DIR%"
if not exist "%KYTY_PIPELINE_CACHE_DIR%" (
  echo Could not create shared cache folder.
  pause
  exit /b 1
)
rem Copy old per-title caches once. Never overwrite a newer shared cache.
for %%F in ("%~dp0_PipelineCache\*.bin") do (
  if exist "%%~fF" if not exist "%KYTY_PIPELINE_CACHE_DIR%\%%~nxF" copy /Y "%%~fF" "%KYTY_PIPELINE_CACHE_DIR%\%%~nxF" >nul
)
echo KytyPS5 shared Vulkan pipeline cache: %KYTY_PIPELINE_CACHE_DIR%
echo Launching KytyPS5 with cache persistence enabled.
start "" /D "%~dp0" "%~dp0kyty_launcher.exe"

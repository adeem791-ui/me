@echo off
setlocal
cd /d "%~dp0"
if not defined LOCALAPPDATA (
  echo ERROR: LOCALAPPDATA is not set.
  pause
  exit /b 1
)
set "KYTY_PIPELINE_CACHE_DIR=%LOCALAPPDATA%\KytyPS5\PipelineCache"
set "RECORD_DIR=%KYTY_PIPELINE_CACHE_DIR%\compute_recordings\PPSA26344"
set "CACHE=%KYTY_PIPELINE_CACHE_DIR%\PPSA26344.bin"
if not exist "%~dp0kyty_compute_prewarm.exe" (
  echo ERROR: kyty_compute_prewarm.exe not found in emulator folder.
  echo Download a successful experimental Actions artifact first.
  pause
  exit /b 1
)
echo IMPORTANT: CLOSE KYTYPS5 BEFORE YOU CONTINUE.
echo Warmup will use your NVIDIA GPU to build pipelines in advance.
echo A warmup cache is valid only for a compatible game translation, GPU and driver.
echo Your existing cache will be left intact if warmup cannot save successfully.
pause
"%~dp0kyty_compute_prewarm.exe" --records "%RECORD_DIR%" --cache "%CACHE%" --workers 2
set "RESULT=%ERRORLEVEL%"
echo.
if "%RESULT%"=="0" (
  echo Prewarm completed. Launch via START_SHARED_SHADER_CACHE.cmd.
) else (
  echo Prewarm did not complete successfully. Exit code: %RESULT%
  echo See error message above. Do not erase your old cache.
)
pause
exit /b %RESULT%
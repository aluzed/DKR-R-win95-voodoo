@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Prepare-Game-Source.ps1" %*
set "exitCode=%ERRORLEVEL%"
if not "%exitCode%"=="0" (
    echo.
    echo [FAILED] The DKR source could not be prepared.
    echo Read the error above and the log path printed by the script.
    pause
)
exit /b %exitCode%

@echo off
setlocal
cd /d "%~dp0"

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Windows PowerShell could not be found.
    echo This build script requires 64-bit Windows 10 or Windows 11 with PowerShell enabled.
    echo.
    echo Install/repair Windows PowerShell, then run this file again.
    if not "%DKRPORT_NO_PAUSE%"=="1" pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Build-Windows.ps1" %*
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo [SUCCESS] DKR Port built successfully.
    echo Run Run-Windows.cmd to start the native launcher.
) else (
    echo [FAILED] The build did not complete.
    echo Read the error above and use the build-log path printed by the PowerShell script.
)

if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b %RESULT%

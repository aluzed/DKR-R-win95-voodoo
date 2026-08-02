@echo off
setlocal
cd /d "%~dp0"
echo DKR Port - prepare and compile the first native runtime boundary
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Prepare-DKR-Runtime.ps1" %*
set "exitCode=%ERRORLEVEL%"
if not "%exitCode%"=="0" (
    echo.
    echo [FAILED] DKR runtime preparation did not complete.
    echo Read the prepare-dkr-runtime log path printed above. The first DKR-specific
    echo recompiler or compiler error is the next integration issue to fix.
) else (
    echo.
    echo [OK] The DKR ELF, generated CPU code and runtime probe were prepared.
)
pause
exit /b %exitCode%

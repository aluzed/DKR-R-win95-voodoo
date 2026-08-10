@echo off
setlocal
cd /d "%~dp0"
set "APP=%~dp0build\dkr-runtime-rt64\bin\Release\DKR-R.exe"
if not exist "%APP%" (
    echo [ERROR] The built application was not found:
    echo %APP%
    echo.
    echo Run Build-Windows.cmd first.
    pause
    exit /b 1
)
start "DKR-R" "%APP%"

@echo off
setlocal
cd /d "%~dp0"
set "APP=%~dp0dist\DKRPort-Windows-x64\DKRPort.exe"
if not exist "%APP%" (
    echo [ERROR] The built application was not found:
    echo %APP%
    echo.
    echo Run Build-Windows.cmd first.
    pause
    exit /b 1
)
start "DKR Port" "%APP%"

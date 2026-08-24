@echo off
setlocal
cd /d "%~dp0..\.."

if "%~1"=="" (
    echo Usage: example\phase-4-safety\run.bat ^<main^|startup-held^>
    exit /b 2
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -Mode "%~1"
exit /b %errorlevel%

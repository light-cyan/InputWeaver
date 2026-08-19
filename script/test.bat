@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\Phase1Tests.exe" (
    echo Phase1Tests.exe is missing. Run script\build.bat first.
    exit /b 2
)

"bin\Phase1Tests.exe"
exit /b %errorlevel%

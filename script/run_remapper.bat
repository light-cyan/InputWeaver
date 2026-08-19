@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\UniversalKeyRemapper.exe" (
    echo UniversalKeyRemapper.exe is missing. Run script\build.bat first.
    exit /b 2
)

"bin\UniversalKeyRemapper.exe" %*
exit /b %errorlevel%

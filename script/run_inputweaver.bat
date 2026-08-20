@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\InputWeaver.exe" (
    echo InputWeaver.exe is missing. Run script\build.bat first.
    exit /b 2
)

"bin\InputWeaver.exe" %*
exit /b %errorlevel%

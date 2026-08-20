@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\InputWeaverTests.exe" (
    echo InputWeaverTests.exe is missing. Run script\build.bat first.
    exit /b 2
)

"bin\InputWeaverTests.exe"
exit /b %errorlevel%

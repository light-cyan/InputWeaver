@echo off
setlocal
cd /d "%~dp0.."
if not exist "bin\MouseStateTests.exe" (
    echo MouseStateTests.exe is missing. Run script\build_mouse_tests.bat first.
    exit /b 2
)
"bin\MouseStateTests.exe"
exit /b %errorlevel%

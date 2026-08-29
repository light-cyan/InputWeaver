@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\WindowsPlatformTests.exe" (
    echo WindowsPlatformTests.exe is missing. Run script\build_executor_tests.bat first.
    exit /b 2
)

if not exist "bin\CompiledProgramTests.exe" (
    echo CompiledProgramTests.exe is missing. Run script\build_executor_tests.bat first.
    exit /b 2
)

"bin\WindowsPlatformTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\CompiledProgramTests.exe"
exit /b %errorlevel%

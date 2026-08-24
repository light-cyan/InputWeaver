@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\ProgramRuntimeTests.exe" (
    echo ProgramRuntimeTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\WindowsRuntimeAdapterTests.exe" (
    echo WindowsRuntimeAdapterTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

"bin\ProgramRuntimeTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsRuntimeAdapterTests.exe"
exit /b %errorlevel%

@echo off
setlocal
cd /d "%~dp0.."

call script\test_mouse.bat
if errorlevel 1 exit /b %errorlevel%

if not exist "bin\ProgramRuntimeTests.exe" (
    echo ProgramRuntimeTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\WindowsRuntimeAdapterTests.exe" (
    echo WindowsRuntimeAdapterTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\DebugProtocolTests.exe" (
    echo DebugProtocolTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\DebugClientTests.exe" (
    echo DebugClientTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\WindowsDebugClientTests.exe" (
    echo WindowsDebugClientTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\WindowsDebugServerTests.exe" (
    echo WindowsDebugServerTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

if not exist "bin\RuntimeCliTests.exe" (
    echo RuntimeCliTests.exe is missing. Run script\build_runtime_tests.bat first.
    exit /b 2
)

"bin\ProgramRuntimeTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsRuntimeAdapterTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\DebugProtocolTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\DebugClientTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsDebugClientTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsDebugServerTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\RuntimeCliTests.exe"
exit /b %errorlevel%

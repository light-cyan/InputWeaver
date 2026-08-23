@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\InputWeaverTests.exe" (
    echo InputWeaverTests.exe is missing. Run script\build.bat first.
    exit /b 2
)

if not exist "bin\CompiledProgramTests.exe" (
    echo CompiledProgramTests.exe is missing. Run script\build.bat first.
    exit /b 2
)

if not exist "bin\ProgramRuntimeTests.exe" (
    echo ProgramRuntimeTests.exe is missing. Run script\build.bat first.
    exit /b 2
)

if not exist "bin\WindowsRuntimeAdapterTests.exe" (
    echo WindowsRuntimeAdapterTests.exe is missing. Run script\build.bat first.
    exit /b 2
)

"bin\InputWeaverTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\CompiledProgramTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\ProgramRuntimeTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsRuntimeAdapterTests.exe"
exit /b %errorlevel%

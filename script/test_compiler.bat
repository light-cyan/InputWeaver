@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\CompilerTests.exe" (
    echo CompilerTests.exe is missing. Run script\build_compiler_tests.bat first.
    exit /b 2
)

"bin\CompilerTests.exe"
exit /b %errorlevel%

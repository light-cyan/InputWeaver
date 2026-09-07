@echo off
setlocal
cd /d "%~dp0.."

if "%~1"=="--skip-build" goto test
if not "%~1"=="" goto usage
call script\build_compiler.bat
if errorlevel 1 exit /b %errorlevel%

:test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "script\test_docs.ps1"
exit /b %errorlevel%

:usage
echo Usage: script\test_docs.bat [--skip-build]
exit /b 2

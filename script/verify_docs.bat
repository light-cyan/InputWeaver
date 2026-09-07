@echo off
setlocal
cd /d "%~dp0.."

if "%~1"=="--skip-build" goto verify
if not "%~1"=="" goto usage
call script\build_compiler.bat
if errorlevel 1 exit /b %errorlevel%

:verify
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "script\verify_docs.ps1"
exit /b %errorlevel%

:usage
echo Usage: script\verify_docs.bat [--skip-build]
exit /b 2

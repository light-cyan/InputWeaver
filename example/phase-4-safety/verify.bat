@echo off
setlocal
cd /d "%~dp0..\.."

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify.ps1"
exit /b %errorlevel%

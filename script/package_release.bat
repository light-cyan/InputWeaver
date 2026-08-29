@echo off
setlocal
cd /d "%~dp0.."

echo [1/2] Building product executables...
call script\build_products.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Creating the portable Windows x64 package...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "script\package_release.ps1"
if errorlevel 1 exit /b %errorlevel%

echo Release package completed successfully.
exit /b 0

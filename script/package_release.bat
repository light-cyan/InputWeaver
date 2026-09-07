@echo off
setlocal
cd /d "%~dp0.."

echo [1/3] Building product executables...
call script\build_products.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Validating documentation and examples...
call script\verify_docs.bat --skip-build
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Creating the portable Windows x64 package...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "script\package_release.ps1"
if errorlevel 1 exit /b %errorlevel%

echo Release package completed successfully.
exit /b 0

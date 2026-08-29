@echo off
setlocal
cd /d "%~dp0.."

echo [1/2] Building product executables...
call script\build_products.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building test executables...
call script\build_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo Complete build finished successfully.
exit /b 0

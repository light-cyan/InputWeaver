@echo off
setlocal
cd /d "%~dp0.."

echo [1/5] Building all products and tests...
call script\build.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/5] Running all tests...
call script\test.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/5] Analyzing all tracked implementations...
call script\analyze_all.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/5] Auditing dependencies...
call script\audit_dependencies.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/5] Checking the working diff...
git diff --check
if errorlevel 1 exit /b %errorlevel%

echo Project verification completed successfully.
exit /b 0

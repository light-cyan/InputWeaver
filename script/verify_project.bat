@echo off
setlocal
cd /d "%~dp0.."

echo [1/6] Building all products and tests...
call script\build.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/6] Running all tests...
call script\test.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/6] Validating documentation and examples...
call script\test_docs.bat --skip-build
if errorlevel 1 exit /b %errorlevel%
call script\verify_docs.bat --skip-build
if errorlevel 1 exit /b %errorlevel%

echo [4/6] Analyzing all tracked implementations...
call script\analyze_all.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/6] Auditing dependencies...
call script\audit_dependencies.bat
if errorlevel 1 exit /b %errorlevel%

echo [6/6] Checking the working diff...
git diff --check
if errorlevel 1 exit /b %errorlevel%

echo Project verification completed successfully.
exit /b 0

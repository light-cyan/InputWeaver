@echo off
setlocal
cd /d "%~dp0.."

echo [1/7] Building the runtime and shared program tests...
call script\build.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/7] Building the compiler and compiler tests...
call script\build_compiler_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/7] Running the runtime and shared program tests...
call script\test.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/7] Running the compiler tests...
call script\test_compiler.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/7] Analyzing all tracked implementations...
call script\analyze_all.bat
if errorlevel 1 exit /b %errorlevel%

echo [6/7] Auditing dependencies...
call script\audit_dependencies.bat
if errorlevel 1 exit /b %errorlevel%

echo [7/7] Checking the working diff...
git diff --check
if errorlevel 1 exit /b %errorlevel%

echo Phase 3 verification completed successfully.
exit /b 0

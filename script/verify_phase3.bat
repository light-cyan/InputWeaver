@echo off
setlocal
cd /d "%~dp0.."

echo [1/8] Building the runtime and shared program tests...
call script\build.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/8] Building the compiler and compiler tests...
call script\build_compiler_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/8] Running the runtime and shared program tests...
call script\test.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/8] Running the compiler tests...
call script\test_compiler.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/8] Analyzing the compiler...
call script\analyze_compiler.bat
if errorlevel 1 exit /b %errorlevel%

echo [6/8] Analyzing the runtime...
call script\analyze_runtime.bat
if errorlevel 1 exit /b %errorlevel%

echo [7/8] Auditing Phase 3 dependencies...
call script\audit_phase3_dependencies.bat
if errorlevel 1 exit /b %errorlevel%

echo [8/8] Checking the working diff...
git diff --check
if errorlevel 1 exit /b %errorlevel%

echo Phase 3 verification completed successfully.
exit /b 0

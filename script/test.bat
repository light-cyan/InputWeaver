@echo off
setlocal
cd /d "%~dp0.."

echo [1/4] Running executor and program tests...
call script\test_executor.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/4] Running compiler tests...
call script\test_compiler.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Running application and TUI tests...
call script\test_app.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Running runtime and debug tests...
call script\test_runtime.bat
if errorlevel 1 exit /b %errorlevel%

echo All tests completed successfully.
exit /b 0

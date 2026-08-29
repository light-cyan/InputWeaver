@echo off
setlocal
cd /d "%~dp0.."

echo [1/4] Building executor and program tests...
call script\build_executor_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/4] Building compiler tests...
call script\build_compiler_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Building application and TUI tests...
call script\build_app_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Building runtime and debug tests...
call script\build_runtime_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo Test build completed successfully.
exit /b 0

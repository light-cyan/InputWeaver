@echo off
setlocal
cd /d "%~dp0.."

echo [1/5] Building language tests...
call script\build_language_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/5] Building executor and program tests...
call script\build_executor_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/5] Building compiler tests...
call script\build_compiler_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [4/5] Building application and TUI tests...
call script\build_app_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/5] Building runtime and debug tests...
call script\build_runtime_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo Test build completed successfully.
exit /b 0

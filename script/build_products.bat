@echo off
setlocal
cd /d "%~dp0.."

echo [1/3] Building InputWeaver.exe...
call script\build_executor.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Building InputWeaverCompiler.exe...
call script\build_compiler.bat
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Building the control UI host and frontend...
call script\build_tui.bat
if errorlevel 1 exit /b %errorlevel%

echo Product build completed successfully.
exit /b 0

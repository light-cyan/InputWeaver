@echo off
setlocal
cd /d "%~dp0.."

echo [1/3] Building InputWeaver.exe and InputWeaverTUI.exe...
call script\build.bat --products-only
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Building InputWeaverCompiler.exe...
call script\build_compiler_tests.bat --products-only
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Creating the portable Windows x64 package...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "script\package_release.ps1"
if errorlevel 1 exit /b %errorlevel%

echo Release package completed successfully.
exit /b 0

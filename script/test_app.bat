@echo off
setlocal
cd /d "%~dp0.."

for %%F in (ApplicationTests.exe TuiTests.exe WindowsAppPlatformTests.exe WindowsTuiIpcTests.exe) do (
    if not exist "bin\%%F" (
        echo %%F is missing. Run script\build_app_tests.bat first.
        exit /b 2
    )
)

"bin\ApplicationTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\TuiTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsAppPlatformTests.exe"
if errorlevel 1 exit /b %errorlevel%

"bin\WindowsTuiIpcTests.exe"
exit /b %errorlevel%

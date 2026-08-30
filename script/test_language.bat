@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin\LanguageTests.exe" (
    echo LanguageTests.exe is missing. Run script\build_language_tests.bat first.
    exit /b 2
)

"bin\LanguageTests.exe"
exit /b %errorlevel%

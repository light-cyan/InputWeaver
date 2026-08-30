@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -Isrc"

echo Building LanguageTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\language\lexer_tests.cpp" ^
    "src\language\lexer.cpp" ^
    -o "bin\LanguageTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo Language test build completed successfully.
exit /b 0

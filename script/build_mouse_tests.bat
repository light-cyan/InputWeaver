@echo off
setlocal
cd /d "%~dp0.."
if not exist "bin" mkdir "bin"

g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -Isrc ^
    "tests\runtime\mouse_state_tests.cpp" ^
    "src\runtime\mouse_state.cpp" ^
    "src\runtime\mouse_fields.cpp" ^
    -o "bin\MouseStateTests.exe"
exit /b %errorlevel%

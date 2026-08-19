@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "UKR_CXX=g++"
set "UKR_WINDRES=windres"
set "UKR_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/3] Compiling the execution manifest...
%UKR_WINDRES% -DUNICODE -D_UNICODE "res\UniversalKeyRemapper.rc" -O coff -o "bin\UniversalKeyRemapper.res.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Building UniversalKeyRemapper.exe...
%UKR_CXX% %UKR_COMMON% -municode ^
    "src\main.cpp" ^
    "src\platform\windows\hook_thread.cpp" ^
    "src\core\fixed_rules.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    "bin\UniversalKeyRemapper.res.o" ^
    -o "bin\UniversalKeyRemapper.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Building Phase1Tests.exe...
%UKR_CXX% %UKR_COMMON% ^
    "tests\runtime_tests.cpp" ^
    "src\platform\windows\hook_thread.cpp" ^
    "src\core\fixed_rules.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    -o "bin\Phase1Tests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo Build completed successfully.
exit /b 0

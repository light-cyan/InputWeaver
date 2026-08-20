@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_WINDRES=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/3] Compiling the execution manifest...
%INPUTWEAVER_WINDRES% -DUNICODE -D_UNICODE "res\InputWeaver.rc" -O coff -o "bin\InputWeaver.res.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Building InputWeaver.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -municode ^
    "src\main.cpp" ^
    "src\app\runtime.cpp" ^
    "src\app\remap_engine.cpp" ^
    "src\app\action_scheduler.cpp" ^
    "src\core\fixed_rules.cpp" ^
    "src\platform\windows\low_level_hooks.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    "bin\InputWeaver.res.o" ^
    -o "bin\InputWeaver.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Building InputWeaverTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime_tests.cpp" ^
    "src\app\runtime.cpp" ^
    "src\app\remap_engine.cpp" ^
    "src\app\action_scheduler.cpp" ^
    "src\core\fixed_rules.cpp" ^
    "src\platform\windows\low_level_hooks.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    -o "bin\InputWeaverTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo Build completed successfully.
exit /b 0

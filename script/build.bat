@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_WINDRES=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/4] Compiling the execution manifest...
%INPUTWEAVER_WINDRES% -DUNICODE -D_UNICODE "res\InputWeaver.rc" -O coff -o "bin\InputWeaver.res.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/4] Building InputWeaver.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -municode ^
    "src\platform\windows\main.cpp" ^
    "src\platform\windows\runtime_session.cpp" ^
    "src\platform\windows\remap_engine.cpp" ^
    "src\platform\windows\action_scheduler.cpp" ^
    "src\runtime\fixed_rules.cpp" ^
    "src\platform\windows\low_level_hooks.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    "bin\InputWeaver.res.o" ^
    -o "bin\InputWeaver.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Building InputWeaverTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\runtime_tests.cpp" ^
    "src\platform\windows\runtime_session.cpp" ^
    "src\platform\windows\remap_engine.cpp" ^
    "src\platform\windows\action_scheduler.cpp" ^
    "src\runtime\fixed_rules.cpp" ^
    "src\platform\windows\low_level_hooks.cpp" ^
    "src\platform\windows\input_injector.cpp" ^
    "src\platform\windows\process_context.cpp" ^
    "src\platform\windows\process_locator.cpp" ^
    "src\diagnostics\diagnostic_log.cpp" ^
    -o "bin\InputWeaverTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Building CompiledProgramTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\program\compiled_program_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\CompiledProgramTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo Build completed successfully.
exit /b 0

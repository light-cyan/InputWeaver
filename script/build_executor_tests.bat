@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/2] Building WindowsPlatformTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\windows_platform_tests.cpp" ^
    "src\platform\windows\runtime\input_injector.cpp" ^
    "src\platform\windows\runtime\process_context.cpp" ^
    "src\platform\windows\runtime\process_locator.cpp" ^
    "src\platform\windows\diagnostics\diagnostic_log.cpp" ^
    -o "bin\WindowsPlatformTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building CompiledProgramTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\program\compiled_program_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\CompiledProgramTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo Executor and program test build completed successfully.
exit /b 0

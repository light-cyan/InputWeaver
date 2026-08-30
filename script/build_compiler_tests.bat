@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc -Itests/program"

echo Building CompilerTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\compiler\compiler_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
    "src\language\lexer.cpp" ^
    "src\compiler\compiler.cpp" ^
    "src\compiler\source.cpp" ^
    "src\compiler\frontend.cpp" ^
    "src\compiler\control_catalog.cpp" ^
    "src\compiler\semantics.cpp" ^
    "src\compiler\lowering.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\CompilerTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo Compiler test build completed successfully.
exit /b 0

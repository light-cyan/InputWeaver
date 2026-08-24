@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc -Itests/program"

echo [1/2] Building InputWeaverCompiler.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "src\platform\windows\cli\compiler_main.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
    "src\ui\cli\compiler_cli.cpp" ^
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
    -municode ^
    -o "bin\InputWeaverCompiler.exe"
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building CompilerTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\compiler\compiler_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
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

echo Compiler build completed successfully.
exit /b 0

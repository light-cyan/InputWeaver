@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_RC=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"
set "INPUTWEAVER_PRODUCT_LINK=-static-libgcc -static-libstdc++"

echo [1/2] Compiling the InputWeaver compiler resources...
%INPUTWEAVER_RC% -Ires -O coff ^
    "res\InputWeaverCompiler.rc" ^
    -o "bin\InputWeaverCompilerResource.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building InputWeaverCompiler.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "src\platform\windows\ui\cli\compiler_main.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
    "src\ui\cli\compiler_cli.cpp" ^
    "src\language\lexer.cpp" ^
    "src\compiler\compiler.cpp" ^
    "src\compiler\source.cpp" ^
    "src\compiler\frontend.cpp" ^
    "src\compiler\control_catalog.cpp" ^
    "src\compiler\semantics.cpp" ^
    "src\compiler\lowering.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\program_code_validator.cpp" ^
    "src\program\program_requirements.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\weavec_codec.cpp" ^
    "bin\InputWeaverCompilerResource.o" ^
    -municode ^
    %INPUTWEAVER_PRODUCT_LINK% ^
    -o "bin\InputWeaverCompiler.exe"
if errorlevel 1 exit /b %errorlevel%

echo Compiler build completed successfully.
exit /b 0

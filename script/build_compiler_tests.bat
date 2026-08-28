@echo off
setlocal
cd /d "%~dp0.."

set "INPUTWEAVER_PRODUCTS_ONLY="
if "%~1"=="" goto arguments_done
if /i not "%~1"=="--products-only" goto usage
if not "%~2"=="" goto usage
set "INPUTWEAVER_PRODUCTS_ONLY=1"
:arguments_done

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc -Itests/program"
set "INPUTWEAVER_PRODUCT_LINK=-static-libgcc -static-libstdc++"
set "INPUTWEAVER_BUILD_STEPS=2"
if defined INPUTWEAVER_PRODUCTS_ONLY set "INPUTWEAVER_BUILD_STEPS=1"

echo [1/%INPUTWEAVER_BUILD_STEPS%] Building InputWeaverCompiler.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "src\platform\windows\cli\compiler_main.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
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
    %INPUTWEAVER_PRODUCT_LINK% ^
    -o "bin\InputWeaverCompiler.exe"
if errorlevel 1 exit /b %errorlevel%

if defined INPUTWEAVER_PRODUCTS_ONLY (
    echo Compiler product build completed successfully.
    exit /b 0
)

echo [2/2] Building CompilerTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\compiler\compiler_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\platform\windows\compiler\artifact_file.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
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

:usage
echo Usage: build_compiler_tests.bat [--products-only]
exit /b 2

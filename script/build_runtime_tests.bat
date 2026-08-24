@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/2] Building ProgramRuntimeTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\program_runtime_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\runtime\artifact_loader.cpp" ^
    "src\runtime\expression_vm.cpp" ^
    "src\runtime\program_runtime.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\ProgramRuntimeTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building WindowsRuntimeAdapterTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -DINPUTWEAVER_TESTING ^
    "tests\runtime\windows_runtime_adapter_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\platform\windows\runtime\compiled_target_resolver.cpp" ^
    "src\runtime\expression_vm.cpp" ^
    "src\runtime\program_runtime.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\platform\windows\runtime\runtime_control_catalog.cpp" ^
    "src\platform\windows\runtime\runtime_process_launcher.cpp" ^
    "src\platform\windows\runtime\runtime_route_adapter.cpp" ^
    "src\platform\windows\runtime\input_injector.cpp" ^
    "src\platform\windows\runtime\process_context.cpp" ^
    -o "bin\WindowsRuntimeAdapterTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo Runtime test build completed successfully.
exit /b 0

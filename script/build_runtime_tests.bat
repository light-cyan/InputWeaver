@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/5] Building ProgramRuntimeTests.exe...
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

echo [2/5] Building WindowsRuntimeAdapterTests.exe...
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

echo [3/5] Building DebugProtocolTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\debug\debug_protocol_tests.cpp" ^
    "src\debug\debug_protocol.cpp" ^
    -o "bin\DebugProtocolTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [4/5] Building WindowsDebugServerTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\debug\windows_debug_server_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\platform\windows\debug\debug_server.cpp" ^
    "src\debug\debug_protocol.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    -o "bin\WindowsDebugServerTests.exe" ^
    -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [5/5] Building RuntimeCliTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\runtime_cli_tests.cpp" ^
    "src\ui\cli\runtime_cli.cpp" ^
    -o "bin\RuntimeCliTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo Runtime test build completed successfully.
exit /b 0

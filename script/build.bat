@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_WINDRES=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/5] Compiling the execution manifest...
%INPUTWEAVER_WINDRES% -DUNICODE -D_UNICODE "res\InputWeaver.rc" -O coff -o "bin\InputWeaver.res.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/5] Building InputWeaver.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -municode ^
    "src\platform\windows\cli\runtime_main.cpp" ^
    "src\ui\cli\runtime_cli.cpp" ^
    "src\platform\windows\runtime\windows_executor.cpp" ^
    "src\platform\windows\runtime\compiled_target_resolver.cpp" ^
    "src\platform\windows\runtime\program_runtime_session.cpp" ^
    "src\platform\windows\runtime\low_level_hooks.cpp" ^
    "src\platform\windows\runtime\input_injector.cpp" ^
    "src\platform\windows\runtime\process_context.cpp" ^
    "src\platform\windows\runtime\process_locator.cpp" ^
    "src\platform\windows\runtime\runtime_control_catalog.cpp" ^
    "src\platform\windows\runtime\runtime_process_launcher.cpp" ^
    "src\platform\windows\runtime\runtime_route_adapter.cpp" ^
    "src\platform\windows\debug\debug_server.cpp" ^
    "src\platform\windows\diagnostics\diagnostic_log.cpp" ^
    "src\debug\debug_protocol.cpp" ^
    "src\runtime\artifact_loader.cpp" ^
    "src\runtime\expression_vm.cpp" ^
    "src\runtime\program_runtime.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    "bin\InputWeaver.res.o" ^
    -o "bin\InputWeaver.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/5] Building WindowsPlatformTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\windows_platform_tests.cpp" ^
    "src\platform\windows\runtime\input_injector.cpp" ^
    "src\platform\windows\runtime\process_context.cpp" ^
    "src\platform\windows\runtime\process_locator.cpp" ^
    "src\platform\windows\diagnostics\diagnostic_log.cpp" ^
    -o "bin\WindowsPlatformTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [4/5] Building CompiledProgramTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\program\compiled_program_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\CompiledProgramTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [5/5] Building the focused runtime tests...
call script\build_runtime_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo Build completed successfully.
exit /b 0

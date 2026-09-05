@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_RC=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"
set "INPUTWEAVER_PRODUCT_LINK=-static-libgcc -static-libstdc++"

echo [1/2] Compiling the InputWeaver executable resources...
%INPUTWEAVER_RC% -Ires -O coff ^
    "res\InputWeaver.rc" ^
    -o "bin\InputWeaverResource.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/2] Building InputWeaver.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -municode ^
    "src\platform\windows\ui\cli\runtime_main.cpp" ^
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
    "src\runtime\array_storage.cpp" ^
    "src\runtime\expression_vm.cpp" ^
    "src\runtime\mouse_state.cpp" ^
    "src\runtime\mouse_fields.cpp" ^
    "src\runtime\program_runtime_mouse.cpp" ^
    "src\runtime\program_runtime.cpp" ^
    "src\runtime\program_runtime_activation.cpp" ^
    "src\runtime\program_runtime_dispatch.cpp" ^
    "src\runtime\program_runtime_output.cpp" ^
    "src\runtime\program_runtime_tasks.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\program_code_validator.cpp" ^
    "src\program\program_requirements.cpp" ^
    "src\program\weavec_codec.cpp" ^
    "bin\InputWeaverResource.o" ^
    -o "bin\InputWeaver.exe" ^
    %INPUTWEAVER_PRODUCT_LINK% ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo Executor build completed successfully.
exit /b 0

@echo off
setlocal
cd /d "%~dp0.."

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_ANALYZE=-std=c++20 -O0 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -fanalyzer -fsyntax-only -DUNICODE -D_UNICODE -Isrc"

for %%F in (
    "src\runtime\artifact_loader.cpp"
    "src\runtime\expression_vm.cpp"
    "src\runtime\program_runtime.cpp"
    "src\ui\cli\runtime_cli.cpp"
    "src\platform\windows\cli\runtime_main.cpp"
    "src\platform\windows\runtime\windows_executor.cpp"
    "src\platform\windows\diagnostics\diagnostic_log.cpp"
    "src\platform\windows\runtime\compiled_target_resolver.cpp"
    "src\platform\windows\runtime\runtime_control_catalog.cpp"
    "src\platform\windows\runtime\runtime_process_launcher.cpp"
    "src\platform\windows\runtime\runtime_route_adapter.cpp"
    "src\platform\windows\runtime\input_injector.cpp"
    "src\platform\windows\runtime\program_runtime_session.cpp"
) do (
    echo Analyzing %%~F...
    %INPUTWEAVER_CXX% %INPUTWEAVER_ANALYZE% "%%~F"
    if errorlevel 1 exit /b %errorlevel%
)

echo Runtime static analysis completed successfully.
exit /b 0

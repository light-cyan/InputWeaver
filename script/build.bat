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
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"
set "INPUTWEAVER_PRODUCT_LINK=-static-libgcc -static-libstdc++"
set "INPUTWEAVER_BUILD_STEPS=6"
if defined INPUTWEAVER_PRODUCTS_ONLY set "INPUTWEAVER_BUILD_STEPS=2"

echo [1/%INPUTWEAVER_BUILD_STEPS%] Building InputWeaver.exe...
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
    -o "bin\InputWeaver.exe" ^
    %INPUTWEAVER_PRODUCT_LINK% ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

if defined INPUTWEAVER_PRODUCTS_ONLY (
    echo [2/2] Building InputWeaverTUI.exe...
    call script\build_tui.bat
    if errorlevel 1 exit /b %errorlevel%
    echo Product build completed successfully.
    exit /b 0
)

echo [2/6] Building WindowsPlatformTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\runtime\windows_platform_tests.cpp" ^
    "src\platform\windows\runtime\input_injector.cpp" ^
    "src\platform\windows\runtime\process_context.cpp" ^
    "src\platform\windows\runtime\process_locator.cpp" ^
    "src\platform\windows\diagnostics\diagnostic_log.cpp" ^
    -o "bin\WindowsPlatformTests.exe" ^
    -luser32 -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/6] Building CompiledProgramTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\program\compiled_program_tests.cpp" ^
    "tests\program\compiled_program_fixtures.cpp" ^
    "src\program\compiled_program.cpp" ^
    "src\program\program_dump.cpp" ^
    "src\program\program_validator.cpp" ^
    "src\program\weavec_codec.cpp" ^
    -o "bin\CompiledProgramTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [4/6] Building InputWeaverTUI.exe...
call script\build_tui.bat
if errorlevel 1 exit /b %errorlevel%

echo [5/6] Building the App and TUI tests...
call script\build_app_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo [6/6] Building the focused runtime tests...
call script\build_runtime_tests.bat
if errorlevel 1 exit /b %errorlevel%

echo Build completed successfully.
exit /b 0

:usage
echo Usage: build.bat [--products-only]
exit /b 2

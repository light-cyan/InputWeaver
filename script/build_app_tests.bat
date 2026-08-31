@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/4] Building ApplicationTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\app\application_tests.cpp" ^
    "src\app\application.cpp" ^
    "src\app\entry_codec.cpp" ^
    -o "bin\ApplicationTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [2/4] Building TuiTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\ui\tui_tests.cpp" ^
    "src\app\application.cpp" ^
    "src\app\entry_codec.cpp" ^
    "src\ui\tui\tui_controller.cpp" ^
    "src\ui\tui\tui_renderer.cpp" ^
    "src\ui\tui\support\canvas.cpp" ^
    "src\ui\tui\support\color_scheme.cpp" ^
    "src\ui\tui\support\interaction.cpp" ^
    "src\ui\tui\support\source_editor.cpp" ^
    "src\ui\tui\support\source_highlighter.cpp" ^
    "src\ui\tui\support\text_layout.cpp" ^
    "src\language\lexer.cpp" ^
    "src\debug\debug_client.cpp" ^
    -o "bin\TuiTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Building WindowsAppPlatformTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\app\windows_app_platform_tests.cpp" ^
    "src\platform\windows\app\program_library.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
    "src\platform\windows\support\command_line.cpp" ^
    "src\platform\windows\support\text_encoding.cpp" ^
    "src\app\entry_codec.cpp" ^
    -o "bin\WindowsAppPlatformTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Building WindowsTuiIpcTests.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% ^
    "tests\ui\windows_tui_ipc_tests.cpp" ^
    "src\platform\windows\ui\tui\tui_ipc.cpp" ^
    "src\ui\tui\support\canvas.cpp" ^
    "src\ui\tui\support\text_layout.cpp" ^
    -o "bin\WindowsTuiIpcTests.exe"
if errorlevel 1 exit /b %errorlevel%

echo App and TUI test build completed successfully.
exit /b 0

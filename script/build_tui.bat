@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"
if not exist "bin\res" mkdir "bin\res"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_WINDRES=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"

echo [1/3] Compiling the TUI manifest...
%INPUTWEAVER_WINDRES% -DUNICODE -D_UNICODE "res\InputWeaverTUI.rc" -O coff -o "bin\InputWeaverTUI.res.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/3] Building InputWeaverTUI.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -municode ^
    "src\platform\windows\tui\tui_main.cpp" ^
    "src\platform\windows\tui\windows_terminal.cpp" ^
    "src\platform\windows\app\windows_app_platform.cpp" ^
    "src\platform\windows\app\program_library.cpp" ^
    "src\platform\windows\app\child_process.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
    "src\platform\windows\support\command_line.cpp" ^
    "src\platform\windows\support\text_encoding.cpp" ^
    "src\platform\windows\debug\debug_client.cpp" ^
    "src\app\application.cpp" ^
    "src\app\entry_codec.cpp" ^
    "src\ui\tui\tui_controller.cpp" ^
    "src\ui\tui\tui_renderer.cpp" ^
    "src\ui\tui\support\canvas.cpp" ^
    "src\ui\tui\support\color_scheme.cpp" ^
    "src\ui\tui\support\interaction.cpp" ^
    "src\ui\tui\support\text_layout.cpp" ^
    "src\debug\debug_client.cpp" ^
    "src\debug\debug_protocol.cpp" ^
    "bin\InputWeaverTUI.res.o" ^
    -o "bin\InputWeaverTUI.exe" ^
    -ladvapi32
if errorlevel 1 exit /b %errorlevel%

echo [3/3] Copying the TUI color scheme...
copy /y "res\InputWeaverTUI.colors.json" "bin\res\InputWeaverTUI.colors.json" >nul
if errorlevel 1 exit /b %errorlevel%

echo TUI build completed successfully.
exit /b 0

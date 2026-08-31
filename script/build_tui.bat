@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"
if not exist "bin\res" mkdir "bin\res"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_RC=windres"
set "INPUTWEAVER_COMMON=-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -DUNICODE -D_UNICODE -Isrc"
set "INPUTWEAVER_PRODUCT_LINK=-static-libgcc -static-libstdc++"

echo [1/4] Compiling the InputWeaver TUI resources...
%INPUTWEAVER_RC% -Isrc -Ires -O coff ^
    "res\InputWeaverTUI.rc" ^
    -o "bin\InputWeaverTUIResource.o"
if errorlevel 1 exit /b %errorlevel%

echo [2/4] Building InputWeaverTUI.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -mwindows -municode ^
    "src\platform\windows\ui\tui\tui_frontend_main.cpp" ^
    "src\platform\windows\ui\tui\tui_ipc.cpp" ^
    "src\platform\windows\ui\tui\windows_tui_window.cpp" ^
    "bin\InputWeaverTUIResource.o" ^
    -o "bin\InputWeaverTUI.exe" ^
    %INPUTWEAVER_PRODUCT_LINK% ^
    -lgdi32 -lshell32 -luser32
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Building InputWeaverHost.exe...
%INPUTWEAVER_CXX% %INPUTWEAVER_COMMON% -mwindows -municode ^
    "src\platform\windows\ui\tui\host_main.cpp" ^
    "src\platform\windows\ui\tui\windows_tray.cpp" ^
    "src\platform\windows\ui\tui\tui_frontend_session.cpp" ^
    "src\platform\windows\ui\tui\tui_ipc.cpp" ^
    "src\platform\windows\ui\tui\tui_resources.cpp" ^
    "src\platform\windows\ui\tui\windows_clipboard.cpp" ^
    "src\platform\windows\app\windows_app_platform.cpp" ^
    "src\platform\windows\app\program_library.cpp" ^
    "src\platform\windows\app\child_process.cpp" ^
    "src\platform\windows\support\atomic_file.cpp" ^
    "src\platform\windows\support\command_line.cpp" ^
    "src\platform\windows\support\text_encoding.cpp" ^
    "src\language\lexer.cpp" ^
    "src\platform\windows\debug\debug_client.cpp" ^
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
    "src\debug\debug_client.cpp" ^
    "src\debug\debug_protocol.cpp" ^
    "bin\InputWeaverTUIResource.o" ^
    -o "bin\InputWeaverHost.exe" ^
    %INPUTWEAVER_PRODUCT_LINK% ^
    -ladvapi32 -lshell32 -luser32
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Copying the TUI color scheme...
copy /y "res\InputWeaverTUI.colors.json" "bin\res\InputWeaverTUI.colors.json" >nul
if errorlevel 1 exit /b %errorlevel%

echo TUI build completed successfully.
exit /b 0

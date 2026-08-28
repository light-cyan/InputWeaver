@echo off
setlocal
cd /d "%~dp0.."

for %%F in (InputWeaver.exe InputWeaverCompiler.exe InputWeaverTUI.exe) do (
    if not exist "bin\%%F" (
        echo bin\%%F is missing. Run script\verify_project.bat first.
        exit /b 2
    )
)
if not exist "bin\res\InputWeaverTUI.colors.json" (
    echo The TUI color scheme is missing. Run script\build_tui.bat first.
    exit /b 2
)

"bin\InputWeaverTUI.exe"
exit /b %errorlevel%

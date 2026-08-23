@echo off
setlocal
cd /d "%~dp0.."

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_ANALYZE=-std=c++20 -O0 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -fanalyzer -fsyntax-only -DUNICODE -D_UNICODE -Isrc"

for %%F in (
    "src\compiler\source.cpp"
    "src\compiler\frontend.cpp"
    "src\compiler\control_catalog.cpp"
    "src\compiler\semantics.cpp"
    "src\compiler\lowering.cpp"
    "src\compiler\compiler.cpp"
    "src\compiler\compiler_cli.cpp"
) do (
    echo Analyzing %%~F...
    %INPUTWEAVER_CXX% %INPUTWEAVER_ANALYZE% "%%~F"
    if errorlevel 1 exit /b %errorlevel%
)

echo Compiler static analysis completed successfully.
exit /b 0

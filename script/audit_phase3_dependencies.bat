@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_DEPENDENCIES=bin\phase3-dependencies.tmp"
set "INPUTWEAVER_CPP=-std=c++20 -DUNICODE -D_UNICODE -Isrc -MM"

for %%F in (
    "src\compiler\source.cpp"
    "src\compiler\frontend.cpp"
    "src\compiler\control_catalog.cpp"
    "src\compiler\semantics.cpp"
    "src\compiler\lowering.cpp"
    "src\compiler\compiler.cpp"
    "src\compiler\compiler_cli.cpp"
) do (
    call :check_compiler "%%~F"
    if errorlevel 1 exit /b 1
)

for %%F in (
    "src\runtime\artifact_loader.cpp"
    "src\runtime\expression_vm.cpp"
    "src\runtime\program_runtime.cpp"
) do (
    call :check_runtime "%%~F"
    if errorlevel 1 exit /b 1
)

for %%F in (
    "src\platform\windows\runtime_control_catalog.cpp"
    "src\platform\windows\runtime_process_launcher.cpp"
    "src\platform\windows\runtime_route_adapter.cpp"
    "src\platform\windows\input_injector.cpp"
    "src\platform\windows\program_runtime_session.cpp"
) do (
    call :check_windows_runtime "%%~F"
    if errorlevel 1 exit /b 1
)

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" "src\runtime\*.cpp" "src\runtime\*.hpp" >nul
if not errorlevel 1 (
    echo Runtime core contains a native Windows include.
    exit /b 1
)

del /q "%INPUTWEAVER_DEPENDENCIES%" >nul 2>nul
echo Phase 3 dependency audit completed successfully.
exit /b 0

:generate_dependencies
%INPUTWEAVER_CXX% %INPUTWEAVER_CPP% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
exit /b 0

:check_compiler
call :generate_dependencies "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\runtime" /c:"src/runtime" /c:"src\platform" /c:"src/platform" /c:"src\app" /c:"src/app" /c:"src\tui" /c:"src/tui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Compiler dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_runtime
call :generate_dependencies "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\platform" /c:"src/platform" /c:"src\app" /c:"src/app" /c:"src\tui" /c:"src/tui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_runtime
call :generate_dependencies "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\app" /c:"src/app" /c:"src\tui" /c:"src/tui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

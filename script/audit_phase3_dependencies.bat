@echo off
setlocal
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_DEPENDENCIES=bin\dependencies.tmp"
set "INPUTWEAVER_CPP=-std=c++20 -DUNICODE -D_UNICODE -Isrc -MM"
set "INPUTWEAVER_GENERATE=%INPUTWEAVER_CXX% %INPUTWEAVER_CPP%"

for %%F in (
    "src\compiler\source.cpp"
    "src\compiler\frontend.cpp"
    "src\compiler\control_catalog.cpp"
    "src\compiler\semantics.cpp"
    "src\compiler\lowering.cpp"
    "src\compiler\compiler.cpp"
) do (
    call :check_compiler "%%~F"
    if errorlevel 1 exit /b 1
)

call :check_windows_compiler "src\platform\windows\compiler\artifact_file.cpp"
if errorlevel 1 exit /b 1

for %%F in (
    "src\runtime\artifact_loader.cpp"
    "src\runtime\expression_vm.cpp"
    "src\runtime\program_runtime.cpp"
) do (
    call :check_runtime "%%~F"
    if errorlevel 1 exit /b 1
)

call :check_windows_diagnostics "src\platform\windows\diagnostics\diagnostic_log.cpp"
if errorlevel 1 exit /b 1

for %%F in (
    "src\platform\windows\runtime\windows_executor.cpp"
    "src\platform\windows\runtime\compiled_target_resolver.cpp"
    "src\platform\windows\runtime\low_level_hooks.cpp"
    "src\platform\windows\runtime\process_context.cpp"
    "src\platform\windows\runtime\process_locator.cpp"
    "src\platform\windows\runtime\runtime_control_catalog.cpp"
    "src\platform\windows\runtime\runtime_process_launcher.cpp"
    "src\platform\windows\runtime\runtime_route_adapter.cpp"
    "src\platform\windows\runtime\input_injector.cpp"
    "src\platform\windows\runtime\program_runtime_session.cpp"
) do (
    call :check_windows_runtime "%%~F"
    if errorlevel 1 exit /b 1
)

call :check_compiler_cli "src\ui\cli\compiler_cli.cpp"
if errorlevel 1 exit /b 1
call :check_runtime_cli "src\ui\cli\runtime_cli.cpp"
if errorlevel 1 exit /b 1
call :check_windows_compiler_cli "src\platform\windows\cli\compiler_main.cpp"
if errorlevel 1 exit /b 1
call :check_windows_runtime_cli "src\platform\windows\cli\runtime_main.cpp"
if errorlevel 1 exit /b 1

dir /b /a-d "src\platform\windows\*" >nul 2>nul
if not errorlevel 1 (
    echo Windows platform files must be owned by a second-level module directory.
    exit /b 1
)

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" "src\runtime\*.cpp" "src\runtime\*.hpp" >nul
if not errorlevel 1 (
    echo Runtime core contains a native Windows include.
    exit /b 1
)

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" "src\compiler\*.cpp" "src\compiler\*.hpp" >nul
if not errorlevel 1 (
    echo Compiler core contains a native Windows include.
    exit /b 1
)

if exist "src\ui" (
    findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*platform[/\\]" "src\ui\*.cpp" "src\ui\*.hpp" >nul 2>nul
    if not errorlevel 1 (
        echo UI code contains a platform-specific dependency.
        exit /b 1
    )
)

del /q "%INPUTWEAVER_DEPENDENCIES%" >nul 2>nul
echo Dependency audit completed successfully.
exit /b 0

:check_compiler
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\runtime" /c:"src/runtime" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Compiler dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_runtime
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_compiler
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\runtime" /c:"src/runtime" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows compiler dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_diagnostics
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" /c:"src\platform\windows\compiler" /c:"src/platform/windows/compiler" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows diagnostics dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_runtime
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_compiler_cli
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\runtime" /c:"src/runtime" /c:"src\platform" /c:"src/platform" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Compiler CLI dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_runtime_cli
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" /c:"src\platform" /c:"src/platform" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Runtime CLI dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_compiler_cli
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows compiler CLI adapter dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_runtime_cli
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows runtime CLI adapter dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

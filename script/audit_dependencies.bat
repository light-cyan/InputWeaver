@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_DEPENDENCIES=bin\dependencies.tmp"
set "INPUTWEAVER_CPP=-std=c++20 -DUNICODE -D_UNICODE -Isrc -MM"
set "INPUTWEAVER_GENERATE=%INPUTWEAVER_CXX% %INPUTWEAVER_CPP%"
set /a INPUTWEAVER_AUDITED=0

for /f "delims=" %%F in ('git ls-files --cached --others --exclude-standard "src/*.cpp"') do (
    call :classify_and_check "%%F"
    if errorlevel 1 exit /b 1
)

if !INPUTWEAVER_AUDITED! equ 0 (
    echo No source implementation was classified for dependency audit.
    exit /b 1
)

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

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" "src\program\*.cpp" "src\program\*.hpp" >nul
if not errorlevel 1 (
    echo Shared program code contains a native Windows include.
    exit /b 1
)

findstr /s /i /c:"windows" /c:"virtualkey" /c:"scancode" /c:"hookflags" /c:"mousedata" /c:"extrainfo" /c:"VK_" "src\input\*.cpp" "src\input\*.hpp" >nul 2>nul
if not errorlevel 1 (
    echo Platform-neutral input code encodes a Windows-native identity.
    exit /b 1
)

if exist "src\ui" (
    findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*platform[/\]" "src\ui\*.cpp" "src\ui\*.hpp" >nul 2>nul
    if not errorlevel 1 (
        echo UI code contains a platform-specific dependency.
        exit /b 1
    )
)

del /q "%INPUTWEAVER_DEPENDENCIES%" >nul 2>nul
echo Dependency audit completed successfully for !INPUTWEAVER_AUDITED! source implementations.
exit /b 0

:classify_and_check
set "INPUTWEAVER_FILE=%~1"
set "INPUTWEAVER_OWNER="
if /i "!INPUTWEAVER_FILE:~0,12!"=="src/program/" set "INPUTWEAVER_OWNER=program"
if /i "!INPUTWEAVER_FILE:~0,13!"=="src/compiler/" set "INPUTWEAVER_OWNER=compiler"
if /i "!INPUTWEAVER_FILE:~0,12!"=="src/runtime/" set "INPUTWEAVER_OWNER=runtime"
if /i "!INPUTWEAVER_FILE:~0,10!"=="src/debug/" set "INPUTWEAVER_OWNER=debug"
if /i "!INPUTWEAVER_FILE!"=="src/ui/cli/compiler_cli.cpp" set "INPUTWEAVER_OWNER=compiler_cli"
if /i "!INPUTWEAVER_FILE!"=="src/ui/cli/runtime_cli.cpp" set "INPUTWEAVER_OWNER=runtime_cli"
if /i "!INPUTWEAVER_FILE!"=="src/platform/windows/cli/compiler_main.cpp" set "INPUTWEAVER_OWNER=windows_compiler_cli"
if /i "!INPUTWEAVER_FILE!"=="src/platform/windows/cli/runtime_main.cpp" set "INPUTWEAVER_OWNER=windows_runtime_cli"
if /i "!INPUTWEAVER_FILE:~0,30!"=="src/platform/windows/compiler/" set "INPUTWEAVER_OWNER=windows_compiler"
if /i "!INPUTWEAVER_FILE:~0,33!"=="src/platform/windows/diagnostics/" set "INPUTWEAVER_OWNER=windows_diagnostics"
if /i "!INPUTWEAVER_FILE:~0,27!"=="src/platform/windows/debug/" set "INPUTWEAVER_OWNER=windows_debug"
if /i "!INPUTWEAVER_FILE:~0,29!"=="src/platform/windows/runtime/" set "INPUTWEAVER_OWNER=windows_runtime"
if not defined INPUTWEAVER_OWNER (
    echo Unclassified tracked implementation: !INPUTWEAVER_FILE!
    exit /b 1
)
call :check_!INPUTWEAVER_OWNER! "!INPUTWEAVER_FILE!"
if errorlevel 1 exit /b 1
set /a INPUTWEAVER_AUDITED+=1
exit /b 0

:generate
%INPUTWEAVER_GENERATE% "%~1" > "%INPUTWEAVER_DEPENDENCIES%"
if errorlevel 1 (
    echo Unable to generate dependencies for %~1.
    exit /b 1
)
exit /b 0

:check_program
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\input" /c:"src/input" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Shared program dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_compiler
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\input" /c:"src/input" /c:"src\runtime" /c:"src/runtime" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Compiler dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_runtime
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_debug
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\platform" /c:"src/platform" /c:"src\ui" /c:"src/ui" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Debug protocol dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_compiler
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\input" /c:"src/input" /c:"src\runtime" /c:"src/runtime" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\diagnostics" /c:"src/platform/windows/diagnostics" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows compiler dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_diagnostics
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" /c:"src\platform\windows\compiler" /c:"src/platform/windows/compiler" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows diagnostics dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_debug
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" /c:"src\platform\windows\compiler" /c:"src/platform/windows/compiler" /c:"src\platform\windows\diagnostics" /c:"src/platform/windows/diagnostics" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows debug dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_runtime
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\ui" /c:"src/ui" /c:"src\platform\windows\cli" /c:"src/platform/windows/cli" /c:"src\platform\windows\compiler" /c:"src/platform/windows/compiler" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows runtime dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_compiler_cli
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\input" /c:"src/input" /c:"src\runtime" /c:"src/runtime" /c:"src\platform" /c:"src/platform" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Compiler CLI dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_runtime_cli
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" /c:"src\platform" /c:"src/platform" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Runtime CLI dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_compiler_cli
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" /c:"src\platform\windows\runtime" /c:"src/platform/windows/runtime" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows compiler CLI adapter dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

:check_windows_runtime_cli
call :generate "%~1"
if errorlevel 1 exit /b 1
findstr /i /c:"src\compiler" /c:"src/compiler" /c:"src\runtime" /c:"src/runtime" /c:"src\program" /c:"src/program" /c:"src\input" /c:"src/input" "%INPUTWEAVER_DEPENDENCIES%" >nul
if not errorlevel 1 (
    echo Windows runtime CLI adapter dependency boundary failed for %~1.
    exit /b 1
)
exit /b 0

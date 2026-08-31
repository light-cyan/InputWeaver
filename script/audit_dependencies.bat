@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."

if not exist "bin" mkdir "bin"

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_DEPENDENCIES=bin\dependencies.tmp"
set "INPUTWEAVER_CPP=-std=c++20 -DUNICODE -D_UNICODE -Isrc -MM"
set "INPUTWEAVER_GENERATE=%INPUTWEAVER_CXX% %INPUTWEAVER_CPP%"
set /a INPUTWEAVER_AUDITED=0
set "INPUTWEAVER_ALLOWED_language=language"
set "INPUTWEAVER_ALLOWED_program=program support"
set "INPUTWEAVER_ALLOWED_compiler=compiler language program support"
set "INPUTWEAVER_ALLOWED_runtime=runtime program input support"
set "INPUTWEAVER_ALLOWED_debug=debug runtime program input support"
set "INPUTWEAVER_ALLOWED_app=app debug runtime program input support"
set "INPUTWEAVER_ALLOWED_ui_tui=ui_tui language app debug runtime program input support"
set "INPUTWEAVER_ALLOWED_windows_support=windows_support"
set "INPUTWEAVER_ALLOWED_windows_app=windows_app app debug runtime program input support windows_debug windows_support"
set "INPUTWEAVER_ALLOWED_windows_tui=windows_tui ui_tui language app debug runtime program input support windows_app windows_debug windows_support"
set "INPUTWEAVER_ALLOWED_windows_compiler=windows_compiler compiler language program support windows_support"
set "INPUTWEAVER_ALLOWED_windows_diagnostics=windows_diagnostics input runtime program support windows_support"
set "INPUTWEAVER_ALLOWED_windows_debug=windows_debug debug input program runtime support windows_support"
set "INPUTWEAVER_ALLOWED_windows_runtime=windows_runtime runtime program input support debug windows_debug windows_diagnostics windows_support"
set "INPUTWEAVER_ALLOWED_compiler_cli=compiler_cli compiler language program support"
set "INPUTWEAVER_ALLOWED_runtime_cli=runtime_cli"
set "INPUTWEAVER_ALLOWED_windows_compiler_cli=windows_compiler_cli compiler_cli"
set "INPUTWEAVER_ALLOWED_windows_runtime_cli=windows_runtime_cli runtime_cli windows_runtime"

for /f "delims=" %%F in ('git ls-files --cached --others --exclude-standard "src/*.cpp"') do (
    if exist "%%F" (
        call :classify_and_check "%%F"
        if errorlevel 1 exit /b 1
    )
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

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" /c:"#.*include.*app[/\]" /c:"#.*include.*compiler[/\]" /c:"#.*include.*debug[/\]" /c:"#.*include.*input[/\]" /c:"#.*include.*language[/\]" /c:"#.*include.*platform[/\]" /c:"#.*include.*program[/\]" /c:"#.*include.*runtime[/\]" /c:"#.*include.*ui[/\]" "src\support\*.cpp" "src\support\*.hpp" >nul 2>nul
if not errorlevel 1 (
    echo Platform-independent support dependency boundary failed.
    exit /b 1
)

findstr /s /i /r /c:"#.*include.*app[/\]" /c:"#.*include.*compiler[/\]" /c:"#.*include.*debug[/\]" /c:"#.*include.*input[/\]" /c:"#.*include.*platform[/\]" /c:"#.*include.*program[/\]" /c:"#.*include.*runtime[/\]" /c:"#.*include.*support[/\]" /c:"#.*include.*ui[/\]" "src\language\*.cpp" "src\language\*.hpp" >nul 2>nul
if not errorlevel 1 (
    echo Language dependency boundary failed.
    exit /b 1
)

findstr /s /i /r /c:"#.*include.*app[/\]" /c:"#.*include.*compiler[/\]" /c:"#.*include.*debug[/\]" /c:"#.*include.*diagnostics[/\]" /c:"#.*include.*input[/\]" /c:"#.*include.*language[/\]" /c:"#.*include.*program[/\]" /c:"#.*include.*runtime[/\]" /c:"#.*include.*ui[/\]" "src\platform\windows\support\*.cpp" "src\platform\windows\support\*.hpp" >nul 2>nul
if not errorlevel 1 (
    echo Windows support depends on a product module.
    exit /b 1
)

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" "src\language\*.cpp" "src\language\*.hpp" "src\compiler\*.cpp" "src\compiler\*.hpp" "src\program\*.cpp" "src\program\*.hpp" "src\runtime\*.cpp" "src\runtime\*.hpp" "src\debug\*.cpp" "src\debug\*.hpp" >nul
if not errorlevel 1 (
    echo Platform-independent core contains a native Windows include.
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

findstr /s /i /r /c:"#.*include.*windows" /c:"#.*include.*winuser" /c:"#.*include.*processthreadsapi" /c:"#.*include.*platform[/\]" /c:"#.*include.*ui[/\]" "src\app\*.cpp" "src\app\*.hpp" >nul 2>nul
if not errorlevel 1 (
    echo App code contains a platform or UI dependency.
    exit /b 1
)

del /q "%INPUTWEAVER_DEPENDENCIES%" >nul 2>nul
echo Dependency audit completed successfully for !INPUTWEAVER_AUDITED! source implementations.
exit /b 0

:classify_and_check
set "INPUTWEAVER_FILE=%~1"
call :classify_path "!INPUTWEAVER_FILE!"
set "INPUTWEAVER_OWNER=!INPUTWEAVER_CLASSIFIED_OWNER!"
if not defined INPUTWEAVER_OWNER (
    echo Unclassified tracked implementation: !INPUTWEAVER_FILE!
    exit /b 1
)
call set "INPUTWEAVER_ALLOWED=%%INPUTWEAVER_ALLOWED_!INPUTWEAVER_OWNER!%%"
if not defined INPUTWEAVER_ALLOWED (
    echo Missing dependency policy for !INPUTWEAVER_OWNER!.
    exit /b 1
)
call :check_dependencies "!INPUTWEAVER_FILE!"
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

:check_dependencies
call :generate "%~1"
if errorlevel 1 exit /b 1
for /f "usebackq tokens=*" %%L in ("%INPUTWEAVER_DEPENDENCIES%") do (
    call :check_dependency_line %%L
    if errorlevel 1 exit /b 1
)
exit /b 0

:check_dependency_line
for %%D in (%*) do (
    set "INPUTWEAVER_DEPENDENCY=%%~D"
    if /i "!INPUTWEAVER_DEPENDENCY:~0,4!"=="src/" (
        call :classify_path "!INPUTWEAVER_DEPENDENCY!"
        if not defined INPUTWEAVER_CLASSIFIED_OWNER (
            echo !INPUTWEAVER_OWNER! dependency boundary failed for !INPUTWEAVER_FILE!: unclassified !INPUTWEAVER_DEPENDENCY!.
            exit /b 1
        )
        set "INPUTWEAVER_ALLOWED_MATCH="
        for %%A in (!INPUTWEAVER_ALLOWED!) do (
            if /i "%%A"=="!INPUTWEAVER_CLASSIFIED_OWNER!" set "INPUTWEAVER_ALLOWED_MATCH=1"
        )
        if not defined INPUTWEAVER_ALLOWED_MATCH (
            echo !INPUTWEAVER_OWNER! dependency boundary failed for !INPUTWEAVER_FILE!: !INPUTWEAVER_CLASSIFIED_OWNER! via !INPUTWEAVER_DEPENDENCY!.
            exit /b 1
        )
    )
)
exit /b 0

:classify_path
set "INPUTWEAVER_CLASSIFIED_FILE=%~1"
set "INPUTWEAVER_CLASSIFIED_OWNER="
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,12!"=="src/program/" set "INPUTWEAVER_CLASSIFIED_OWNER=program"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,13!"=="src/language/" set "INPUTWEAVER_CLASSIFIED_OWNER=language"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,13!"=="src/compiler/" set "INPUTWEAVER_CLASSIFIED_OWNER=compiler"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,12!"=="src/runtime/" set "INPUTWEAVER_CLASSIFIED_OWNER=runtime"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,10!"=="src/debug/" set "INPUTWEAVER_CLASSIFIED_OWNER=debug"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,8!"=="src/app/" set "INPUTWEAVER_CLASSIFIED_OWNER=app"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,10!"=="src/input/" set "INPUTWEAVER_CLASSIFIED_OWNER=input"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,12!"=="src/support/" set "INPUTWEAVER_CLASSIFIED_OWNER=support"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/ui/cli/compiler_cli.cpp" set "INPUTWEAVER_CLASSIFIED_OWNER=compiler_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/ui/cli/compiler_cli.hpp" set "INPUTWEAVER_CLASSIFIED_OWNER=compiler_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/ui/cli/runtime_cli.cpp" set "INPUTWEAVER_CLASSIFIED_OWNER=runtime_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/ui/cli/runtime_cli.hpp" set "INPUTWEAVER_CLASSIFIED_OWNER=runtime_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,11!"=="src/ui/tui/" set "INPUTWEAVER_CLASSIFIED_OWNER=ui_tui"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/platform/windows/ui/cli/compiler_main.cpp" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_compiler_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE!"=="src/platform/windows/ui/cli/runtime_main.cpp" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_runtime_cli"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,30!"=="src/platform/windows/compiler/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_compiler"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,33!"=="src/platform/windows/diagnostics/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_diagnostics"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,27!"=="src/platform/windows/debug/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_debug"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,29!"=="src/platform/windows/runtime/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_runtime"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,25!"=="src/platform/windows/app/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_app"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,28!"=="src/platform/windows/ui/tui/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_tui"
if /i "!INPUTWEAVER_CLASSIFIED_FILE:~0,29!"=="src/platform/windows/support/" set "INPUTWEAVER_CLASSIFIED_OWNER=windows_support"
exit /b 0

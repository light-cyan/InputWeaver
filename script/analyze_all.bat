@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."

set "INPUTWEAVER_CXX=g++"
set "INPUTWEAVER_ANALYZE=-std=c++20 -O0 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror -fanalyzer -fsyntax-only -DUNICODE -D_UNICODE -Isrc"
set /a INPUTWEAVER_ANALYZED=0

for /f "delims=" %%F in ('git ls-files --cached --others --exclude-standard "src/*.cpp"') do (
    call :analyze "%%F"
    if errorlevel 1 exit /b 1
)

if !INPUTWEAVER_ANALYZED! equ 0 (
    echo No source implementation was classified for static analysis.
    exit /b 1
)

echo Static analysis completed successfully for !INPUTWEAVER_ANALYZED! source implementations.
exit /b 0

:analyze
set "INPUTWEAVER_FILE=%~1"
set "INPUTWEAVER_OWNER="
if /i "!INPUTWEAVER_FILE:~0,12!"=="src/program/" set "INPUTWEAVER_OWNER=program"
if /i "!INPUTWEAVER_FILE:~0,13!"=="src/compiler/" set "INPUTWEAVER_OWNER=compiler"
if /i "!INPUTWEAVER_FILE:~0,12!"=="src/runtime/" set "INPUTWEAVER_OWNER=runtime"
if /i "!INPUTWEAVER_FILE:~0,10!"=="src/debug/" set "INPUTWEAVER_OWNER=debug"
if /i "!INPUTWEAVER_FILE:~0,11!"=="src/ui/cli/" set "INPUTWEAVER_OWNER=ui-cli"
if /i "!INPUTWEAVER_FILE:~0,25!"=="src/platform/windows/cli/" set "INPUTWEAVER_OWNER=windows-cli"
if /i "!INPUTWEAVER_FILE:~0,30!"=="src/platform/windows/compiler/" set "INPUTWEAVER_OWNER=windows-compiler"
if /i "!INPUTWEAVER_FILE:~0,33!"=="src/platform/windows/diagnostics/" set "INPUTWEAVER_OWNER=windows-diagnostics"
if /i "!INPUTWEAVER_FILE:~0,27!"=="src/platform/windows/debug/" set "INPUTWEAVER_OWNER=windows-debug"
if /i "!INPUTWEAVER_FILE:~0,29!"=="src/platform/windows/runtime/" set "INPUTWEAVER_OWNER=windows-runtime"
if not defined INPUTWEAVER_OWNER (
    echo Unclassified tracked implementation: !INPUTWEAVER_FILE!
    exit /b 1
)
echo Analyzing [!INPUTWEAVER_OWNER!] !INPUTWEAVER_FILE!...
%INPUTWEAVER_CXX% %INPUTWEAVER_ANALYZE% "!INPUTWEAVER_FILE!"
if errorlevel 1 exit /b 1
set /a INPUTWEAVER_ANALYZED+=1
exit /b 0

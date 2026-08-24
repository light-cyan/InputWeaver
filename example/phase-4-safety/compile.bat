@echo off
setlocal
pushd "%~dp0..\.."

if not exist "bin\InputWeaverCompiler.exe" (
    echo InputWeaverCompiler.exe is missing. Run script\build_compiler_tests.bat first.
    popd
    exit /b 2
)

"bin\InputWeaverCompiler.exe" validate "example\phase-4-safety\phase4-safety.weave"
if errorlevel 1 goto compile_failed

"bin\InputWeaverCompiler.exe" compile "example\phase-4-safety\phase4-safety.weave" "example\phase-4-safety\phase4-safety.weavec"
set "INPUTWEAVER_RESULT=%errorlevel%"
popd
exit /b %INPUTWEAVER_RESULT%

:compile_failed
set "INPUTWEAVER_RESULT=%errorlevel%"
popd
exit /b %INPUTWEAVER_RESULT%

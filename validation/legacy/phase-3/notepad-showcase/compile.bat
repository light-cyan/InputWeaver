@echo off
setlocal
pushd "%~dp0..\.."

if not exist "bin\InputWeaverCompiler.exe" (
    echo InputWeaverCompiler.exe is missing. Run script\build_compiler_tests.bat first.
    popd
    exit /b 2
)

"bin\InputWeaverCompiler.exe" compile "example\notepad-showcase\notepad-showcase.weave" "example\notepad-showcase\notepad-showcase.weavec"
set "INPUTWEAVER_RESULT=%errorlevel%"
popd
exit /b %INPUTWEAVER_RESULT%

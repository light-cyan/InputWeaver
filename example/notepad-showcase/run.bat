@echo off
setlocal
pushd "%~dp0..\.."

if not exist "bin\InputWeaver.exe" (
    echo InputWeaver.exe is missing. Run script\build.bat first.
    popd
    exit /b 2
)

if not exist "example\notepad-showcase\notepad-showcase.weavec" (
    echo notepad-showcase.weavec is missing. Run example\notepad-showcase\compile.bat first.
    popd
    exit /b 2
)

"bin\InputWeaver.exe" --program "example\notepad-showcase\notepad-showcase.weavec" --allow-exec --log "example\notepad-showcase\acceptance.jsonl" --trace-input
set "INPUTWEAVER_RESULT=%errorlevel%"
popd
exit /b %INPUTWEAVER_RESULT%

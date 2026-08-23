@echo off
setlocal
pushd "%~dp0.."

if not exist "bin\InputWeaver.exe" (
    echo InputWeaver.exe is missing. Run script\build.bat first.
    popd
    exit /b 2
)

if not exist "example\notepad-showcase.weavec" (
    echo notepad-showcase.weavec is missing. Run example\compile.bat first.
    popd
    exit /b 2
)

"bin\InputWeaver.exe" --program "example\notepad-showcase.weavec" --log "example\acceptance.jsonl" --trace-input
set "INPUTWEAVER_RESULT=%errorlevel%"
popd
exit /b %INPUTWEAVER_RESULT%

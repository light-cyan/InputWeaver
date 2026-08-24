@echo off
setlocal
cd /d "%~dp0.."

echo [1/6] Running the complete Phase 3 verification baseline...
call script\verify_phase3.bat
if errorlevel 1 exit /b %errorlevel%

echo [2/6] Verifying the example source and compiled artifacts...
"bin\InputWeaverCompiler.exe" compile "example\notepad-showcase\notepad-showcase.weave" "bin\notepad-showcase.weavec"
if errorlevel 1 exit /b %errorlevel%
fc /b "bin\notepad-showcase.weavec" "example\notepad-showcase\notepad-showcase.weavec" >nul
if errorlevel 1 (
    echo example\notepad-showcase\notepad-showcase.weavec is out of date.
    exit /b 1
)
"bin\InputWeaverCompiler.exe" validate "example\phase-4-safety\phase4-safety.weave"
if errorlevel 1 exit /b %errorlevel%
"bin\InputWeaverCompiler.exe" compile "example\phase-4-safety\phase4-safety.weave" "bin\phase4-safety.weavec"
if errorlevel 1 exit /b %errorlevel%
fc /b "bin\phase4-safety.weavec" "example\phase-4-safety\phase4-safety.weavec" >nul
if errorlevel 1 (
    echo example\phase-4-safety\phase4-safety.weavec is out of date.
    exit /b 1
)

echo [3/6] Verifying the executor command-line authority and target boundaries...
"bin\InputWeaver.exe" --help > "bin\phase4-cli.tmp" 2>&1
if errorlevel 1 exit /b %errorlevel%
findstr /c:"--allow-exec" "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo InputWeaver help does not publish --allow-exec.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--target-global" "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo InputWeaver help does not publish --target-global.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--test-rules" "bin\phase4-cli.tmp" >nul
if not errorlevel 1 (
    echo InputWeaver help publishes an unsupported command-line option.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
"bin\InputWeaver.exe" > "bin\phase4-cli.tmp" 2>&1
set "INPUTWEAVER_CLI_RESULT=%errorlevel%"
if not "%INPUTWEAVER_CLI_RESULT%"=="2" (
    echo InputWeaver without --program returned an unexpected result.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--program is required." "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo InputWeaver did not report the required compiled program.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
"bin\InputWeaver.exe" --test-rules --target "notepad.exe" > "bin\phase4-cli.tmp" 2>&1
set "INPUTWEAVER_CLI_RESULT=%errorlevel%"
if not "%INPUTWEAVER_CLI_RESULT%"=="2" (
    echo Unsupported executor option returned an unexpected result.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"Unknown option: --test-rules" "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo Unsupported executor option was not rejected by the CLI.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
"bin\InputWeaver.exe" --allow-exec > "bin\phase4-cli.tmp" 2>&1
set "INPUTWEAVER_CLI_RESULT=%errorlevel%"
if not "%INPUTWEAVER_CLI_RESULT%"=="2" (
    echo --allow-exec without --program returned an unexpected result.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--allow-exec is valid only with --program." "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo --allow-exec without --program did not report its authority boundary.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
"bin\InputWeaver.exe" --target-global > "bin\phase4-cli.tmp" 2>&1
set "INPUTWEAVER_CLI_RESULT=%errorlevel%"
if not "%INPUTWEAVER_CLI_RESULT%"=="2" (
    echo --target-global without --program returned an unexpected result.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--target-global is valid only with --program." "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo --target-global without --program did not report its scope.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
"bin\InputWeaver.exe" --program "example\phase-4-safety\phase4-safety.weavec" --target "notepad.exe" --target-global > "bin\phase4-cli.tmp" 2>&1
set "INPUTWEAVER_CLI_RESULT=%errorlevel%"
if not "%INPUTWEAVER_CLI_RESULT%"=="2" (
    echo Conflicting target overrides returned an unexpected result.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
findstr /c:"--target and --target-global are mutually exclusive." "bin\phase4-cli.tmp" >nul
if errorlevel 1 (
    echo Conflicting target overrides were not rejected before execution.
    del /q "bin\phase4-cli.tmp" >nul 2>nul
    exit /b 1
)
del /q "bin\phase4-cli.tmp" >nul 2>nul

echo [4/6] Self-testing the retained Windows acceptance validator...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "example\phase-4-safety\verify.ps1" -SelfTest
if errorlevel 1 exit /b %errorlevel%

echo [5/6] Verifying the retained physical Windows acceptance evidence...
if not exist "example\phase-4-safety\verify.bat" (
    echo example\phase-4-safety\verify.bat is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\verify.ps1" (
    echo example\phase-4-safety\verify.ps1 is missing.
    exit /b 1
)
call example\phase-4-safety\verify.bat
if errorlevel 1 exit /b %errorlevel%

echo [6/6] Checking current user guides and self-contained example assets...
if not exist "docs\safety-guide.md" (
    echo docs\safety-guide.md is missing.
    exit /b 1
)
if not exist "docs\runtime-boundaries.md" (
    echo docs\runtime-boundaries.md is missing.
    exit /b 1
)
if not exist "src\ui\cli\compiler_cli.hpp" (
    echo src\ui\cli\compiler_cli.hpp is missing.
    exit /b 1
)
if not exist "src\ui\cli\runtime_cli.hpp" (
    echo src\ui\cli\runtime_cli.hpp is missing.
    exit /b 1
)
if not exist "src\platform\windows\cli\compiler_main.cpp" (
    echo src\platform\windows\cli\compiler_main.cpp is missing.
    exit /b 1
)
if not exist "src\platform\windows\cli\runtime_main.cpp" (
    echo src\platform\windows\cli\runtime_main.cpp is missing.
    exit /b 1
)
if not exist "example\notepad-showcase\ManualTest.md" (
    echo example\notepad-showcase\ManualTest.md is missing.
    exit /b 1
)
if not exist "example\notepad-showcase\compile.bat" (
    echo example\notepad-showcase\compile.bat is missing.
    exit /b 1
)
if not exist "example\notepad-showcase\run.bat" (
    echo example\notepad-showcase\run.bat is missing.
    exit /b 1
)
if not exist "example\notepad-showcase\acceptance.jsonl" (
    echo example\notepad-showcase\acceptance.jsonl is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\ManualTest.md" (
    echo example\phase-4-safety\ManualTest.md is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\compile.bat" (
    echo example\phase-4-safety\compile.bat is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\run.bat" (
    echo example\phase-4-safety\run.bat is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\run.ps1" (
    echo example\phase-4-safety\run.ps1 is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\acceptance.jsonl" (
    echo example\phase-4-safety\acceptance.jsonl is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\acceptance.txt" (
    echo example\phase-4-safety\acceptance.txt is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\startup-held.jsonl" (
    echo example\phase-4-safety\startup-held.jsonl is missing.
    exit /b 1
)
if not exist "example\phase-4-safety\startup-held.txt" (
    echo example\phase-4-safety\startup-held.txt is missing.
    exit /b 1
)

echo Phase 4 verification completed successfully.
exit /b 0

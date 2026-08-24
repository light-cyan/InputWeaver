[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("main", "startup-held")]
    [string]$Mode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$exampleRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $exampleRoot
$executablePath = Join-Path $repositoryRoot "bin\InputWeaver.exe"
$programPath = Join-Path $PSScriptRoot "phase4-safety.weavec"

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "InputWeaver.exe is missing. Run script\build.bat first."
}
if (-not (Test-Path -LiteralPath $programPath -PathType Leaf)) {
    throw "The Phase 4 acceptance artifact is missing. Run example\phase-4-safety\compile.bat first."
}

if ($Mode -eq "main") {
    $jsonlPath = Join-Path $PSScriptRoot "acceptance.jsonl"
    $consolePath = Join-Path $PSScriptRoot "acceptance.txt"
} else {
    $jsonlPath = Join-Path $PSScriptRoot "startup-held.jsonl"
    $consolePath = Join-Path $PSScriptRoot "startup-held.txt"
    Write-Host "The executor will start in five seconds. Focus Notepad and hold F6, left Ctrl, and right Shift now."
    Start-Sleep -Seconds 5
}

$arguments = @(
    "--program", $programPath,
    "--allow-exec",
    "--log", $jsonlPath
)

Set-Location -LiteralPath $repositoryRoot
Write-Host "Starting InputWeaver; live executor output follows."
& $executablePath @arguments 2>&1 | Tee-Object -FilePath $consolePath
$executorExitCode = $LASTEXITCODE
$exitLine = "InputWeaver acceptance exit_code=$executorExitCode"
Write-Output $exitLine
$exitLine | Out-File -LiteralPath $consolePath -Append
exit $executorExitCode

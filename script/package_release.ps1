$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$binRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'bin'))
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $binRoot 'release'))
$packageRoot = [IO.Path]::GetFullPath((Join-Path $releaseRoot 'InputWeaver'))
$archivePath = [IO.Path]::GetFullPath((Join-Path $releaseRoot 'InputWeaver-windows-x64.zip'))

function Assert-ChildPath([string]$Path, [string]$Parent) {
    $prefix = $Parent.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Release path escapes its expected parent: $Path"
    }
}

Assert-ChildPath $releaseRoot $binRoot
Assert-ChildPath $packageRoot $releaseRoot
Assert-ChildPath $archivePath $releaseRoot

$executables = @(
    'InputWeaver.exe',
    'InputWeaverCompiler.exe',
    'InputWeaverHost.exe',
    'InputWeaverTUI.exe'
)
$allowedSystemDlls = @(
    'ADVAPI32.dll',
    'GDI32.dll',
    'KERNEL32.dll',
    'SHELL32.dll',
    'USER32.dll'
)

foreach ($name in $executables) {
    $path = Join-Path $binRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required release executable is missing: $path"
    }
    $headers = & objdump.exe -f $path 2>&1
    $headerText = $headers -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $headerText -notmatch 'architecture: i386:x86-64') {
        throw "Release executable is not Windows x64: $path"
    }
    $imports = & objdump.exe -p $path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot inspect release dependencies: $path"
    }
    $dependencies = $imports |
        Select-String -Pattern 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    foreach ($dependency in $dependencies) {
        $isAllowed = $allowedSystemDlls -contains $dependency
        $isWindowsApiSet = $dependency.StartsWith(
            'api-ms-win-',
            [StringComparison]::OrdinalIgnoreCase)
        if (-not $isAllowed -and -not $isWindowsApiSet) {
            throw "Non-system release dependency in ${name}: $dependency"
        }
    }
}

$colorScheme = Join-Path $repositoryRoot 'res\InputWeaverTUI.colors.json'
$documents = @(
    'docs\grammar.md',
    'docs\tui-guide.md',
    'docs\safety-guide.md',
    'docs\runtime-boundaries.md'
)
if (-not (Test-Path -LiteralPath $colorScheme -PathType Leaf)) {
    throw "Required release color scheme is missing: $colorScheme"
}
foreach ($relativePath in $documents) {
    $path = Join-Path $repositoryRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required release document is missing: $path"
    }
}

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

$resourceRoot = Join-Path $packageRoot 'res'
$documentRoot = Join-Path $packageRoot 'docs'
$null = New-Item -ItemType Directory -Path $resourceRoot -Force
$null = New-Item -ItemType Directory -Path $documentRoot -Force

foreach ($name in $executables) {
    Copy-Item -LiteralPath (Join-Path $binRoot $name) -Destination $packageRoot
}
Copy-Item -LiteralPath $colorScheme -Destination $resourceRoot
foreach ($relativePath in $documents) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $relativePath) -Destination $documentRoot
}

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
$archiveStream = [IO.File]::OpenRead($archivePath)
try {
    $archiveHash = [BitConverter]::ToString(
        $hashAlgorithm.ComputeHash($archiveStream)).Replace('-', '')
} finally {
    $archiveStream.Dispose()
    $hashAlgorithm.Dispose()
}
Write-Host "Release directory: $packageRoot"
Write-Host "Release archive:   $archivePath"
Write-Host "SHA-256:           $archiveHash"

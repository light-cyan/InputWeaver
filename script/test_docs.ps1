$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot = Join-Path $repositoryRoot ('bin\docs-validation-tests\' + [Guid]::NewGuid().ToString('N'))
$compiler = Join-Path $repositoryRoot 'bin\InputWeaverCompiler.exe'
$verifier = Join-Path $PSScriptRoot 'verify_docs.ps1'
$utf8 = New-Object Text.UTF8Encoding($false)
$passed = 0
$template = @'
<a id="section-guide"></a>

# Guide

[LANGUAGE](../LOCALE/README.md)

<a id="section-example"></a>

## Example

[This section](#section-example) and [external reference](https://example.com/).
`[This is code](missing.md)`

```weave
number count = 0;
F6:down => set(count, count + 1);
```

```powershell
.\InputWeaverCompiler.exe validate .\sample.weave
```
'@

function Run-Case([string]$Name, [scriptblock]$Change, [string]$Expected = '') {
    $caseRoot = Join-Path $testRoot $Name
    $docs = Join-Path $caseRoot 'docs'
    $null = New-Item -ItemType Directory -Path (Join-Path $docs 'zh'), (Join-Path $docs 'en') -Force
    [IO.File]::WriteAllText((Join-Path $docs 'README.md'), "# Documentation`n`n[Chinese](zh/README.md)`n[English](en/README.md)`n", $utf8)
    $zh = $template.Replace('LANGUAGE', 'English').Replace('LOCALE', 'en')
    $en = $template.Replace('LANGUAGE', 'Chinese').Replace('LOCALE', 'zh')
    $extra = @{}
    . $Change
    if ($null -ne $zh) { [IO.File]::WriteAllText((Join-Path $docs 'zh\README.md'), $zh, $utf8) }
    if ($null -ne $en) { [IO.File]::WriteAllText((Join-Path $docs 'en\README.md'), $en, $utf8) }
    foreach ($path in $extra.Keys) { [IO.File]::WriteAllText((Join-Path $docs $path), $extra[$path], $utf8) }
    $output = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifier -DocumentationRoot $docs -CompilerPath $compiler -OutputRoot (Join-Path $caseRoot 'examples') 2>&1)
    $code = $LASTEXITCODE
    $message = $output -join "`n"
    if (($Expected -and ($code -eq 0 -or -not $message.Contains($Expected))) -or (-not $Expected -and $code -ne 0)) {
        throw "Case '$Name' failed (exit $code, expected '$Expected'):`n$message"
    }
    $script:passed++
    Write-Host "PASS $Name"
}

try {
    Run-Case 'valid' {}
    Run-Case 'missing-translation' { $en = $null } 'Missing translated page'
    Run-Case 'extra-translation' { $extra['en\extra.md'] = $en } 'Missing translated page'
    Run-Case 'heading-level' { $en = $en.Replace('## Example', '### Example') } 'Heading levels, order, or shared anchors differ'
    Run-Case 'heading-anchor' { $en = $en.Replace('section-example', 'section-changed') } 'Heading levels, order, or shared anchors differ'
    Run-Case 'missing-switch' { $en = $en.Replace('[Chinese](../zh/README.md)', '') } 'Add a direct language switch'
    Run-Case 'broken-file' { $zh += "`n[Broken](missing.md)`n" } 'Broken file link'
    Run-Case 'broken-section' { $en = $en.Replace('](#section-example)', '](#missing)') } 'Broken section link'
    Run-Case 'filename-case' { $en = $en.Replace('../zh/README.md', '../zh/readme.md') } 'Document link casing'
    Run-Case 'escaping-link' { $zh += "`n[Outside](../../outside.md)`n" } 'Link escapes packaged documentation'
    Run-Case 'example-drift' { $en = $en.Replace('count + 1', 'count + 2') } 'Example content, language tag, or section differs'
    Run-Case 'example-section' {
        $en = $en.Replace('## Example', "## Example`n`n<a id=`"section-other`"></a>`n`n## Other")
        $zh += "`n`n<a id=`"section-other`"></a>`n`n## Other`n"
    } 'Example content, language tag, or section differs'
    Run-Case 'invalid-weave' {
        $zh = $zh.Replace('count + 1', 'unknown_name + 1')
        $en = $en.Replace('count + 1', 'unknown_name + 1')
    } 'Invalid Weave example'
    Run-Case 'invalid-powershell' {
        $zh = $zh.Replace('.\sample.weave', '"unterminated')
        $en = $en.Replace('.\sample.weave', '"unterminated')
    } 'Invalid PowerShell example'
    Run-Case 'unknown-code-language' { $en = $en.Replace('```weave', '```text') } 'Code fences must use weave or powershell'
    Run-Case 'unclosed-fence' { $zh += "`n" + '```weave' + "`nnumber value = 0;`n" } 'Unclosed code fence'
    Run-Case 'line-ending-equivalence' { $en = $en.Replace("`r`n", "`n").Replace("`n", "`r`n") }
    Write-Host "Documentation verifier tests passed: $passed cases."
    exit 0
} catch {
    Write-Host $_.Exception.Message
    exit 1
}

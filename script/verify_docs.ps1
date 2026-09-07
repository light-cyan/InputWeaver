param(
    [string]$DocumentationRoot = (Join-Path $PSScriptRoot '..\docs'),
    [string]$CompilerPath = (Join-Path $PSScriptRoot '..\bin\InputWeaverCompiler.exe'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\bin\docs-validation')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = New-Object Text.UTF8Encoding($false, $true)
$problems = New-Object 'System.Collections.Generic.List[string]'
$documents = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([StringComparer]::Ordinal)
$DocumentationRoot = [IO.Path]::GetFullPath($DocumentationRoot).TrimEnd([char[]]'\/')
$CompilerPath = [IO.Path]::GetFullPath($CompilerPath)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$documentPrefix = $DocumentationRoot + [IO.Path]::DirectorySeparatorChar

function Add-Problem([string]$Name, [int]$Line, [string]$Message) {
    $problems.Add("${Name}:${Line}: $Message")
}

function Read-Document([IO.FileInfo]$File) {
    $name = $File.FullName.Substring($documentPrefix.Length).Replace('\', '/')
    $lines = [IO.File]::ReadAllText($File.FullName, $utf8).Replace("`r`n", "`n").Split("`n")
    $headings = New-Object 'System.Collections.Generic.List[object]'
    $links = New-Object 'System.Collections.Generic.List[object]'
    $blocks = New-Object 'System.Collections.Generic.List[object]'
    $anchors = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $slugs = @{}
    $pendingAnchor = ''
    $section = ''
    $fence = ''
    $tag = ''
    $blockLine = 0
    $body = New-Object 'System.Collections.Generic.List[string]'
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ($fence) {
            if ($line -match ('^ {0,3}' + [regex]::Escape($fence.Substring(0, 1)) + '{' + $fence.Length + ',}\s*$')) {
                $blocks.Add([pscustomobject]@{ Tag = $tag; Body = ($body -join "`n"); Line = $blockLine; Section = $section })
                $fence = ''
            } else { $body.Add($line) }
            continue
        }
        if ($line -match '^ {0,3}(`{3,}|~{3,})(.*)$') {
            $fence = $Matches[1]
            $tag = $Matches[2].Trim()
            $blockLine = $i + 2
            $body.Clear()
            if ($tag -cnotin @('weave', 'powershell')) {
                Add-Problem $name ($i + 1) 'Code fences must use weave or powershell so examples can be validated.'
            }
            continue
        }
        if ($line -match '^<a id="([a-z][a-z0-9-]*)"></a>$') {
            $pendingAnchor = $Matches[1]
            if (-not $anchors.Add($pendingAnchor)) { Add-Problem $name ($i + 1) "Duplicate anchor: $pendingAnchor" }
            continue
        }
        if ($line -match '^(#{1,6}) (.+)$') {
            $level = $Matches[1].Length
            $title = $Matches[2]
            if ($name -match '^(zh|en)/' -and -not $pendingAnchor) {
                Add-Problem $name ($i + 1) 'Heading needs a shared explicit anchor immediately above it.'
            }
            $section = $pendingAnchor
            $headings.Add([pscustomobject]@{ Level = $level; Id = $section; Line = $i + 1 })
            $slug = (($title.ToLowerInvariant() -replace '[^\p{L}\p{N}\p{M}\s_-]', '') -replace '\s', '-')
            $baseSlug = $slug
            if ($slugs.ContainsKey($baseSlug)) { $slugs[$baseSlug]++; $slug += '-' + $slugs[$baseSlug] }
            else { $slugs[$baseSlug] = 0 }
            $null = $anchors.Add($slug)
        }
        if ($line.Trim()) {
            if ($pendingAnchor -and $line -notmatch '^#{1,6} ') {
                Add-Problem $name ($i + 1) 'An explicit anchor must be followed by its heading.'
            }
            $pendingAnchor = ''
        }
        $prose = $line -replace '`+[^`]*`+', ''
        if ($prose -match '<(?:a\s+[^>]*href|img\s)|\[[^\]]+\]\[[^\]]*\]|^\s*\[[^\]]+\]:|^ {4}\S|<!-- code:') {
            Add-Problem $name ($i + 1) 'Use inline Markdown links and fenced code blocks; unfinished example markers are invalid.'
        }
        foreach ($match in [regex]::Matches($prose, '\[[^\]\r\n]*\]\((?:<([^>\r\n]+)>|([^\s)]+))(?:\s+"[^"]*")?\)')) {
            $target = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            $links.Add([pscustomobject]@{ Target = $target; Line = $i + 1 })
        }
    }
    if ($fence) { Add-Problem $name ($blockLine - 1) 'Unclosed code fence.' }
    if ($pendingAnchor) { Add-Problem $name $lines.Length 'Anchor has no heading.' }
    if ($headings.Count -eq 0 -or $headings[0].Level -ne 1 -or @($headings | Where-Object Level -eq 1).Count -ne 1) {
        Add-Problem $name 1 'Each page must have one level-one title as its first heading.'
    }
    return [pscustomobject]@{ Name = $name; Path = $File.FullName; Headings = $headings; Anchors = $anchors; Links = $links; Blocks = $blocks }
}

try {
    if (-not (Test-Path -LiteralPath $DocumentationRoot -PathType Container)) { throw "Documentation directory is missing: $DocumentationRoot" }
    if (-not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) { throw "Compiler is missing: $CompilerPath. Run script\verify_docs.bat to build it." }
    foreach ($file in Get-ChildItem -LiteralPath $DocumentationRoot -Filter '*.md' -File -Recurse | Sort-Object FullName) {
        $document = Read-Document $file
        $documents.Add($document.Name, $document)
    }
    foreach ($entry in @('README.md', 'zh/README.md', 'en/README.md')) {
        if (-not $documents.ContainsKey($entry)) { Add-Problem $entry 1 'Required documentation entry point is missing.' }
    }

    $pairCount = 0
    foreach ($name in @($documents.Keys | Sort-Object)) {
        $document = $documents[$name]
        $counterpart = ''
        if ($name -cmatch '^(zh|en)/(.+)$') {
            $locale = $Matches[1]
            $counterpart = $(if ($locale -eq 'zh') { 'en/' } else { 'zh/' }) + $Matches[2]
            if (-not $documents.ContainsKey($counterpart)) { Add-Problem $name 1 "Missing translated page: $counterpart" }
            elseif ($locale -eq 'zh') {
                $pairCount++
                $other = $documents[$counterpart]
                $structure = @($document.Headings | ForEach-Object { "$($_.Level):$($_.Id)" }) -join '|'
                $otherStructure = @($other.Headings | ForEach-Object { "$($_.Level):$($_.Id)" }) -join '|'
                if ($structure -cne $otherStructure) { Add-Problem $name 1 "Heading levels, order, or shared anchors differ from $counterpart." }
                if ($document.Blocks.Count -ne $other.Blocks.Count) { Add-Problem $name 1 "Code block count differs from $counterpart." }
                else {
                    for ($b = 0; $b -lt $document.Blocks.Count; $b++) {
                        $left = $document.Blocks[$b]
                        $right = $other.Blocks[$b]
                        if ($left.Tag -cne $right.Tag -or $left.Body -cne $right.Body -or $left.Section -cne $right.Section) {
                            Add-Problem $name $left.Line "Example content, language tag, or section differs from ${counterpart}:$($right.Line)."
                        }
                    }
                }
            }
        }
        $hasSwitch = $false
        $firstSection = @($document.Headings | Where-Object Level -gt 1 | Select-Object -First 1)
        $switchBoundary = if ($firstSection.Count) { $firstSection[0].Line } else { [int]::MaxValue }
        foreach ($link in $document.Links) {
            if ($link.Target -match '^(https?://|mailto:)') { continue }
            if ($link.Target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:|^[/\\]') {
                Add-Problem $name $link.Line "Use a relative local link: $($link.Target)"
                continue
            }
            $parts = $link.Target.Split([char]'#', 2)
            $relative = [Uri]::UnescapeDataString($parts[0])
            $destination = if ($relative) { [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $document.Path) $relative)) } else { $document.Path }
            if (-not $destination.StartsWith($documentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                Add-Problem $name $link.Line "Link escapes packaged documentation: $($link.Target)"
                continue
            }
            $destinationName = $destination.Substring($documentPrefix.Length).Replace('\', '/')
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                Add-Problem $name $link.Line "Broken file link: $($link.Target)"
                continue
            }
            if ([IO.Path]::GetExtension($destination) -ieq '.md') {
                if (-not $documents.ContainsKey($destinationName)) {
                    Add-Problem $name $link.Line "Document link casing does not match the filename: $($link.Target)"
                    continue
                }
                if ($parts.Length -eq 2 -and $parts[1] -and -not $documents[$destinationName].Anchors.Contains([Uri]::UnescapeDataString($parts[1]))) {
                    Add-Problem $name $link.Line "Broken section link: $($link.Target)"
                }
            }
            if ($counterpart -and $destinationName -ceq $counterpart -and $link.Line -lt $switchBoundary -and $parts.Length -eq 1) { $hasSwitch = $true }
            if ($counterpart -and $destinationName -cmatch '^(zh|en)/' -and $Matches[1] -cne $locale -and $destinationName -cne $counterpart) {
                Add-Problem $name $link.Line "Cross-language links must switch to the corresponding page: $($link.Target)"
            }
        }
        if ($counterpart -and -not $hasSwitch) { Add-Problem $name 1 "Add a direct language switch to $counterpart above the first section." }
    }
    if ($documents.ContainsKey('README.md')) {
        foreach ($target in @('zh/README.md', 'en/README.md')) {
            if ($target -cnotin @($documents['README.md'].Links | ForEach-Object Target)) { Add-Problem 'README.md' 1 "Language index must link to $target." }
        }
    }

    $null = New-Item -ItemType Directory -Path $OutputRoot -Force
    $weaveCount = 0
    $powershellCount = 0
    foreach ($name in @($documents.Keys | Sort-Object)) {
        foreach ($block in $documents[$name].Blocks) {
            if ($block.Tag -ceq 'weave') {
                $weaveCount++
                $examplePath = Join-Path $OutputRoot ('example-{0:D3}.weave' -f $weaveCount)
                [IO.File]::WriteAllText($examplePath, $block.Body + "`n", $utf8)
                # Validation compiles the source without executing any documented action.
                $savedPreference = $ErrorActionPreference
                try {
                    # Windows PowerShell represents native stderr as error records.
                    $ErrorActionPreference = 'Continue'
                    $LASTEXITCODE = $null
                    $diagnostics = @(& $CompilerPath validate $examplePath 2>&1)
                    $validationExit = $LASTEXITCODE
                } finally { $ErrorActionPreference = $savedPreference }
                if ($validationExit -ne 0) { Add-Problem $name $block.Line ("Invalid Weave example:`n" + ($diagnostics -join "`n")) }
            } elseif ($block.Tag -ceq 'powershell') {
                $powershellCount++
                $tokens = $null
                $parseErrors = $null
                $null = [Management.Automation.Language.Parser]::ParseInput($block.Body, [ref]$tokens, [ref]$parseErrors)
                foreach ($parseError in $parseErrors) { Add-Problem $name ($block.Line + $parseError.Extent.StartLineNumber - 1) ("Invalid PowerShell example: " + $parseError.Message) }
            }
        }
    }
    if ($problems.Count) {
        foreach ($problem in $problems) { Write-Host $problem }
        Write-Host "Documentation verification failed: $($problems.Count) problem(s)."
        exit 1
    }
    Write-Host "Documentation verified: $($documents.Count) pages, $pairCount bilingual pairs, $weaveCount Weave examples, $powershellCount PowerShell examples."
    exit 0
} catch {
    Write-Host "Documentation verification failed: $($_.Exception.Message)"
    exit 1
}

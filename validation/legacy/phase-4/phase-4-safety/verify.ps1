[CmdletBinding()]
param(
    [string]$AcceptanceLog,
    [string]$AcceptanceConsole,
    [string]$StartupHeldLog,
    [string]$StartupHeldConsole,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script:Failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure {
    param([string]$Message)
    [void]$script:Failures.Add($Message)
}

function Test-RecordShape {
    param(
        [object]$Record,
        [string[]]$Properties,
        [string]$Context
    )
    $valid = $true
    foreach ($property in $Properties) {
        if ($null -eq $Record.PSObject.Properties[$property]) {
            Add-Failure "$Context is missing '$property'."
            $valid = $false
        }
    }
    return $valid
}

function Get-Int64Field {
    param(
        [object]$Record,
        [string]$Property,
        [string]$Context
    )
    try {
        return [Convert]::ToInt64(
            $Record.$Property,
            [Globalization.CultureInfo]::InvariantCulture)
    } catch {
        Add-Failure "$Context has a non-integer '$Property'."
        return [int64]0
    }
}

function Get-BoolField {
    param(
        [object]$Record,
        [string]$Property,
        [string]$Context
    )
    if ($Record.$Property -isnot [bool]) {
        Add-Failure "$Context has a non-Boolean '$Property'."
        return $false
    }
    return [bool]$Record.$Property
}

function Read-JsonlRecords {
    param(
        [string]$Path,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure "$Label does not exist: $Path"
        return @()
    }
    $records = [System.Collections.Generic.List[object]]::new()
    $lineNumber = 0
    foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $Path).Path)) {
        ++$lineNumber
        if ([string]::IsNullOrWhiteSpace($line)) {
            Add-Failure "$Label line $lineNumber is blank."
            continue
        }
        try {
            $record = $line | ConvertFrom-Json -ErrorAction Stop
        } catch {
            Add-Failure "$Label line $lineNumber is not valid JSON."
            continue
        }
        if ($null -eq $record -or $null -eq $record.PSObject.Properties["kind"]) {
            Add-Failure "$Label line $lineNumber has no record kind."
            continue
        }
        if ([string]$record.kind -notin @("hook", "injection", "runtime")) {
            Add-Failure "$Label line $lineNumber has unknown record kind '$($record.kind)'."
            continue
        }
        [void]$records.Add($record)
    }
    if ($lineNumber -eq 0) {
        Add-Failure "$Label is empty."
    }
    return @($records)
}

function Test-CommonLog {
    param(
        [object[]]$Records,
        [string]$Label
    )
    $hookFields = @("seq", "qpc", "duration_us", "device", "transition", "code", "origin", "suppressed")
    $injectionFields = @(
        "source_seq", "generation", "qpc", "device", "transition", "code",
        "requested", "sent", "error", "cleanup_requested", "cleanup_sent", "cleanup_error",
        "cancelled_target", "cancelled_physical", "cancelled_circuit", "cancelled_shutdown",
        "cancelled_generation", "circuit_open")
    $runtimeFields = @(
        "event", "program_serial", "generation", "sequence", "source_begin", "source_length",
        "subject", "position", "deadline_ns", "detail")
    $hooks = [System.Collections.Generic.List[object]]::new()
    $injections = [System.Collections.Generic.List[object]]::new()
    $runtime = [System.Collections.Generic.List[object]]::new()
    $successfulInjections = [System.Collections.Generic.List[object]]::new()
    $maximumHookMicroseconds = [int64]0

    foreach ($record in $Records) {
        if ($record.kind -eq "hook") {
            $context = "$Label hook record"
            if (-not (Test-RecordShape $record $hookFields $context)) {
                continue
            }
            $duration = Get-Int64Field $record "duration_us" $context
            $qpc = Get-Int64Field $record "qpc" $context
            if ($duration -lt 0) {
                Add-Failure "$context has a negative duration."
            }
            if ($qpc -le 0) {
                Add-Failure "$context has a non-positive QPC timestamp."
            }
            if ($duration -gt $maximumHookMicroseconds) {
                $maximumHookMicroseconds = $duration
            }
            [void](Get-BoolField $record "suppressed" $context)
            [void]$hooks.Add($record)
            continue
        }
        if ($record.kind -eq "runtime") {
            $context = "$Label runtime record"
            if (-not (Test-RecordShape $record $runtimeFields $context)) {
                continue
            }
            [void](Get-Int64Field $record "generation" $context)
            [void](Get-Int64Field $record "subject" $context)
            [void](Get-Int64Field $record "detail" $context)
            [void]$runtime.Add($record)
            continue
        }

        $context = "$Label injection record"
        if (-not (Test-RecordShape $record $injectionFields $context)) {
            continue
        }
        $sourceSequence = Get-Int64Field $record "source_seq" $context
        $qpc = Get-Int64Field $record "qpc" $context
        $requested = Get-Int64Field $record "requested" $context
        $sent = Get-Int64Field $record "sent" $context
        $errorCode = Get-Int64Field $record "error" $context
        $cleanupRequested = Get-Int64Field $record "cleanup_requested" $context
        $cleanupSent = Get-Int64Field $record "cleanup_sent" $context
        $cleanupError = Get-Int64Field $record "cleanup_error" $context
        $cancelled = $false
        foreach ($field in @(
            "cancelled_target", "cancelled_physical", "cancelled_circuit",
            "cancelled_shutdown", "cancelled_generation")) {
            $cancelled = (Get-BoolField $record $field $context) -or $cancelled
        }
        $circuitOpen = Get-BoolField $record "circuit_open" $context
        if ($sourceSequence -le 0 -or $qpc -le 0) {
            Add-Failure "$context has a non-positive sequence or QPC timestamp."
        }
        if ($errorCode -ne 0 -or $cleanupError -ne 0) {
            Add-Failure "$context reports an injection or cleanup error."
        }
        if ($sent -ne $requested -or $cleanupSent -ne $cleanupRequested) {
            Add-Failure "$context reports a partial injection or cleanup."
        }
        if ($circuitOpen) {
            Add-Failure "$context reports an open injection circuit."
        }
        if ($cancelled -and ($requested -ne 0 -or $sent -ne 0)) {
            Add-Failure "$context reports both cancellation and native injection."
        }
        if (-not $cancelled -and $requested -le 0) {
            Add-Failure "$context neither injected output nor recorded cancellation."
        }
        if (-not $cancelled -and $requested -gt 0 -and $sent -eq $requested -and
            $errorCode -eq 0 -and $cleanupError -eq 0 -and -not $circuitOpen) {
            [void]$successfulInjections.Add($record)
        }
        [void]$injections.Add($record)
    }

    if ($hooks.Count -eq 0) {
        Add-Failure "$Label contains no hook records."
    }
    if ($injections.Count -eq 0) {
        Add-Failure "$Label contains no injection records."
    }
    if ($runtime.Count -eq 0) {
        Add-Failure "$Label contains no runtime records."
    }

    $previousSourceSequence = [int64]0
    $held = @{}
    $cycles = @{}
    foreach ($record in @($successfulInjections | Sort-Object { [int64]$_.source_seq })) {
        $sourceSequence = [int64]$record.source_seq
        if ($sourceSequence -le $previousSourceSequence) {
            Add-Failure "$Label successful injection sequences are not strictly increasing."
        }
        $previousSourceSequence = $sourceSequence
        if ([string]$record.device -ne "Keyboard") {
            continue
        }
        $key = [string]([int64]$record.code)
        $transition = [string]$record.transition
        if ($transition -eq "Down") {
            if (-not $held.ContainsKey($key) -or -not [bool]$held[$key]) {
                $held[$key] = $true
                if (-not $cycles.ContainsKey($key)) {
                    $cycles[$key] = 0
                }
                $cycles[$key] = [int]$cycles[$key] + 1
            }
        } elseif ($transition -eq "Up") {
            if (-not $held.ContainsKey($key) -or -not [bool]$held[$key]) {
                Add-Failure "$Label publishes keyboard code $key up without a retained down."
            }
            $held[$key] = $false
        } else {
            Add-Failure "$Label has an unsupported successful keyboard output transition '$transition'."
        }
    }
    foreach ($key in $held.Keys) {
        if ([bool]$held[$key]) {
            Add-Failure "$Label leaves keyboard code $key held at shutdown."
        }
    }

    $fatalEvents = @(
        "ActivationFailure", "TransactionCapacity", "PredicateFault", "TaskExpressionFault",
        "TaskActionFault", "LaunchFailure", "OutputFailure", "OutputRateExceeded")
    foreach ($record in $runtime) {
        if ([string]$record.event -in $fatalEvents) {
            Add-Failure "$Label contains unexpected runtime diagnostic '$($record.event)'."
        }
    }

    return [pscustomobject]@{
        Hooks = @($hooks)
        Injections = @($injections)
        Runtime = @($runtime)
        SuccessfulInjections = @($successfulInjections)
        Cycles = $cycles
        MaximumHookMicroseconds = $maximumHookMicroseconds
    }
}

function Get-PhysicalHookCount {
    param(
        [object[]]$Hooks,
        [int64]$Code,
        [string]$Transition,
        [bool]$RequireSuppressed
    )
    return @($Hooks | Where-Object {
        [string]$_.device -eq "Keyboard" -and
        [string]$_.origin -eq "PhysicalCandidate" -and
        [int64]$_.code -eq $Code -and
        [string]$_.transition -eq $Transition -and
        (-not $RequireSuppressed -or [bool]$_.suppressed)
    }).Count
}

function Get-RuntimeCount {
    param(
        [object[]]$Runtime,
        [string]$Event,
        [object]$Subject,
        [object]$Detail
    )
    return @($Runtime | Where-Object {
        [string]$_.event -eq $Event -and
        ($null -eq $Subject -or [int64]$_.subject -eq [int64]$Subject) -and
        ($null -eq $Detail -or [int64]$_.detail -eq [int64]$Detail)
    }).Count
}

function Test-F7CancellationEvidence {
    param(
        [object]$Summary,
        [string]$Label
    )
    $bDowns = @($Summary.SuccessfulInjections | Where-Object {
        [string]$_.device -eq "Keyboard" -and
        [int64]$_.code -eq 66 -and
        [string]$_.transition -eq "Down"
    })
    $cDowns = @($Summary.SuccessfulInjections | Where-Object {
        [string]$_.device -eq "Keyboard" -and
        [int64]$_.code -eq 67 -and
        [string]$_.transition -eq "Down"
    })
    $targetLosses = @($Summary.Runtime | Where-Object {
        [string]$_.event -eq "TargetEligibilityChange" -and
        [int64]$_.detail -eq 0
    })
    $targetCancellations = @($Summary.Runtime | Where-Object {
        [string]$_.event -eq "Cancellation" -and
        [int64]$_.subject -eq 2
    })

    $provedCancellation = $false
    foreach ($bDown in $bDowns) {
        $generation = [int64]$bDown.generation
        $bSequence = [int64]$bDown.source_seq
        foreach ($targetLoss in @($targetLosses | Where-Object {
            [int64]$_.generation -eq $generation -and
            [int64]$_.sequence -gt $bSequence
        } | Sort-Object { [int64]$_.sequence })) {
            $lossSequence = [int64]$targetLoss.sequence
            $hasCancellation = @($targetCancellations | Where-Object {
                [int64]$_.generation -gt $generation -and
                [int64]$_.sequence -eq $lossSequence
            }).Count -ne 0
            $completedBeforeLoss = @($cDowns | Where-Object {
                [int64]$_.generation -eq $generation -and
                [int64]$_.source_seq -gt $bSequence -and
                [int64]$_.source_seq -lt $lossSequence
            }).Count -ne 0
            if ($hasCancellation -and -not $completedBeforeLoss) {
                $provedCancellation = $true
                break
            }
        }
        if ($provedCancellation) {
            break
        }
    }
    if (-not $provedCancellation) {
        Add-Failure "$Label has no F7 task interrupted by target ineligibility before C output."
    }

    foreach ($cDown in $cDowns) {
        $generation = [int64]$cDown.generation
        $sourceSequence = [int64]$cDown.source_seq
        if (@($targetLosses | Where-Object {
            [int64]$_.generation -eq $generation -and
            [int64]$_.sequence -le $sourceSequence
        }).Count -ne 0) {
            Add-Failure "$Label contains successful stale C output after target loss in generation $generation."
        }
    }
}

function Test-MainAcceptance {
    param([object]$Summary)
    $label = "main acceptance log"
    foreach ($key in @(117, 118, 119, 120, 123)) {
        if ((Get-PhysicalHookCount $Summary.Hooks $key "Down" $true) -eq 0) {
            Add-Failure "$label has no suppressed physical VK $key down event."
        }
    }
    if ((Get-RuntimeCount $Summary.Runtime "TargetEligibilityChange" $null 0) -lt 3) {
        Add-Failure "$label records fewer than three foreground-loss transitions."
    }
    if ((Get-RuntimeCount $Summary.Runtime "TargetEligibilityChange" $null 1) -lt 3) {
        Add-Failure "$label records fewer than three foreground-return transitions."
    }
    if ((Get-RuntimeCount $Summary.Runtime "Cancellation" 2 $null) -lt 3) {
        Add-Failure "$label records fewer than three target-ineligible cancellations."
    }
    if ((Get-RuntimeCount $Summary.Runtime "TaskBudgetExceeded" $null 1) -eq 0) {
        Add-Failure "$label has no instruction-budget cancellation."
    }
    if ((Get-RuntimeCount $Summary.Runtime "Cancellation" 5 $null) -eq 0) {
        Add-Failure "$label has no physical force-stop cancellation."
    }
    $aCycles = 0
    if ($Summary.Cycles.ContainsKey("65")) {
        $aCycles = [int]$Summary.Cycles["65"]
    }
    if ($aCycles -lt 5) {
        Add-Failure "$label proves fewer than five complete A mapping lifecycles."
    }
    Test-F7CancellationEvidence $Summary $label
}

function Test-StartupHeldAcceptance {
    param([object]$Summary)
    $label = "startup-held acceptance log"
    $f6Ups = @($Summary.Hooks | Where-Object {
        [string]$_.device -eq "Keyboard" -and
        [string]$_.origin -eq "PhysicalCandidate" -and
        [int64]$_.code -eq 117 -and
        [string]$_.transition -eq "Up"
    } | Sort-Object { [int64]$_.qpc })
    if ($f6Ups.Count -eq 0) {
        Add-Failure "$label has no physical F6 release after startup."
        return
    }
    $firstF6UpQpc = [int64]$f6Ups[0].qpc
    $aDowns = @($Summary.SuccessfulInjections | Where-Object {
        [string]$_.device -eq "Keyboard" -and
        [int64]$_.code -eq 65 -and
        [string]$_.transition -eq "Down"
    })
    if (@($aDowns | Where-Object { [int64]$_.qpc -le $firstF6UpQpc }).Count -ne 0) {
        Add-Failure "$label generated A before the startup-held F6 was released."
    }
    if (@($aDowns | Where-Object { [int64]$_.qpc -gt $firstF6UpQpc }).Count -eq 0) {
        Add-Failure "$label has no fresh A mapping after the startup-held F6 release."
    }
    if ((Get-PhysicalHookCount $Summary.Hooks 123 "Down" $true) -eq 0) {
        Add-Failure "$label has no suppressed physical F12 force-stop event."
    }
    if ((Get-RuntimeCount $Summary.Runtime "Cancellation" 5 $null) -eq 0) {
        Add-Failure "$label has no physical force-stop cancellation."
    }
    $aCycles = 0
    if ($Summary.Cycles.ContainsKey("65")) {
        $aCycles = [int]$Summary.Cycles["65"]
    }
    if ($aCycles -lt 1) {
        Add-Failure "$label proves no complete fresh A mapping lifecycle."
    }
}

function Parse-Metrics {
    param([string]$Text)
    $metrics = @{}
    foreach ($match in [regex]::Matches($Text, "(?<key>[a-z_]+)=(?<value>[^\s]+)")) {
        $metrics[$match.Groups["key"].Value] = $match.Groups["value"].Value
    }
    return $metrics
}

function Test-ZeroMetric {
    param(
        [hashtable]$Metrics,
        [string]$Key,
        [string]$Context
    )
    if (-not $Metrics.ContainsKey($Key)) {
        Add-Failure "$Context is missing '$Key'."
        return
    }
    [uint64]$value = 0
    if (-not [uint64]::TryParse([string]$Metrics[$Key], [ref]$value) -or $value -ne 0) {
        Add-Failure "$Context requires $Key=0."
    }
}

function Test-ConsoleTranscript {
    param(
        [string]$Path,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure "$Label does not exist: $Path"
        return
    }
    $text = Get-Content -LiteralPath $Path -Raw
    $sessionMatches = [regex]::Matches(
        $text,
        "(?m)^Compiled-program session stopped\.(?<metrics>[^\r\n]*)")
    if ($sessionMatches.Count -eq 0) {
        Add-Failure "$Label has no completed compiled-program session metrics."
    }
    foreach ($match in $sessionMatches) {
        $metrics = Parse-Metrics $match.Groups["metrics"].Value
        Test-ZeroMetric $metrics "injection_failures" "$Label session metrics"
        if ($metrics.ContainsKey("runtime_diagnostic_drops")) {
            Test-ZeroMetric $metrics "runtime_diagnostic_drops" "$Label session metrics"
        }
        foreach ($key in @("max_hook_us", "scheduler_backoffs")) {
            if (-not $metrics.ContainsKey($key)) {
                Add-Failure "$Label session metrics are missing '$key'."
                continue
            }
            [uint64]$value = 0
            if (-not [uint64]::TryParse([string]$metrics[$key], [ref]$value)) {
                Add-Failure "$Label session metric '$key' is not a finite integer."
            }
        }
    }

    $finalMatches = [regex]::Matches(
        $text,
        "(?m)^Diagnostic log stopped\.(?<metrics>[^\r\n]*)")
    if ($finalMatches.Count -ne 1) {
        Add-Failure "$Label must contain exactly one final diagnostic-log metric line."
    } else {
        $metrics = Parse-Metrics $finalMatches[0].Groups["metrics"].Value
        foreach ($key in @("hook_log_drops", "injection_log_drops", "runtime_log_drops")) {
            Test-ZeroMetric $metrics $key "$Label final diagnostic metrics"
        }
        if (-not $metrics.ContainsKey("jsonl_truncated") -or
            [string]$metrics["jsonl_truncated"] -ne "false") {
            Add-Failure "$Label requires jsonl_truncated=false."
        }
    }

    $exitMatches = [regex]::Matches(
        $text,
        "(?m)^InputWeaver acceptance exit_code=(?<code>-?[0-9]+)\s*$")
    if ($exitMatches.Count -ne 1 -or $exitMatches[0].Groups["code"].Value -ne "0") {
        Add-Failure "$Label has no unique successful executor exit record."
    }
}

function Invoke-AcceptanceValidation {
    param(
        [string]$MainLog,
        [string]$MainConsole,
        [string]$StartupLog,
        [string]$StartupConsole
    )
    $mainRecords = @(Read-JsonlRecords $MainLog "main acceptance log")
    $startupRecords = @(Read-JsonlRecords $StartupLog "startup-held acceptance log")
    $mainMaximumHookMicroseconds = 0
    $startupMaximumHookMicroseconds = 0
    if ($mainRecords.Count -ne 0) {
        $mainSummary = Test-CommonLog $mainRecords "main acceptance log"
        Test-MainAcceptance $mainSummary
        $mainMaximumHookMicroseconds = $mainSummary.MaximumHookMicroseconds
    }
    if ($startupRecords.Count -ne 0) {
        $startupSummary = Test-CommonLog $startupRecords "startup-held acceptance log"
        Test-StartupHeldAcceptance $startupSummary
        $startupMaximumHookMicroseconds = $startupSummary.MaximumHookMicroseconds
    }
    Test-ConsoleTranscript $MainConsole "main acceptance console"
    Test-ConsoleTranscript $StartupConsole "startup-held acceptance console"
    return [pscustomobject]@{
        MainMaximumHookMicroseconds = $mainMaximumHookMicroseconds
        StartupMaximumHookMicroseconds = $startupMaximumHookMicroseconds
    }
}

function New-FixtureHook {
    param([int64]$Sequence, [int64]$Qpc, [int64]$Code, [string]$Transition, [bool]$Suppressed)
    return [ordered]@{ kind = "hook"; seq = $Sequence; qpc = $Qpc; duration_us = 4; device = "Keyboard"; transition = $Transition; code = $Code; origin = "PhysicalCandidate"; suppressed = $Suppressed }
}

function New-FixtureInjection {
    param([int64]$Sequence, [int64]$Qpc, [int64]$Code, [string]$Transition, [int64]$Generation = 1)
    return [ordered]@{ kind = "injection"; source_seq = $Sequence; generation = $Generation; qpc = $Qpc; device = "Keyboard"; transition = $Transition; code = $Code; requested = 1; sent = 1; error = 0; cleanup_requested = 0; cleanup_sent = 0; cleanup_error = 0; cancelled_target = $false; cancelled_physical = $false; cancelled_circuit = $false; cancelled_shutdown = $false; cancelled_generation = $false; circuit_open = $false }
}

function New-FixtureRuntime {
    param([string]$Event, [int64]$Generation, [int64]$Subject, [int64]$Detail, [int64]$Sequence = 1)
    return [ordered]@{ kind = "runtime"; event = $Event; program_serial = 1; generation = $Generation; sequence = $Sequence; source_begin = 0; source_length = 0; subject = $Subject; position = 4294967295; deadline_ns = 0; detail = $Detail }
}

function Write-FixtureJsonl {
    param([string]$Path, [object[]]$Records)
    $lines = @($Records | ForEach-Object { $_ | ConvertTo-Json -Compress })
    [IO.File]::WriteAllLines($Path, $lines, [Text.UTF8Encoding]::new($false))
}

function Invoke-SelfTest {
    $temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("InputWeaverPhase4Acceptance-" + [guid]::NewGuid().ToString("N"))
    [void][IO.Directory]::CreateDirectory($temporaryRoot)
    try {
        $mainRecords = [System.Collections.Generic.List[object]]::new()
        $hookSequence = 1
        foreach ($code in @(117, 118, 119, 120, 123)) {
            [void]$mainRecords.Add((New-FixtureHook $hookSequence (10 + $hookSequence) $code "Down" $true))
            ++$hookSequence
        }
        $injectionSequence = 1
        for ($cycle = 0; $cycle -lt 5; ++$cycle) {
            [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 65 "Down"))
            ++$injectionSequence
            [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 65 "Up"))
            ++$injectionSequence
        }
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 66 "Down" 1))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 66 "Up" 1))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 67 "Down" 1))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 67 "Up" 1))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 1 4294967295 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "Cancellation" 2 2 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 2 4294967295 1 $injectionSequence))

        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 66 "Down" 2))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence (100 + $injectionSequence) 66 "Up" 2))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 2 4294967295 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "Cancellation" 3 2 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 3 4294967295 1 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 3 4294967295 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "Cancellation" 4 2 0 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "TargetEligibilityChange" 4 4294967295 1 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "TaskBudgetExceeded" 4 0 1 $injectionSequence))
        [void]$mainRecords.Add((New-FixtureRuntime "Cancellation" 5 5 0 $injectionSequence))

        $startupRecords = @(
            (New-FixtureHook 1 100 117 "Up" $false),
            (New-FixtureHook 2 200 117 "Down" $true),
            (New-FixtureHook 3 400 123 "Down" $true),
            (New-FixtureInjection 1 250 65 "Down"),
            (New-FixtureInjection 2 300 65 "Up"),
            (New-FixtureRuntime "Cancellation" 2 5 0)
        )
        $mainLog = Join-Path $temporaryRoot "main.jsonl"
        $startupLog = Join-Path $temporaryRoot "startup.jsonl"
        $mainConsole = Join-Path $temporaryRoot "main.txt"
        $startupConsole = Join-Path $temporaryRoot "startup.txt"
        Write-FixtureJsonl $mainLog @($mainRecords)
        Write-FixtureJsonl $startupLog $startupRecords
        $consoleFixture = @"
Compiled-program session stopped. dispatched=1 suppressed=1 tasks_started=1 tasks_completed=0 tasks_cancelled=1 output_transitions=1 scheduler_backoffs=0 queued=1 cancelled_batches=0 injection_failures=0 outside_target_forwarded=0 max_hook_us=4 hook_log_drops=0 injection_log_drops=0 runtime_log_drops=0 jsonl_bytes=100 jsonl_truncated=false
Diagnostic log stopped. hook_log_drops=0 injection_log_drops=0 runtime_log_drops=0 jsonl_bytes=100 jsonl_truncated=false
InputWeaver acceptance exit_code=0
"@
        [IO.File]::WriteAllText($mainConsole, $consoleFixture, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($startupConsole, $consoleFixture, [Text.UTF8Encoding]::new($false))
        [void](Invoke-AcceptanceValidation $mainLog $mainConsole $startupLog $startupConsole)
        if ($script:Failures.Count -ne 0) {
            throw "The positive acceptance-validator fixture failed: $($script:Failures -join '; ')"
        }

        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence 500 67 "Down" 2))
        ++$injectionSequence
        [void]$mainRecords.Add((New-FixtureInjection $injectionSequence 501 67 "Up" 2))
        Write-FixtureJsonl $mainLog @($mainRecords)
        $script:Failures.Clear()
        [void](Invoke-AcceptanceValidation $mainLog $mainConsole $startupLog $startupConsole)
        if (@($script:Failures | Where-Object { $_ -like "*stale C output*" }).Count -eq 0) {
            throw "The negative acceptance-validator fixture did not reject stale C output."
        }
    } finally {
        if (Test-Path -LiteralPath $temporaryRoot -PathType Container) {
            [IO.Directory]::Delete($temporaryRoot, $true)
        }
    }
    $script:Failures.Clear()
    Write-Host "Phase 4 acceptance validator self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($AcceptanceLog)) {
    $AcceptanceLog = Join-Path $PSScriptRoot "acceptance.jsonl"
}
if ([string]::IsNullOrWhiteSpace($AcceptanceConsole)) {
    $AcceptanceConsole = Join-Path $PSScriptRoot "acceptance.txt"
}
if ([string]::IsNullOrWhiteSpace($StartupHeldLog)) {
    $StartupHeldLog = Join-Path $PSScriptRoot "startup-held.jsonl"
}
if ([string]::IsNullOrWhiteSpace($StartupHeldConsole)) {
    $StartupHeldConsole = Join-Path $PSScriptRoot "startup-held.txt"
}

$summary = Invoke-AcceptanceValidation $AcceptanceLog $AcceptanceConsole $StartupHeldLog $StartupHeldConsole
if ($script:Failures.Count -ne 0) {
    Write-Host "Phase 4 Windows acceptance verification failed:"
    foreach ($failure in $script:Failures) {
        Write-Host "  - $failure"
    }
    exit 1
}

Write-Host "Phase 4 Windows acceptance evidence passed."
Write-Host "Observed JSONL hook maxima: main=$($summary.MainMaximumHookMicroseconds) us startup-held=$($summary.StartupMaximumHookMicroseconds) us."
exit 0

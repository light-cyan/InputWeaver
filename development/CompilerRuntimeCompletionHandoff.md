# Compiler and Runtime Completion Handoff

## Scope

This document defines the delivered integration interface at the compiler and runtime completion stage. The supported product path is a persistent file workflow from Weave source to Windows execution.

```text
.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe -> Windows input
```

The compiler and executor are independent command-line programs. A `.weavec` file is the complete handoff between them and can be stored and executed later; the executor structurally validates it during load.

## Delivered artifacts

- `bin/InputWeaverCompiler.exe` validates and compiles Weave source files.
- `bin/InputWeaver.exe` loads and executes a compiled program on Windows.
- `.weave` is the source format defined by `docs/language/`.
- `.weavec` is the persistent compiled-program format shared by the compiler and executor.

## Compiler interface

```text
InputWeaverCompiler.exe compile <source.weave> [destination.weavec]
InputWeaverCompiler.exe validate <source.weave>
InputWeaverCompiler.exe dump <source.weave>
```

- `compile` validates the source and writes a `.weavec` artifact. When the destination is omitted, the source extension is replaced with `.weavec`.
- `validate` checks the source without writing an artifact.
- `dump` prints the deterministic compiled representation without writing an artifact.
- Compiler failures report source locations and return a nonzero exit code.

## Executor interface

```text
InputWeaver.exe --program <file.weavec> [--target <exe-name-or-absolute-path> | --target-global] [--allow-exec] [--log <jsonl-path>] [--trace-input] [--debug-session <opaque-token>]
```

- The executor requires one `.weavec` file and executes it against the compiled target by default. `--target` overrides it with an executable selector, while `--target-global` overrides it with global execution; the two command-line overrides are mutually exclusive. The effective selection governs process discovery, runtime rule routing, and output eligibility for the whole invocation.
- A target may be an executable name or an absolute executable path. Target-scoped execution waits for one matching process and becomes active only while that process owns the foreground window.
- `TARGET = GLOBAL` selects global execution in source. When neither the compiled program nor the command line supplies a target, mapping execution is rejected before input hooks are installed.
- `--allow-exec` grants process-launch authority for the current compiled-program invocation. Programs requiring that authority are rejected during activation when it is not granted.
- `--log` writes bounded operational diagnostics as JSONL. Launch failures expose both a portable `launch_result` and the captured numeric `platform_error`. `--trace-input` additionally includes physical input records and requires `--log`.
- `--debug-session` enables the local input-debug endpoint for this executor. The token must contain 1 to 64 ASCII letters, digits, periods, hyphens, or underscores; the client connects to `\\.\pipe\InputWeaver.Debug.<pid>.<token>` and must run as the same Windows user. The endpoint accepts one client, requires protocol negotiation, and does not capture until the client sends `StartCapture`; `StopCapture` ends capture and `RequestExecutorStop` requests orderly executor shutdown. Without this option, the executor creates no debug queue, pipe, or debug thread. The protocol contract is recorded in `development/legacy/phase-7-input-debug-producer/TargetDataContract.md`.
- The compiled exit rules stop the active executor. A source with no `exit` statement receives the compiled default of physical left-or-right Ctrl plus left-or-right Shift and `F12:down`.

## Weave interface

The implemented language supports target declarations, constants, typed user variables, full mappings, event rules, exit rules, optional PAUSE rules, conditions, continuing rule matching, numeric and duration expressions, `if`, `repeat`, `while`, `press`, `release`, `tap`, `wait`, action intervals, raw Windows control identifiers, keyboard controls, mouse controls, and `exec`.

`docs/language/grammar.v1.md` defines the base source language, while `docs/language/grammar.v2.md` and `docs/language/grammar.v2.ebnf` define the current control-name extension. `InputWeaverCompiler.exe validate` is the canonical command for checking a source file against that contract.

## Runtime contract

- Temporary foreground loss cancels target-bound mappings and pending actions, releases owned outputs, and permits fresh input after the target returns.
- A source held across foreground return must be released before a new physical press can create another mapping.
- Activated keyboard state is initialized when execution starts so a key already held at startup does not create a new mapping; compiled exit conditions can observe activated modifiers that were already held at startup.
- Exit rules are evaluated for physical candidate input before target eligibility and PAUSE routing. A matching rule consumes its event and starts orderly runtime cancellation and shutdown.
- A program with no PAUSE rule has no compiled PAUSE control table and bypasses PAUSE control lookup while the built-in `PAUSE` state remains `on`.
- Dispatch, task progress, scheduling, output publication, and diagnostic transport are bounded so input processing remains responsive under accepted programs.
- Raw Windows outputs preserve their authored virtual-key, scan-code, extended-scan, or mouse identity; unsupported output identities are rejected during activation.
- Keyboard and mouse output is published through the Windows input system. The active keyboard layout, IME, Caps Lock, and physically held modifiers therefore affect the receiving application.
- Process launch is denied by default and becomes available only through the explicit executor authority described above.
- Current fixed capacities, task budgets, logging limits, and observable console statistics are recorded in `docs/runtime-boundaries.md`.

## Operating requirements

- The delivered executor runs on Windows.
- InputWeaver and its target should run at the same Windows integrity level so hooks and generated input are permitted.
- A target selector should resolve to one process; the executor reports candidates while the selector is ambiguous.

## Canonical workflow

Run commands from the repository root in `cmd.exe`.

```bat
bin\InputWeaverCompiler.exe compile path\program.weave path\program.weavec
bin\InputWeaver.exe --program path\program.weavec --target target.exe
```

Keyboard-safety operation guidance is `docs/safety-guide.md`, and current runtime limits and console fields are recorded in `docs/runtime-boundaries.md`.

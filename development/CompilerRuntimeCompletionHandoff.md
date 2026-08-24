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
InputWeaver.exe --program <file.weavec> [--target <exe-name-or-absolute-path> | --target-global] [--allow-exec] [--log <jsonl-path>] [--trace-input]
```

- The executor requires one `.weavec` file and executes it against the compiled target by default. `--target` overrides it with an executable selector, while `--target-global` overrides it with global execution; the two command-line overrides are mutually exclusive. The effective selection governs process discovery, runtime rule routing, and output eligibility for the whole invocation.
- A target may be an executable name or an absolute executable path. Target-scoped execution waits for one matching process and becomes active only while that process owns the foreground window.
- `TARGET = GLOBAL` selects global execution in source. When neither the compiled program nor the command line supplies a target, mapping execution is rejected before input hooks are installed.
- `--allow-exec` grants process-launch authority for the current compiled-program invocation. Programs requiring that authority are rejected during activation when it is not granted.
- `--log` writes bounded operational diagnostics as JSONL. `--trace-input` additionally includes physical input records and requires `--log`.
- Physical Ctrl+Shift+F12 stops the active executor.

## Weave interface

The implemented language supports target declarations, constants, typed user variables, full mappings, event rules, PAUSE rules, conditions, continuing rule matching, numeric and duration expressions, `if`, `repeat`, `while`, `press`, `release`, `tap`, `wait`, action intervals, raw Windows control identifiers, keyboard controls, mouse controls, and `exec`.

`docs/language/grammar.v1.md` defines the base source language, while `docs/language/grammar.v2.md` and `docs/language/grammar.v2.ebnf` define the current control-name extension. `InputWeaverCompiler.exe validate` is the canonical command for checking a source file against that contract.

## Runtime contract

- Temporary foreground loss cancels target-bound mappings and pending actions, releases owned outputs, and permits fresh input after the target returns.
- A source held across foreground return must be released before a new physical press can create another mapping.
- Activated keyboard state is initialized when execution starts so a key already held at startup does not create a new mapping; physical force stop remains available from startup-held modifiers.
- Dispatch, task progress, scheduling, output publication, and diagnostic transport are bounded so input processing remains responsive under accepted programs.
- Keyboard and mouse output is published through the Windows input system. The active keyboard layout, IME, Caps Lock, and physically held modifiers therefore affect the receiving application.
- Process launch is denied by default and becomes available only through the explicit executor authority described above.
- Current fixed capacities, task budgets, logging limits, and observable console statistics are recorded in `docs/runtime-boundaries.md`.

## Operating requirements

- The delivered executor runs on Windows.
- InputWeaver and its target should run at the same Windows integrity level so hooks and generated input are permitted.
- A target selector should resolve to one process; the executor reports candidates while the selector is ambiguous.
- Example capture commands overwrite their own JSONL and console evidence files when the same mode is run again.

## Canonical workflow

Run commands from the repository root in `cmd.exe`.

```bat
script\build_compiler_tests.bat
script\build.bat
example\notepad-showcase\compile.bat
example\notepad-showcase\run.bat
```

The showcase procedure is `example/notepad-showcase/ManualTest.md`. Keyboard-safety operation guidance is `docs/safety-guide.md`, runtime limits are recorded in `docs/runtime-boundaries.md`, and the retained physical Windows procedure and evidence are under `example/phase-4-safety/`.

## Verification interface

- `script/verify_phase4.bat` is the current combined build, test, analysis, dependency, command-line authority, example reproducibility, retained-evidence validation, documentation, and diff gate.
- `example/phase-4-safety/verify.bat` verifies the retained physical Windows JSONL and console evidence after the operator completes the visible-window procedure.

# Current Implementation Handoff

## Outcome

InputWeaver currently provides a complete file-based command-line path from a Weave source file to a live Windows input macro: `.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe`.

The compiler and executor are separate programs. They do not call each other and do not exchange an in-memory program. The `.weavec` artifact is the persistent and independently loadable boundary between them, and there is no combined compile-and-run command.

## Command-line artifacts

- `bin/InputWeaverCompiler.exe` is the compiler frontend.
- `bin/InputWeaver.exe` is the Windows executor and diagnostic observer.

## Compiler commands

```text
InputWeaverCompiler.exe compile <source.weave> [destination.weavec]
InputWeaverCompiler.exe validate <source.weave>
InputWeaverCompiler.exe dump <source.weave>
```

- `compile` loads, parses, binds, type-checks, lowers, validates, and writes a `.weavec` artifact. When the destination is omitted, the source extension is replaced with `.weavec`.
- `validate` runs the compiler checks without writing an artifact.
- `dump` prints the deterministic lowered program representation without writing an artifact.
- Compile diagnostics include source locations and return a nonzero process exit code when compilation fails.

## Executor modes

```text
InputWeaver.exe --program <file.weavec> [--target <override>] [--log <jsonl-path>] [--trace-input]
InputWeaver.exe [--log <jsonl-path>] [--trace-input]
InputWeaver.exe --test-rules --target <exe-name-or-absolute-path> [--log <jsonl-path>] [--trace-input]
```

- Compiled-program mode decodes and structurally validates a `.weavec` artifact, resolves its configured target or the `--target` override, installs the Windows input hooks, and executes the compiled mappings while the target is eligible.
- Observer mode installs the Windows hooks and reports activity without suppressing physical input or generating output.
- Fixed-rule mode retains the Phase 1 F6/F7/F9/middle-button integration rules as a Windows regression path.
- `--log` writes bounded diagnostics as JSONL. `--trace-input` adds physical hook records and requires `--log`.
- Physical Ctrl+Shift+F12 stops any active mode.

## Implemented Weave path

The compiled-program path supports target selection, full mappings, event rules, conditions, continuing rule matching, user state, numeric and duration expressions, `if`, `repeat`, `while`, `press`, `release`, `tap`, `wait`, action intervals, `PAUSE`, raw Windows control identifiers, keyboard and mouse controls, and `exec`.

The current source-language contract is under `docs/language/`. The shared compiled representation, canonical encoding, decoding, and structural validation are under `src/program/`. The platform-independent execution engine is under `src/runtime/`, and the active Windows assembly is under `src/platform/windows/`.

## Canonical workflow

Run these commands from the repository root in `cmd.exe`:

```bat
script\build_compiler_tests.bat
script\build.bat
bin\InputWeaverCompiler.exe validate example\notepad-showcase.weave
bin\InputWeaverCompiler.exe compile example\notepad-showcase.weave example\notepad-showcase.weavec
bin\InputWeaver.exe --program example\notepad-showcase.weavec --log example\acceptance.jsonl --trace-input
```

The example also provides `example/compile.bat`, `example/run.bat`, and a Chinese manual acceptance guide in `example/README.md`.

## Verification

`script/verify_phase3.bat` is the comprehensive gate. It builds both executables and all compiler, program, runtime, and Windows adapter tests; runs the tests; performs strict compiler and runtime analysis; audits subsystem dependencies; and checks the working diff.

The full gate passed on 2026-08-24 after the compiled-program Windows session was integrated.

The retained Notepad acceptance log at `example/acceptance.jsonl` contains 1,428 records: 1,197 contiguous hook records, 82 contiguous injection records, and 149 runtime records. All 82 requested inputs were sent, every generated key-down had a matching key-up, and the log contains no injection failure, cancellation cleanup failure, runtime fault, dropped diagnostic record, or JSONL truncation. The maximum recorded hook processing time was 80 microseconds. The log also records the expected `PAUSE` cancellation generations and the physical force-stop shutdown path.

## Operational constraints

- Target-scoped mappings are active only while the selected process owns the foreground window.
- The target and InputWeaver should run at the same Windows integrity level so hooks and `SendInput` are permitted.
- Text results depend on the active keyboard layout, IME state, modifier state, and Caps Lock state because output is emitted as input controls rather than pasted text.
- `SendInput` is system-wide rather than PID-addressed; foreground changes during an asynchronous action can redirect later output from that action.
- The example overwrites `example/acceptance.jsonl` when it starts, so preserve a completed run before launching it again.
- `exec` launch failures are reported, while a successful launch is confirmed by observing the launched process or window.

## Archive

Completed Phase 2 and Phase 3 engineering records are preserved under `development/legacy/phase-2/`, `development/legacy/phase-3-compiler/`, and `development/legacy/phase-3-runtime/`. These directories are historical snapshots rather than current implementation inputs, and their internal references retain the original pre-archive paths.

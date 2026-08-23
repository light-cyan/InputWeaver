# Phase 3 Runtime Verification

## Result

The Phase 3 runtime implementation gate passes on 2026-08-23 from reviewed base commit `dad2f4bb4fe0f7d7b78b777b1ab4f2087d1334ba`. The implementation loads validated `.weavec` artifacts, activates immutable programs transactionally, executes the frozen runtime semantics with bounded state and cooperative scheduling, and supplies the required Windows control, routing, output, and process-launch adapters.

## Implemented files

- `src/runtime/runtime_types.hpp` defines runtime values, activation and capacity results, cancellation, diagnostics, metrics, input and output records, and platform port interfaces.
- `src/runtime/expression_vm.*` implements the typed preallocated expression VM, physical and variable reads, branches, operators, finite arithmetic, duration arithmetic, and bounded evaluation faults.
- `src/runtime/program_runtime.*` implements transactional activation, pause and variable locking, physical state, dispatch transactions, mappings, the action VM, the cooperative scheduler, cancellation generations, output ownership, routing, and bounded diagnostics.
- `src/runtime/artifact_loader.*` implements bounded file loading through the shared `.weavec` decoder and transactional activation while retaining the prior active program on failure.
- `src/platform/windows/runtime_control_catalog.*` implements the activated Windows control catalog, native input normalization, force-stop recognition, runtime output conversion, and the bounded `ActionBatch` queue adapter.
- `src/platform/windows/runtime_route_adapter.*` implements retained-target liveness, foreground, pointer, dispatch, and injection checks.
- `src/platform/windows/runtime_process_launcher.*` implements strict command decoding, deterministic executable resolution, the exact native creation contract, cancellation checks, handle release, and bounded failure results.
- `src/platform/windows/input_injector.cpp` supports virtual-key injection when a published Windows virtual key has no scan-code mapping.
- `tests/runtime/program_runtime_tests.cpp` and `tests/runtime/windows_runtime_adapter_tests.cpp` provide deterministic core, capacity, safety, Windows catalog, routing, process, and regression coverage.
- `script/build_runtime_tests.bat`, `script/build.bat`, and `script/test.bat` provide the focused and canonical runtime build and test commands.

## Strict build and tests

`cmd /c script\build.bat` completed successfully with MinGW-w64 G++ 15.1.0 under C++20 and `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`. It built `InputWeaver.exe`, `InputWeaverTests.exe`, `CompiledProgramTests.exe`, `ProgramRuntimeTests.exe`, and `WindowsRuntimeAdapterTests.exe`.

`cmd /c script\test.bat` completed successfully with the following results:

```text
All Phase 1 automated tests passed.
All CompiledProgram contract tests passed.
All Phase 3 runtime core tests passed.
All Windows runtime adapter tests passed.
```

## Runtime coverage

- The frozen tap, complete mapping, conditional repeat, and pause-control fixtures execute deterministically with fake time, fake output, and no compiler dependency.
- Valid artifact loading, malformed artifact rejection, pre-decode size limits, missing files, and failed replacement retention have direct tests.
- Activation tests cover control, value, mapping, expression stack, repeat frame, ownership, task, transaction, diagnostic, process policy, target, identity, and capability rejection before publication.
- Every expression opcode, unary operator, binary operator, value domain, built-in value, physical-state read, forward branch, and typed return executes through the runtime VM; division, modulo, non-finite number, duration overflow, predicate, and capacity faults have direct checks.
- Every action opcode executes through the cooperative action VM, including adjacent instructions, timed tap, wait, gap, set, toggle, exec, forward branches, repeat frames, yielded back edges, and end.
- Dispatch tests cover source order, consume and observe delivery, continue and stop flow, empty actions, repeated physical input, injected-input bypass, pause first-match behavior, and synchronous pause cancellation.
- Transaction tests cover static acceptance, precommit task exhaustion, fail-open forwarding, no partial task or mapping commit, and bounded visible diagnostics.
- Ownership tests cover repeated acquisition, unowned release, task overlap, mapping and task overlap, mapping repeat, cancellation cleanup, failed-release retry, reload cleanup gating, output failure, and single zero-to-one and one-to-zero native edges.
- Cancellation tests cover pause, reload, target and routing loss, fatal failure, force stop, shutdown, timed tasks, mappings, owned output, and process launch before resolution, before creation, and after creation.
- The production scheduler test verifies one cooperative task thread and cancellable timed wakeup behavior.

## Windows coverage

- Every published portable keyboard, mouse-button, and consumer usage in the current language contract binds with its required capabilities, normalizes native input to its activated strong ID, and converts output down and up to native input records.
- Windows virtual-key, layout-sensitive, normal scan-code, E0, and E1 identities have direct recipe and ambiguity tests.
- Physical Ctrl-Shift-F12 force stop remains available independently of active program bindings, and self-injected or external injected events bypass user rules.
- Quoted paths with spaces, absolute paths, bare-name search, explicit command-interpreter invocation, missing executables, native creation failure, exact `CreateProcessW` parameters, child working directory, immediate return, and cancellation immediately before native creation have direct tests.
- The frozen Phase 1 runtime and injection tests remain passing after the Windows adapter changes.

## Static analysis and dependency audit

The following translation units passed GCC `-fanalyzer` independently under the strict warning policy:

```text
src/runtime/artifact_loader.cpp
src/runtime/expression_vm.cpp
src/runtime/program_runtime.cpp
src/platform/windows/runtime_control_catalog.cpp
src/platform/windows/runtime_process_launcher.cpp
src/platform/windows/runtime_route_adapter.cpp
src/platform/windows/input_injector.cpp
```

Compiler-generated dependency output for all six new Phase 3 runtime translation units contains no dependency on `src/compiler/`, `src/app/`, or `src/tui/`. Runtime-owned files contain no Windows platform include, and Windows runtime adapters contain no compiler, app, or TUI include.

`git diff --check` passes.

## Shared contract

`src/program/` and `tests/program/` remain byte-for-byte at the reviewed base commit. Runtime consumes that immutable `CompiledProgram` and `.weavec` contract directly. `docs/language/grammar.v1.md` records the user-facing Windows executable lookup and child-working-directory clarification without changing the compiled-program contract.

## Executable-resolution decision

`development/phase-3-runtime/WindowsExecutableResolution.md` owns the selected Windows `Exec` policy, implementation boundaries, authoritative API references, and deterministic tests.

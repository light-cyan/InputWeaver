# UniversalKeyRemapper

## Project Overview

UniversalKeyRemapper is a planned Windows key and mouse remapping utility written in C++. It is intended to run with administrator privileges when required by a target application and to read user-defined `.krm` mapping configurations. The original mapping-language design, including key states, actions, sequences, variables, and mouse events, is retained in `legacy/OriginalDesign.md` as historical reference material.

The repository also retains `legacy/MouseHookPrototype.cpp`, an unsuccessful exploratory mouse-hook prototype that is not part of the application implementation. Current directories are `src/app/` for runtime orchestration, remapping, and action scheduling, `src/core/` for platform-independent runtime data and rules, `src/platform/windows/` for Win32 integration, `src/diagnostics/` for logging, `tests/` for automated tests, `script/` for build and run batch files, `res/` for runtime resources, and `bin/` for generated executables.

## Environment

- Platform: Windows.
- Compiler: MinGW-w64 GCC and G++ 15.1.0 are available on `PATH`.
- Language: C++.
- Dependencies: use only the C++ standard library and the Windows API; do not introduce third-party libraries.
- Git: this repository is initialized for Git-based version control. Inspect `git status` before changing files and do not commit or push unless explicitly requested.

## Build and Verification

- Keep the canonical build and run commands in batch files under `script/` when the application source is added.
- Place generated executables and other build output under `bin/` and keep them out of version control.
- Compile with `g++` and verify a successful build after changing C++ source code.
- Use `legacy/OriginalDesign.md` as historical reference for the `.krm` language; establish a separate current specification when implementation begins.

## Agent Coordination

- `AGENTS.md` is the persistent agent-to-agent shared message channel for all agents working on this repository.
- Read this file before starting work and use it to preserve repository-wide decisions, active implementation status, completion gates, and handoff information for later agents.
- Add concise durable handoff notes under `Shared Agent Messages` and preserve unresolved notes written by other agents.
- Keep detailed design and verification records in their linked documents; keep the shared status here concise and authoritative.
- Update a phase status when work starts or its completion gate is satisfied, but do not mark partially implemented or unverified work as complete.

## Implementation Status

### Phase 1: Input Loop and Self-Injection Isolation

- Status: `Complete` (2026-08-19).
- Plan: `docs/phase-1/ImplementationPlan.md`.
- Code design: `docs/phase-1/CodeDesign.md`.
- Verification: `docs/phase-1/Verification.md`.
- Scope: low-level keyboard and mouse hooks, origin classification, self-tagged `SendInput`, recursion prevention, executable-based target discovery, foreground-scoped fixed test rules, bounded operational logging, opt-in input tracing, and Phase 1 verification without the `.krm` parser.

## Shared Agent Messages

- 2026-08-19 | Phase 1 design | Commit `3aa8c3e` records `docs/phase-1/ImplementationPlan.md` and `docs/phase-1/CodeDesign.md` as the implementation boundary, architecture, verification requirements, and completion gate.
- 2026-08-19 | Phase 1 implementation | Source implementation started after design commit `3aa8c3e`.
- 2026-08-19 | Phase 1 CLI boundary | `UniversalKeyRemapper.exe` is a console application. Observer mode accepts logging options, while `--test-rules --target <exe-name-or-absolute-path>` activates the fixed Phase 1 rules only when the uniquely resolved target owns the foreground window.
- 2026-08-19 | Phase 1 diagnostics | `--log <jsonl-path>` records bounded operational events by default. `--trace-input` is an explicit expansion that includes redacted normalized input and aggregated mouse movement.
- 2026-08-19 | Phase 1 interactive verification | The current 32-bit tag produced 28 real injected mouse hook events classified as `SelfInjected` and `SelfTag`, with no external classification or recursive rule activation. All 43 recorded injection batches completed without failure or cancellation.
- 2026-08-19 | Phase 1 CLI implementation | The console build locates targets by executable basename or absolute path, retains and revalidates process identity, waits for target start or restart, filters routine input from operational logs, and uses a 32-bit self tag for keyboard and mouse round trips. The canonical strict build and automated tests pass.
- 2026-08-19 | Console termination | `Ctrl+C` is consumed without stopping the process. Physical `Ctrl+Shift+F12` is the interactive stop chord.
- 2026-08-19 | Phase 1 completion | Low-level hooks, remap decisions, action scheduling, and application lifecycle are separated into cohesive modules. The strict canonical build, automated suite, `-fanalyzer`, and real 32-bit keyboard and mouse self-tag round trip pass; Phase 1 is complete.
- 2026-08-20 | Mapping language v1 | `grammar.v1.md` defines the current `.krm` v1 syntax and runtime semantics, and `ImplementationMethod.v1.md` defines its compiler, dispatcher, cooperative task scheduler, cancellation, and output-ownership model. The original drafts and v0 specification are retained with the v1 documents.

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

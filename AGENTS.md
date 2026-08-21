# InputWeaver

context-aware input mapping and macro engine

## Project Overview

InputWeaver is a planned Windows key and mouse remapping utility written in C++. It is intended to run with administrator privileges when required by a target application and to read user-defined `.weave` source files. The original mapping-language design, including key states, actions, sequences, variables, and mouse events, is retained in `development/legacy/OriginalDesign.md` as historical reference material.

The repository also retains `development/legacy/MouseHookPrototype.cpp`, an unsuccessful exploratory mouse-hook prototype that is not part of the application implementation. Current directories are `src/app/` for runtime orchestration, remapping, and action scheduling, `src/core/` for platform-independent runtime data and rules, `src/platform/windows/` for Win32 integration, `src/diagnostics/` for logging, `tests/` for automated tests, `script/` for build and run batch files, `res/` for runtime resources, and `bin/` for generated executables.

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
- Use `development/legacy/OriginalDesign.md` as historical reference for the predecessor `.krm` design and `docs/language/grammar.v1.md` as the current Weave specification.

## Agent Coordination

- `AGENTS.md` is the persistent agent-to-agent shared message channel for all agents working on this repository.
- Read this file before starting work and use it to preserve repository-wide decisions, active implementation status, completion gates, and handoff information for later agents.
- Add concise durable handoff notes under `Shared Agent Messages` and preserve unresolved notes written by other agents.
- Keep detailed design and verification records in their linked documents; keep the shared status here concise and authoritative.
- Update a phase status when work starts or its completion gate is satisfied, but do not mark partially implemented or unverified work as complete.

## Dependencies

The project's final recommended dependencies can be simplified as:

```
    tui
     |
    app
  /     \
compiler runtime
    \   /
     core
      |
platform-independent contracts

src/platform/windows -> implements app/runtime platform ports
src/diagnostics      -> shared reporting infrastructure
src/support          -> domain-independent primitives only
```


## Implementation Status

### Phase 1: Input Loop and Self-Injection Isolation

- Status: `Complete` (2026-08-19).
- Plan: `development/phase-1/ImplementationPlan.md`.
- Code design: `development/phase-1/CodeDesign.md`.
- Verification: `development/phase-1/Verification.md`.
- Scope: low-level keyboard and mouse hooks, origin classification, self-tagged `SendInput`, recursion prevention, executable-based target discovery, foreground-scoped fixed test rules, bounded operational logging, opt-in input tracing, and Phase 1 verification without the Weave compiler.

### Phase 2: CompiledProgram Contract

- Status: `Complete; successor branch point ready` (2026-08-21).
- Plan: `development/phase-2/ImplementationPlan.md`.
- Compiled program design: `development/phase-2/CompiledProgramDesign.md`.
- Verification: `development/phase-2/Verification.md`.
- Scope: complete platform-independent immutable compiler-to-runtime data model, builder and finalizer, canonical pool remapping, bounded structural validation, exact resource requirement derivation, deterministic program dumps, required fixtures, and contract tests.

### Compiler Successor Phase

- Status: `Planned; not started`.
- Plan: `development/phase-3-compiler/ImplementationPlan.md`.
- Scope: complete Weave v1 source loading, diagnostics, lexer, parser, binding, type checking, lowering, and compiler-facing commands against the frozen Phase 2 contract.

### Runtime Successor Phase

- Status: `Planned; not started`.
- Plan: `development/phase-3-runtime/ImplementationPlan.md`.
- Scope: activation, expression VM, dispatcher, action VM, scheduler, mappings, ownership, cancellation, diagnostics, and Windows adapters against frozen Phase 2 fixtures.

## Active Design Issues

- Register: `development/OpenDesignIssues.md`.
- Current gates: stable event snapshots, shared Weave v1 control identities and Windows output recipes, native executable resolution for `exec`, and the persistent compiled-program artifact boundary.

## Shared Agent Messages

- 2026-08-19 | Phase 1 design | Commit `3aa8c3e` records `docs/phase-1/ImplementationPlan.md` and `docs/phase-1/CodeDesign.md` as the implementation boundary, architecture, verification requirements, and completion gate.
- 2026-08-19 | Phase 1 implementation | Source implementation started after design commit `3aa8c3e`.
- 2026-08-19 | Phase 1 CLI boundary | `InputWeaver.exe` is a console application. Observer mode accepts logging options, while `--test-rules --target <exe-name-or-absolute-path>` activates the fixed Phase 1 rules only when the uniquely resolved target owns the foreground window.
- 2026-08-19 | Phase 1 diagnostics | `--log <jsonl-path>` records bounded operational events by default. `--trace-input` is an explicit expansion that includes redacted normalized input and aggregated mouse movement.
- 2026-08-19 | Phase 1 interactive verification | The current 32-bit tag produced 28 real injected mouse hook events classified as `SelfInjected` and `SelfTag`, with no external classification or recursive rule activation. All 43 recorded injection batches completed without failure or cancellation.
- 2026-08-19 | Phase 1 CLI implementation | The console build locates targets by executable basename or absolute path, retains and revalidates process identity, waits for target start or restart, filters routine input from operational logs, and uses a 32-bit self tag for keyboard and mouse round trips. The canonical strict build and automated tests pass.
- 2026-08-19 | Console termination | `Ctrl+C` is consumed without stopping the process. Physical `Ctrl+Shift+F12` is the interactive stop chord.
- 2026-08-19 | Phase 1 completion | Low-level hooks, remap decisions, action scheduling, and application lifecycle are separated into cohesive modules. The strict canonical build, automated suite, `-fanalyzer`, and real 32-bit keyboard and mouse self-tag round trip pass; Phase 1 is complete.
- 2026-08-20 | Mapping language v1 | `docs/language/grammar.v1.md` defines the current Weave v1 syntax and user-visible semantics for `.weave` source files. `development/phase-2/CompiledProgramDesign.md` owns the exact immutable representation, and the two successor plans own complete compiler and runtime implementation and verification scope.
- 2026-08-20 | Post-Phase 1 hardening | A full action queue remains fail-open and now signals a one-per-runtime console error while preserving every rejection in metrics and optional JSONL diagnostics. Core input and rule data use standard platform-independent types; Win32 input and point conversion occurs at the Windows adapter boundary. The strict build, automated suite, and `-fanalyzer` pass, and `development/after-phase-1-and-grammar-design/ResearchReport.md` records the current assessment and architecture research.
- 2026-08-20 | Product identity | The project name is `InputWeaver`, with the subtitle `context-aware input mapping and macro engine`. The application artifact is `InputWeaver.exe`.
- 2026-08-20 | Language identity | The configuration language is `Weave`, and `.weave` is its sole source-file extension.
- 2026-08-21 | Weave v2 control identity | `docs/language/grammar.v2.md` defines the v2 language increment for portable, platform-qualified, and raw discrete control references; a namespaced `ControlId`; deterministic aliases; backend capability checks; and platform-bound native recipes. The current Phase 2 and successor implementation contracts remain on Weave v1.
- 2026-08-20 | Internal identity | C++ code uses the `inputweaver` namespace, and build-script variables use the `INPUTWEAVER_` prefix.
- 2026-08-20 | Phase 2 CompiledProgram implementation | `src/core/compiled_program.*`, `program_validator.*`, and `program_dump.*` implement the complete immutable contract. Four required fixtures, full opcode and operator construction coverage, golden dumps, corruption rejection, strict builds, automated tests, core `-fanalyzer`, and platform isolation pass; `development/phase-2/Verification.md` records the evidence.
- 2026-08-20 | Successor phase split | Compiler and runtime development begin only from the reviewed Phase 2 completion commit. Their independent plans are `development/phase-3-compiler/ImplementationPlan.md` and `development/phase-3-runtime/ImplementationPlan.md`; neither successor synchronizes progress or branch changes with the other, and a future integration phase combines their completed artifacts.
- 2026-08-21 | Successor plan authority | The compiler and runtime successor plans explicitly depend on `docs/language/grammar.v1.md`, `development/phase-2/CompiledProgramDesign.md`, the frozen Phase 2 core implementation, and `development/OpenDesignIssues.md`. They are complete phase plans rather than partial implementation steps.
- 2026-08-21 | Runtime cancellation safety | Cancellation epochs remain required because queue clearing cannot invalidate executing or timed tasks, committed mappings, racing producers, or output ownership.
- 2026-08-21 | Pause-control channel | Weave v1 uses `pause event [when condition] =>|~> on|off|toggle;`. The compiler lowers these statements into dedicated pause-control buckets and rules; runtime evaluates physical candidates through that channel before the ordinary pause guard and applies the first match synchronously without creating a task.
- 2026-08-21 | Process launch contract | `Exec` retains only its authored command in `CompiledProgram`. The platform launcher resolves the executable and uses its containing directory as the child working directory, independently of the InputWeaver location, source-file location, and parent working directory.

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

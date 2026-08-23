# InputWeaver

context-aware input mapping and macro engine

## Project Overview

InputWeaver is a C++ context-aware input mapping and macro engine. Weave source files use the `.weave` extension, compiled programs use the `.weavec` extension, and the Windows application artifact is `InputWeaver.exe`.

The compiler and runtime are independent subsystems. The compiler transforms `.weave` source into a `.weavec` file, the runtime loads that file and executes it, and the application coordinates those operations without passing an in-memory compiled program between the two subsystems.

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
- Use the specifications under `docs/language/` for current Weave behavior.

## Agent Coordination

- `AGENTS.md` contains stable repository rules, the dependency model, the source layout, and concise pointers to current development work.
- `docs/language/` owns Weave source-language definitions.
- `development/` outside `development/legacy/` owns current designs, phase plans, verification evidence, research, and open design issues.
- `development/legacy/` contains archived material from past work. It is not a current requirement or development input and does not need to be read unless the user explicitly requests historical comparison.
- Read the relevant language specification, compiled-program design, phase plan, and open-issue register before changing an owned subsystem.
- Record a new decision in its owning document instead of duplicating phase history or handoff logs in `AGENTS.md`.

## Dependency Model

The application is the control plane. It tells the compiler which `.weave` source to compile and tells the runtime which `.weavec` file to load, start, reload, or stop. The compiler and runtime never call each other and never exchange an in-memory `CompiledProgram`.

```text
Control flow:

tui -> app -> compiler
           -> runtime

Program data:

.weave -> compiler -> .weavec -> runtime

Code dependencies:

compiler -> program
runtime  -> program + input
program  -> input control types
platform/windows -> app and runtime interfaces + input contracts
app, compiler, runtime, platform/windows, and tui -> diagnostics as needed
all modules -> support only for domain-independent primitives
```

`program` is the shared definition of the compiled artifact, not a call path between compiler and runtime. `app` passes file paths and user commands, while `runtime` owns artifact loading and executable state.

## File Layout

- `src/app/` owns application use cases, compile/run/reload/stop coordination, configuration selection, and status exposed to user interfaces. It contains no parser, virtual machine, hook, injector, or platform handle.
- `src/compiler/` owns Weave source loading, lexical analysis, parsing, semantic binding, type checking, lowering, compile diagnostics, and `.weavec` emission.
- `src/program/` owns `CompiledProgram`, canonicalization, structural validation, deterministic dumps, and the shared `.weavec` encoding contract.
- `src/input/` owns platform-independent live input and output control types, normalized events, transitions, origins, decisions, and bounded output batch records.
- `src/runtime/` owns platform-independent program activation, physical state, variable and `PAUSE` state, dispatch, expression evaluation, mappings, action execution, task scheduling, cancellation, output ownership, and runtime port interfaces.
- `src/platform/windows/` owns the Windows executable entry point, current Windows runtime-session assembly, hooks, native input normalization, `SendInput` injection, process discovery and validation, process launch, and implementations of application and runtime platform interfaces.
- `src/tui/` owns terminal rendering and user interaction and communicates with compiler and runtime only through `app`.
- `src/diagnostics/` owns bounded diagnostic transport, formatting, and reporting sinks; domain decisions remain in the producing subsystem.
- `src/support/` owns primitives that are independent of Weave, compiled programs, input devices, runtime execution, application policy, and operating systems.
- `tests/program/`, `tests/compiler/`, `tests/runtime/`, and `tests/app/` mirror the corresponding source-module boundaries; platform integration tests remain explicitly Windows-scoped.
- `docs/language/` contains user-visible Weave language definitions; `development/` contains current engineering documents; `development/legacy/` contains archived material that is outside current development.
- `script/` contains canonical build, test, and run commands; `res/` contains Windows resources; `bin/` contains ignored generated artifacts.

## Current Development

- Implement the compiler independently according to `development/phase-3-compiler/ImplementationPlan.md`.
- Implement the runtime independently according to `development/phase-3-runtime/ImplementationPlan.md`.
- Keep `development/OpenDesignIssues.md` open for decisions that block affected implementation gates; `ODI-004` is resolved only when native `exec` execution is implemented and verified.

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

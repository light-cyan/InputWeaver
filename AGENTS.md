# InputWeaver

context-aware input mapping and macro engine

## Project Overview

InputWeaver is a C++ context-aware input mapping and macro engine. Weave source files use the `.weave` extension, compiled programs use the `.weavec` extension, the compiler artifact is `InputWeaverCompiler.exe`, and the Windows executor artifact is `InputWeaver.exe`.

The compiler and executor are independent command-line programs. `InputWeaverCompiler.exe` transforms `.weave` source into a persistent `.weavec` file, and `InputWeaver.exe` loads and executes that file without reparsing source or receiving an in-memory compiled program from the compiler.

## Environment

- Platform: Windows.
- Compiler: MinGW-w64 GCC and G++ 15.1.0 are available on `PATH`.
- Language: C++.
- Dependencies: use only the C++ standard library and the Windows API; do not introduce third-party libraries.
- Git: this repository is initialized for Git-based version control. Inspect `git status` before changing files and do not commit or push unless explicitly requested.

## Build and Verification

- Keep canonical build, test, analysis, and run commands in batch files under `script/`.
- Place generated executables and other build output under `bin/` and keep them out of version control.
- Compile with `g++` and verify a successful build after changing C++ source code.
- Use the specifications under `docs/language/` for current Weave behavior.
- Use `script/build_compiler_tests.bat` to build `InputWeaverCompiler.exe` and compiler tests, `script/build.bat` to build `InputWeaver.exe` and runtime tests, and `script/verify_phase3.bat` for the combined strict verification gate.

## Agent Coordination

- `AGENTS.md` contains stable repository rules, the dependency model, the source layout, and concise pointers to current development work.
- `docs/language/` owns Weave source-language definitions.
- `development/` outside `development/legacy/` owns current designs, handoff material, research, and open design issues.
- `development/legacy/` contains archived material from past work. It is not a current requirement or development input and does not need to be read unless the user explicitly requests historical comparison.
- Move completed phase directories into `development/legacy/` as content-preserving snapshots; do not rewrite their internal references solely because the containing directory moved.
- `development/Handoff.md` is the current implementation and command-line handoff.
- Read the relevant language specification, current handoff, shared program contract in `src/program/`, and open-issue register before changing an owned subsystem.
- Record a new decision in its owning document instead of duplicating phase history or handoff logs in `AGENTS.md`.

## Dependency Model

The command line is the current control plane. The compiler command selects a `.weave` source and `.weavec` destination, while the executor command selects a `.weavec` artifact and optional target override. The compiler and runtime never call each other and never exchange an in-memory `CompiledProgram`.

```text
Control flow:

InputWeaverCompiler.exe -> compiler
InputWeaver.exe         -> runtime -> Windows hooks and injection

Program data:

.weave -> compiler -> .weavec -> runtime

Code dependencies:

compiler -> program
runtime  -> program + input
program  -> input control types
platform/windows -> runtime + program + input + diagnostics
diagnostics -> input + runtime diagnostic types
all modules -> support only for domain-independent primitives
```

`program` is the shared definition of the compiled artifact, not a call path between compiler and runtime. The Windows entry point owns command-line mode selection and target discovery, while `runtime` owns artifact activation and executable state.

## File Layout

- `src/compiler/` owns Weave source loading, lexical analysis, parsing, semantic binding, type checking, lowering, compile diagnostics, and `.weavec` emission.
- `src/program/` owns `CompiledProgram`, canonicalization, structural validation, deterministic dumps, and the shared `.weavec` encoding contract.
- `src/input/` owns platform-independent live input and output control types, normalized events, transitions, origins, decisions, and bounded output batch records.
- `src/runtime/` owns platform-independent program activation, physical state, variable and `PAUSE` state, dispatch, expression evaluation, mappings, action execution, task scheduling, cancellation, output ownership, and runtime port interfaces.
- `src/platform/windows/` owns the Windows executor entry point, command-line mode selection, runtime-session assembly, hooks, native input normalization, `SendInput` injection, process discovery and validation, process launch, and runtime platform interfaces.
- `src/diagnostics/` owns bounded diagnostic transport, formatting, and reporting sinks; domain decisions remain in the producing subsystem.
- `src/support/` owns primitives that are independent of Weave, compiled programs, input devices, runtime execution, application policy, and operating systems.
- `tests/program/`, `tests/compiler/`, and `tests/runtime/` mirror the corresponding source-module boundaries; platform integration tests remain explicitly Windows-scoped.
- `docs/language/` contains user-visible Weave language definitions; `development/` contains current engineering documents; `development/legacy/` contains archived material that is outside current development.
- `script/` contains canonical build, test, and run commands; `res/` contains Windows resources; `bin/` contains ignored generated artifacts.

## Current Development

- The implemented command-line workflow is `.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe`.
- `InputWeaverCompiler.exe` provides `compile`, `validate`, and `dump`; `InputWeaver.exe` provides compiled-program execution, observer mode, and the retained Phase 1 fixed-rule regression mode.
- `example/notepad-showcase.weave`, its compiled artifact, helper scripts, Chinese acceptance guide, and acceptance JSONL demonstrate and verify the current end-to-end Windows path.
- `development/Handoff.md` records the current implementation boundary, commands, modes, verification evidence, and operational constraints.
- Completed Phase 2 and Phase 3 plans and verification records are archived under `development/legacy/phase-2/`, `development/legacy/phase-3-compiler/`, and `development/legacy/phase-3-runtime/`.
- Use `development/OpenDesignIssues.md` for design decisions that remain active and `script/verify_phase3.bat` for the combined build, test, static-analysis, dependency, and diff gate.

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

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
- Use `docs/language/grammar.v1.md`, `docs/language/grammar.v2.md`, and `docs/language/grammar.v2.ebnf` for current Weave behavior.
- For compiler, compiler CLI, or Windows compiler-backend changes, use `script/build_compiler_tests.bat` followed by `script/test_compiler.bat`.
- For platform-independent runtime or Windows runtime-adapter changes, use `script/build_runtime_tests.bat` followed by `script/test_runtime.bat`.
- For shared-program, Windows executor, hook, injection, or diagnostic changes, use `script/build.bat` followed by `script/test.bat`.
- Run the relevant analyzer and dependency audit when a changed boundary warrants them; reserve `script/verify_phase4.bat` for cross-cutting integration, release, or acceptance-gate verification.

## Agent Coordination

- `AGENTS.md` contains stable repository rules, the dependency model, the source layout, and concise pointers to current development work.
- `docs/language/` owns Weave source-language definitions.
- Chinese product operation guides live directly under `docs/`, outside `docs/language/`; example-specific manual test procedures live with their example as `ManualTest.md`.
- `development/` outside `development/legacy/` owns current designs, handoff material, research, and open design issues.
- `development/legacy/` contains archived material from past work. It is not a current requirement or development input and does not need to be read unless the user explicitly requests historical comparison.
- Move completed phase directories into `development/legacy/` as content-preserving snapshots; do not rewrite their internal references solely because the containing directory moved.
- `development/CompilerRuntimeCompletionHandoff.md` is the interface handoff for the completed compiler and runtime stage.
- Read the relevant language specification, compiler/runtime handoff, shared program contract in `src/program/`, and open-issue register before changing an owned subsystem.
- Record a new decision in its owning document instead of duplicating phase history or handoff logs in `AGENTS.md`.

## Dependency Model

The command line is the current control plane. The compiler command selects a `.weave` source and `.weavec` destination, while the executor command selects a `.weavec` artifact and optional target override. The compiler and runtime never call each other and never exchange an in-memory `CompiledProgram`.

```text
Control flow:

InputWeaverCompiler.exe -> Windows compiler entry -> compiler CLI -> compiler
InputWeaver.exe         -> Windows runtime entry -> runtime CLI -> Windows executor -> runtime + Windows hooks and injection

Program data:

.weave -> compiler -> .weavec -> runtime

Code dependencies:

compiler -> program
runtime  -> program + input
program  -> input control types
ui/cli/compiler_cli -> compiler
ui/cli/runtime_cli -> standard library
platform/windows/cli/compiler_main -> ui/cli/compiler_cli
platform/windows/cli/runtime_main -> ui/cli/runtime_cli + platform/windows/runtime executor interface
platform/windows/compiler -> compiler artifact-file interface
platform/windows/diagnostics -> input + runtime diagnostic types
platform/windows/runtime -> runtime + program + input + platform/windows/diagnostics
all modules -> support only for domain-independent primitives
```

`program` is the shared definition of the compiled artifact, not a call path between compiler and runtime. The platform-independent CLI layer owns option models, parsing, help, and compiler command presentation. Platform paths place the platform first and the owning module second. The Windows CLI module adapts `wmain` arguments and invokes the appropriate platform-independent CLI or Windows executor interface; the Windows runtime module owns target discovery and session assembly, while the platform-independent `runtime` module owns artifact activation and executable state.

## File Layout

- `src/compiler/` owns Weave source loading, lexical analysis, parsing, semantic binding, type checking, lowering, compile diagnostics, `.weavec` encoding, and the platform-neutral artifact publication flow.
- `src/program/` owns `CompiledProgram`, canonicalization, structural validation, deterministic dumps, and the shared `.weavec` encoding contract.
- `src/input/` owns platform-independent live input and output control types, normalized events, transitions, origins, decisions, and bounded output batch records.
- `src/runtime/` owns platform-independent program activation, physical state, variable and `PAUSE` state, dispatch, expression evaluation, mappings, action execution, task scheduling, cancellation, output ownership, and runtime port interfaces.
- `src/ui/cli/` owns platform-independent command-line option models, parsing, help output, and compiler command presentation.
- `src/platform/<platform>/<module>/` is the required layout for platform-specific code.
- `src/platform/windows/cli/` owns only the Windows command-line entry points, native argument adaptation, and invocation of the platform-independent CLI or Windows executor interface.
- `src/platform/windows/compiler/` owns Windows sibling-temporary naming and atomic destination replacement for compiled artifacts.
- `src/platform/windows/diagnostics/` owns bounded Windows diagnostic records, privacy redaction, JSONL formatting, transport, and file output.
- `src/platform/windows/runtime/` owns Windows executor assembly, hooks, native input normalization, `SendInput` injection, process discovery and validation, process launch, and runtime platform interfaces; it depends on Windows diagnostics but not on the CLI module.
- `src/support/` owns primitives that are independent of Weave, compiled programs, input devices, runtime execution, application policy, and operating systems.
- `tests/program/`, `tests/compiler/`, and `tests/runtime/` mirror the corresponding source-module boundaries; platform integration tests remain explicitly Windows-scoped.
- `docs/language/` contains Weave language definitions; direct files under `docs/` contain Chinese product operation guides; each example directory may contain its own `ManualTest.md`; `development/` contains current engineering documents; `development/legacy/` contains archived material that is outside current development.
- `script/` contains canonical build, test, and run commands; `res/` contains Windows resources; `bin/` contains ignored generated artifacts.

## Current Development

- The current delivered stage is the completed compiler and runtime command-line interface recorded in `development/CompilerRuntimeCompletionHandoff.md`.
- The implemented command-line workflow is `.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe`.
- `InputWeaverCompiler.exe` provides `compile`, `validate`, and `dump`; `InputWeaver.exe` loads and executes one compiled `.weavec` program.
- `example/notepad-showcase/` owns the showcase source, compiled artifact, helper commands, manual test procedure, and retained acceptance JSONL for the current end-to-end Windows path.
- `docs/safety-guide.md`, `docs/runtime-boundaries.md`, and `example/phase-4-safety/` define the current Chinese safety guidance, fixed execution boundaries, and Windows keyboard-safety manual test path.
- `development/CompilerRuntimeCompletionHandoff.md` records the delivered artifacts, command-line interfaces, language boundary, runtime contract, canonical workflow, and operating requirements for this stage.
- Completed Phase 2, Phase 3, and Phase 4 records are archived under `development/legacy/phase-2/`, `development/legacy/phase-3-compiler/`, `development/legacy/phase-3-runtime/`, `development/legacy/phase-4-safety/`, and `development/legacy/phase-4-refactor/`.
- Use `development/OpenDesignIssues.md` for design decisions that remain active, `script/verify_phase4.bat` for the combined build, test, static-analysis, dependency, CLI-authority, acceptance-validator self-test, retained-evidence validation, documentation, and diff gate, and `example/phase-4-safety/verify.bat` for retained physical Windows evidence.

## Repository Practices

- Keep source code, comments, filenames, language specifications, and engineering documentation in English; keep Chinese product operation guides under `docs/` and example-specific manual test procedures with their example.
- Keep `src/ui/` platform-independent; native entry points, operating-system APIs, and platform adapter dependencies belong under `src/platform/<platform>/<module>/`.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

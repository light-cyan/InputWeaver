# InputWeaver

context-aware input mapping and macro engine

## Project Overview

InputWeaver is a C++ context-aware input mapping and macro engine. Weave source files use the `.weave` extension, compiled programs use the `.weavec` extension, the compiler artifact is `InputWeaverCompiler.exe`, and the Windows executor artifact is `InputWeaver.exe`.

The compiler and executor are independent command-line programs. `InputWeaverCompiler.exe` transforms `.weave` source into a persistent `.weavec` file, and `InputWeaver.exe` loads and executes that file without reparsing source or receiving an in-memory compiled program from the compiler.

The Windows control UI consists of the GUI tray host `InputWeaverHost.exe` and its internal native-window frontend `InputWeaverTUI.exe`. The tray host owns application and executor state, while the frontend can exit and be recreated without stopping the host.

## Environment

- Platform: Windows.
- Compiler: MinGW-w64 GCC and G++ 15.1.0 are available on `PATH`; product builds also require `windres` on `PATH`.
- Language: C++.
- Dependencies: use only the C++ standard library and the Windows API; do not introduce third-party libraries.
- Git: this repository is initialized for Git-based version control. Inspect `git status` before changing files and do not commit or push unless explicitly requested.

## Build and Verification

- Keep canonical build, test, analysis, and run commands in batch files under `script/`.
- Place generated executables and other build output under `bin/` and keep them out of version control.
- Compile with `g++` and verify a successful build after changing C++ source code.
- Use `language.md`, `rules.md`, `actions.md`, and `mouse.md` under `docs/zh/` or `docs/en/` for current Weave syntax and behavior; use `running.md` for execution behavior and limits.
- Use `script/build_products.bat` to build release products without tests, `script/build_tests.bat` to build all test executables, and `script/build.bat` to build both groups.
- Use `script/build_tui.bat` to build the Windows tray host and frontend.
- Use `script/package_release.bat` to build products and package all files under `docs/` with the executables and color resources. It creates `bin/release/InputWeaver/` and `bin/release/InputWeaver-windows-x64.zip`.
- Use `script/test.bat` to run all C++ test suites; focused test suites use matching `script/build_<area>_tests.bat` and `script/test_<area>.bat` commands.
- Use `script/verify_docs.bat` to build the compiler and validate documentation structure, local links, and code examples; use `script/test_docs.bat` for the documentation verifier's regression tests. Both accept `--skip-build` when the compiler is already current.
- For compiler, compiler CLI, or Windows compiler-backend changes, build `script/build_compiler.bat` and `script/build_compiler_tests.bat`, then run `script/test_compiler.bat`.
- For platform-independent runtime or Windows runtime-adapter changes, use `script/build_runtime_tests.bat` followed by `script/test_runtime.bat`.
- For shared-program, Windows executor, hook, injection, or diagnostic changes, build `script/build_executor.bat` and `script/build_executor_tests.bat`, then run `script/test_executor.bat`.
- Use `script/verify_project.bat` for cross-cutting code and documentation verification, and run the relevant analyzer and dependency audit when a changed boundary warrants them. Release packaging also validates documentation before creating the archive.

## Agent Coordination

- `AGENTS.md` contains stable repository rules, the dependency model, the source layout, and concise product-status pointers.
- `docs/README.md` is the language entry point. `docs/zh/README.md` and `docs/en/README.md` index the Chinese and English documentation. Their `language.md`, `rules.md`, `actions.md`, and `mouse.md` pages own the current Weave language explanations and examples.
- `windows.md` in each language owns Windows control capabilities and external-process path behavior; keep platform-specific details separate from shared language and runtime semantics.
- Chinese and English tutorials and product references live under `docs/zh/` and `docs/en/`; validation-specific manual test procedures live with their validation assets as `ManualTest.md`.
- `development/legacy/` contains archived material from past work. It is not a current requirement or development input and does not need to be read unless the user explicitly requests historical comparison.
- Move completed phase directories into `development/legacy/` as content-preserving snapshots; do not rewrite their internal references solely because the containing directory moved.
- `validation/` is the tracked location for validation assets.
- Read the relevant language specification, shared program contract in `src/program/`, and current operational documentation before changing an owned subsystem.
- Record a new decision in its owning current document instead of duplicating archived phase history in `AGENTS.md`.

## Documentation Synchronization

- Repository landing pages are `README.md` in English and `README.zh-CN.md` in Simplified Chinese. Keep their heading structure, shared anchors, and examples synchronized, with reciprocal language links below the title and links to documentation in the selected language.
- Update Chinese and English pages together in the same change. Keep identical relative filenames and the same ordered heading levels and section meanings; translate titles and prose naturally. Review meaning as well as the mechanical checks.
- Give each heading a standalone `<a id="section-name"></a>` immediately above it, separated by a blank line. Use the same stable English ID in both languages, retain it when wording changes, and use it in section links.
- Put a direct link to the corresponding page in the other language below each page title. Keep other page links within the current language. `docs/README.md` links to both language indexes; all local documentation links must resolve within the packaged `docs/` tree.
- Use inline Markdown links, ATX headings, and fenced code blocks. Each fence must be labeled `weave` or `powershell`. Keep code blocks identical and in corresponding sections in both languages, including English comments; each Weave block must be a complete source that validates independently.
- `script/verify_docs.bat` checks page pairs, heading levels and shared anchors, language switches, local file and section links, and matching examples. It runs the compiler's `validate` command for Weave and parses PowerShell examples without executing them. Generated examples stay under `bin/docs-validation/`.
- Keep tutorials and references focused on user-visible behavior, valid usage, and troubleshooting. Keep build procedures and synchronization rules in engineering documentation, and keep Windows-specific behavior explicitly scoped to Windows.

## Dependency Model

The command line is the current control plane. The compiler command selects a `.weave` source and `.weavec` destination, while the executor command selects a `.weavec` artifact and optional target override. The compiler and runtime never call each other and never exchange an in-memory `CompiledProgram`.

```text
Control flow:

InputWeaverCompiler.exe -> Windows compiler entry -> compiler CLI -> compiler
InputWeaver.exe         -> Windows runtime entry -> runtime CLI -> Windows executor -> runtime + Windows hooks and injection
InputWeaverHost.exe     -> Windows tray host -> TUI controller -> application -> Windows application adapter -> compiler, executor, and DebugClient
InputWeaverTUI.exe      -> Windows native frontend -> restricted inherited IPC -> Windows tray host

Program data:

.weave -> compiler -> .weavec -> runtime

Code dependencies:

language  -> standard library
compiler -> language + program
runtime  -> program + input
debug -> runtime + program + input
app -> debug
program  -> standard library
ui/cli/compiler_cli -> compiler
ui/cli/runtime_cli -> standard library
ui/tui -> language + app + debug
platform/windows/ui/cli/compiler_main -> ui/cli/compiler_cli
platform/windows/ui/cli/runtime_main -> ui/cli/runtime_cli + platform/windows/runtime executor interface
platform/windows/compiler -> compiler artifact-file interface + platform/windows/support
platform/windows/app -> app + debug + platform/windows/debug + platform/windows/support
platform/windows/diagnostics -> input + runtime diagnostic types
platform/windows/debug -> debug + runtime + program + input + platform/windows/support
platform/windows/support -> Windows API
platform/windows/ui/tui -> ui/tui + app + platform/windows/app + platform/windows/support
platform/windows/runtime -> runtime + program + input + platform/windows/debug + platform/windows/diagnostics + platform/windows/support
all modules except language -> support only for domain-independent primitives
```

`program` is the shared definition of the compiled artifact, not a call path between compiler and runtime. The platform-independent CLI layer owns option models, parsing, help, and compiler command presentation. Platform paths place the platform first and the owning module second, with platform UI adapters mirroring `src/ui/` under `src/platform/<platform>/ui/`. The Windows CLI adapter invokes the appropriate platform-independent CLI or Windows executor interface; the Windows runtime module owns target discovery and session assembly, while the platform-independent `runtime` module owns artifact activation and executable state.

## File Layout

- `src/language/` owns platform-independent Weave lexical analysis, string-literal decoding, and the shared language word catalog used by the compiler and source highlighter.
- `src/compiler/` owns Weave source loading, parsing, semantic binding, type checking, lowering, compile diagnostics, `.weavec` encoding, and the platform-neutral artifact publication flow.
- `src/program/` owns `CompiledProgram`, canonicalization, structural validation, deterministic dumps, and the shared `.weavec` encoding contract.
- `src/input/` owns platform-independent live input and output types, transitions, origins, and decisions; platform-native raw events and injection recipes belong under `src/platform/<platform>/`.
- `src/runtime/` owns platform-independent program activation, physical state, variable and `PAUSE` state, dispatch, expression evaluation, mappings, action execution, task scheduling, cancellation, output ownership, and runtime port interfaces.
- `src/debug/` owns the platform-independent input-debug protocol values, explicit binary frame codec, client interface, event reduction, and immutable derived state.
- `src/app/` owns platform-independent program catalog state, import decisions, executor policy, console history, and debug-session orchestration through an abstract platform port.
- `src/ui/cli/` owns platform-independent command-line option models, parsing, help output, and compiler command presentation.
- `src/ui/tui/` owns platform-independent page state, keyboard intents, viewport behavior, text layout, color-scheme parsing, and cell-based rendering.
- `src/platform/<platform>/<module>/` is the required layout for non-UI platform-specific code; platform UI adapters live under `src/platform/<platform>/ui/<frontend>/` and mirror `src/ui/<frontend>/`.
- `src/platform/windows/ui/cli/` owns only the Windows command-line entry points, native argument adaptation, and invocation of the platform-independent CLI or Windows executor interface.
- `src/platform/windows/app/` owns the Windows program library, compiler and executor child processes, redirected output, and `WindowsDebugClient` lifecycle used by the application port.
- `src/platform/windows/compiler/` owns Windows sibling-temporary naming and atomic destination replacement for compiled artifacts.
- `src/platform/windows/diagnostics/` owns bounded Windows diagnostic records, privacy redaction, JSONL formatting, transport, and file output.
- `src/platform/windows/debug/` owns the local same-user named-pipe server and client, capture commands, process and endpoint validation, cancellable pipe I/O, and bounded debug event transport.
- `src/platform/windows/support/` owns Windows resource and API primitives that are independent of compiler, runtime, debug, diagnostics, and application policy.
- `src/platform/windows/runtime/` owns Windows executor assembly, hooks, native input normalization, `SendInput` injection, process discovery and validation, process launch, and runtime platform interfaces; it depends on Windows debug and diagnostics but not on the Windows UI CLI adapter.
- `src/platform/windows/ui/tui/` owns the tray-host and native-frontend entry points, their inherited-pipe IPC, the Win32 TUI window, GDI cell rendering, keyboard and clipboard input, resize handling, notification icon, and color-resource loading.
- `src/support/` owns primitives that are independent of Weave, compiled programs, input devices, runtime execution, application policy, and operating systems.
- `tests/program/`, `tests/compiler/`, `tests/debug/`, `tests/runtime/`, `tests/app/`, and `tests/ui/` mirror the corresponding source-module boundaries; platform integration tests remain explicitly Windows-scoped.
- `docs/README.md` is the language entry point; `docs/zh/` and `docs/en/` contain matching Chinese and English tutorials and references, each organized by its `README.md`; `validation/` is the tracked location for validation assets; `development/legacy/` contains archived engineering material.
- `script/` contains canonical build, test, analysis, and release-packaging commands; `res/` contains Windows resources; `bin/` contains ignored generated artifacts.

## Repository Practices

- Keep source code, comments, filenames, and engineering documentation in English; keep Chinese and English tutorials and product references under `docs/zh/` and `docs/en/`, respectively, and validation-specific manual test procedures with their validation assets.
- Keep `src/ui/` platform-independent; native entry points, operating-system APIs, and platform adapter dependencies belong under `src/platform/<platform>/<module>/`.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.

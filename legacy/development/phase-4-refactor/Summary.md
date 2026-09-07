# Phase 4 Refactor Summary

## Purpose

Phase 4 completed the compiler and runtime boundary after the persistent `.weavec` workflow became operational. The refactor reduced the delivered execution surface to one compiled-program path, separated Windows command-line entry points from executor assembly, organized platform code by platform and owning module, and retained the keyboard-safety work as an independently reproducible example.

## Delivered command-line boundary

- `InputWeaverCompiler.exe` remains an independent compiler with `compile`, `validate`, and `dump` commands.
- `InputWeaver.exe` requires `--program <file.weavec>` and executes only a compiled program.
- The source `TARGET` declaration is the default execution target, `--target <selector>` overrides it with an executable selector, and `--target-global` explicitly overrides it with global execution.
- The effective target selected by the command line or compiled source is passed into runtime activation, so process discovery, rule routing, and output eligibility use the same scope.
- `--allow-exec` is the invocation-scoped authority for process launch.
- The compiler and executor exchange only a persistent `.weavec` artifact and never call each other or exchange an in-memory `CompiledProgram`.

## Removed execution paths

- The no-program input observation path was removed from the executor interface and implementation.
- The hard-coded fixed-rule command and its dedicated fixed-rule engine, remap engine, action scheduler, Windows session, and producer-drain helper were removed.
- The executor now assembles only `WindowsProgramRuntimeSession`, which activates and runs a validated compiled program.
- The Phase 1 mixed runtime test executable was replaced by Windows platform tests that cover the platform facilities still used by compiled-program execution.

## Platform and module layout

Platform-specific source paths use the platform as the first directory and the owning module as the second directory.

```text
src/ui/cli/
src/platform/windows/compiler/
src/platform/windows/cli/
src/platform/windows/diagnostics/
src/platform/windows/runtime/
```

- `src/ui/cli/` owns platform-independent option structures, command-line parsing, usage output, and compiler command presentation.
- `src/platform/windows/compiler/` owns sibling-temporary naming and atomic destination replacement for compiler artifacts.
- `src/platform/windows/cli/` owns only the Windows `wmain` entry points, native argument adaptation, and invocation of the platform-independent CLI or Windows executor interface.
- `src/platform/windows/diagnostics/` owns bounded diagnostic records, privacy redaction, JSONL formatting, transport, and file output.
- `src/platform/windows/runtime/` owns executor assembly, target resolution, program runtime sessions, hooks, input normalization and injection, process validation and discovery, runtime control binding, routing, and process launch.
- The compiler core depends only on the platform-neutral artifact-file interface; the Windows implementation is selected by the build without introducing Windows API headers into the compiler core.
- `src/ui/` contains only platform-independent user-interface code and does not own native entry points or platform adapters.
- The dependency audit rejects Windows runtime dependencies on the CLI module, compiler/runtime core dependencies on platform code, native Windows dependencies in runtime core, platform dependencies in UI code, and files placed directly under `src/platform/windows/` without a second-level module owner.

## Language specification boundary

The current source-language contract starts with `docs/language/grammar.v1.md`; `grammar.v2.md` and `grammar.v2.ebnf` add the current control-name forms. The earlier v0 draft was removed from the current language specifications.

## Safety and operational boundary

- Target eligibility changes cancel target-bound work and release runtime-owned output before execution can resume.
- Dispatch, task work, action publication, diagnostics, and shutdown cleanup use fixed limits and fail-safe behavior.
- Activated keyboard state is seeded before input handling so startup-held controls do not create mappings and startup-held modifiers remain available to the physical force-stop chord.
- Process launch is denied unless the executor receives explicit invocation authority.
- Operational diagnostics are privacy-redacted, bounded, and accompanied by final drop, size, truncation, transaction-rejection, scheduler, and hook-duration statistics.
- The self-contained Windows safety source, commands, manual procedure, validator, and retained evidence live under `example/phase-4-safety/`.

## Verification boundary

`script/verify_phase4.bat` is the combined build, automated-test, static-analysis, dependency-audit, command-line-boundary, reproducible-example, validator-self-test, retained-evidence, documentation, and diff gate for the completed refactor.

The complete gate passed on 2026-08-24 after the CLI and initial platform-directory split. The retained Windows evidence passed with observed JSONL hook maxima of 84 microseconds for the main run and 35 microseconds for the startup-held run. The subsequent boundary audit passed the focused compiler build and tests, Windows executor build and tests, runtime fast tests, dependency audit, and diff checks after Windows compiler publication, diagnostic placement, and effective-target propagation were finalized.

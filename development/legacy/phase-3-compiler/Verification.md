# Phase 3 Compiler Verification

## Status

The Phase 3 compiler implementation gate passes on 2026-08-24. The compiler accepts every specified valid Weave v2 form, rejects the specified invalid forms with bounded diagnostics and no artifact, and emits structurally valid deterministic `.weavec` programs.

## Implemented pipeline

- `src/compiler/source.*` implements bounded binary source loading, UTF-8 validation, byte-based source identity, CRLF/LF/CR line starts including an empty final line after a terminal break, source excerpts, stable diagnostics, and diagnostic limits.
- `src/compiler/frontend.*` and `src/compiler/syntax.hpp` implement the complete parser-oriented Weave v2 grammar, longest-match tokens, ASCII syntax enforcement, UTF-8 comments, cooked strings with the frozen escape set, bounded top-level and nested action-flow recovery, precise spans, expression precedence, action adjacency, mappings, event rules, and dedicated pause rules.
- `src/compiler/control_catalog.*` implements canonical v1 and v2 named controls, aliases, platform-qualified catalog entries, and numeric control constructors with shared frozen storage domains.
- `src/compiler/semantics.*` and `src/compiler/bound_program.hpp` implement declaration-order name binding, built-in values, raw control validation, exact duration conversion, static types, short-circuit-aware constant fault checks, non-empty target and command checks, writable-value checks, and bound structured control flow.
- `src/compiler/lowering.*` implements canonical pool construction, typed expression bytecode, short-circuit branches, immutable action programs, cooperative loop back edges, mapping descriptors, pause-control and ordinary event indexes, source ordinals, control-use requirements, resource derivation, and shared finalization.
- `src/compiler/compiler.*` implements the public source, compile, validate, and dump boundary, deterministic `.weavec` encoding, bounded formatted diagnostics, sibling temporary-file writes, and destination replacement after a complete successful write.
- `src/compiler/compiler_cli.cpp` implements the `compile`, `validate`, and `dump` commands with a native wide Windows argument boundary and without application or runtime integration.

## Strict build and compiler tests

`cmd /c script\build_compiler_tests.bat` completed successfully under C++20 with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror` and built `InputWeaverCompiler.exe` and `CompilerTests.exe` under `bin/`.

`cmd /c script\test_compiler.bat` completed successfully with this result:

```text
All compiler tests passed.
```

The compiler tests cover UTF-8 and ASCII boundaries, all line endings, token and syntax limits, comments, every cooked string escape, unknown escapes, decoded artifact bytes, empty and non-empty whitespace strings, longest-match arrows, top-level and nested action-flow parser recovery, every top-level and action production, declaration-order symbols, reserved and duplicate names, all value and operator signatures, exact and invalid durations, reachable and proven-unreachable constant faults, writable targets, v2 aliases, every raw-control boundary, all expression opcodes, all action opcodes, all unary and binary operators, every arrow combination, empty action rules, exact gap emission, mappings, pause controls, cooperative loop edges, globally unique source ordinals, control-use bits, requirements, exact fixture dumps and artifacts, deterministic bytes, shared-codec round trips, artifact corruption, successful compile, validate, and dump file commands, file replacement, retained destinations after failed compilation, and formatted diagnostics.

## Static analysis and dependency audit

`cmd /c script\analyze_compiler.bat` completed successfully for every compiler translation unit with GCC `-fanalyzer -fsyntax-only` and the strict warning policy.

`cmd /c script\audit_phase3_dependencies.bat` completed successfully. The compiler dependency graph contains only the standard library and the shared `program` module, except that `compiler.cpp` uses the Windows API solely for destination artifact replacement. No compiler file includes runtime, hook, injector, scheduler, application, or TUI code.

The compiler retains the exact non-empty NUL-free decoded command in one `Exec` operand and derives `requiresProcessLaunch`, while executable-token extraction and native resolution remain in the Windows runtime adapter.

`git diff --check` and the explicit trailing-whitespace audit pass.

## Existing regression suites

`cmd /c script\build.bat` and `cmd /c script\test.bat` completed successfully after the compiler implementation with these results:

```text
All Phase 1 automated tests passed.
All CompiledProgram contract tests passed.
All Phase 3 runtime core tests passed.
All Windows runtime adapter tests passed.
```

## Shared fixture convergence

Compiling the four required fixture sources produces the exact deterministic dump and `.weavec` bytes of the Phase 2 hand-built fixtures, including canonical settings, pools, values, constants, instructions, descriptors, indexes, requirements, mappings, pause controls, rules, source ordinals, line starts, and precise debug spans. Encoding and decoding each result reproduces the same dump and bytes.

The shared fixture builders retain the smallest complete authored construct for source records and use the synthetic-instruction span policy in `development/phase-2/CompiledProgramDesign.md`. The shared golden hashes cover these precise spans and the terminal line-start entry.

The authoritative language and compiled-program documents now define cooked ASCII strings, non-empty target and `exec` values, raw-control numeric domains with activation-time capability checks, and compile-time constant-fault reachability consistent with runtime short-circuit execution. The shared validator and compiler/runtime tests enforce the same boundaries.

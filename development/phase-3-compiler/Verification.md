# Phase 3 Compiler Verification

## Status

The current compiler test suite and every compiler-owned verification command pass on 2026-08-23. Phase completion remains open for the exact frozen-dump convergence decision and the active items in `development/phase-3-compiler/OpenQuestions.md`.

## Implemented pipeline

- `src/compiler/source.*` implements bounded binary source loading, UTF-8 validation, byte-based source identity, CRLF/LF/CR line starts, source excerpts, stable diagnostics, and diagnostic limits.
- `src/compiler/frontend.*` and `src/compiler/syntax.hpp` implement the complete parser-oriented Weave v2 grammar, longest-match tokens, ASCII syntax enforcement, UTF-8 comments, strings, bounded recovery, precise spans, expression precedence, action adjacency, mappings, event rules, and dedicated pause rules.
- `src/compiler/control_catalog.*` implements canonical v1 and v2 named controls, aliases, platform-qualified catalog entries, and numeric control constructors.
- `src/compiler/semantics.*` and `src/compiler/bound_program.hpp` implement declaration-order name binding, built-in values, raw control validation, exact duration conversion, static types, constant fault checks, writable-value checks, and bound structured control flow.
- `src/compiler/lowering.*` implements canonical pool construction, typed expression bytecode, short-circuit branches, immutable action programs, cooperative loop back edges, mapping descriptors, pause-control and ordinary event indexes, source ordinals, control-use requirements, resource derivation, and shared finalization.
- `src/compiler/compiler.*` implements the public source, compile, validate, and dump boundary, deterministic `.weavec` encoding, bounded formatted diagnostics, sibling temporary-file writes, and destination replacement after a complete successful write.
- `src/compiler/compiler_cli.cpp` implements the `compile`, `validate`, and `dump` commands without application or runtime integration.

## Strict build and compiler tests

`cmd /c script\build_compiler_tests.bat` completed successfully under C++20 with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror` and built `InputWeaverCompiler.exe` and `CompilerTests.exe` under `bin/`.

`cmd /c script\test_compiler.bat` completed successfully with this result:

```text
All compiler tests passed.
```

The compiler tests cover UTF-8 and ASCII boundaries, all line endings, token and syntax limits, comments, strings, longest-match arrows, parser recovery, every top-level and action production, declaration-order symbols, reserved and duplicate names, all value and operator signatures, exact and invalid durations, constant faults, writable targets, v2 aliases and raw controls, all expression opcodes, all action opcodes, all unary and binary operators, every arrow combination, empty action rules, exact gap emission, mappings, pause controls, cooperative loop edges, globally unique source ordinals, control-use bits, requirements, deterministic bytes, shared-codec round trips, artifact corruption, file replacement, retained destinations after failed compilation, and formatted diagnostics.

## Static analysis and dependency audit

`cmd /c script\analyze_compiler.bat` completed successfully for every compiler translation unit with GCC `-fanalyzer -fsyntax-only` and the strict warning policy.

The compiler include audit found dependencies only on the standard library and the shared `program` module, except that `compiler.cpp` uses the Windows API solely for destination artifact replacement. No compiler file includes runtime, hook, injector, scheduler, application, or TUI code.

`ODI-004` does not alter compiler-owned data: the compiler retains the exact NUL-free authored command in one `Exec` operand and derives `requiresProcessLaunch`, while executable-token extraction and native resolution remain in the Windows runtime adapter.

`git diff --check` and the explicit trailing-whitespace audit pass.

## Existing regression suites

`cmd /c script\build.bat` and `cmd /c script\test.bat` completed successfully after the compiler implementation with these results:

```text
All Phase 1 automated tests passed.
All CompiledProgram contract tests passed.
```

## Shared fixture convergence

Compiling the four required fixture sources produces the same canonical settings, pools, values, constants, instructions, descriptors, indexes, requirements, mappings, pause controls, rules, source ordinals, and debug-record topology as the Phase 2 hand-built fixtures. Encoding and decoding each compiler result reproduces its deterministic dump and bytes.

The Phase 2 fixtures assign the whole source-file span to every retained target, rule, expression, action, instruction, mapping, and variable record. The compiler emits the precise source region required by C5. Because the deterministic dump includes these spans, the compiler's complete dump cannot equal the existing frozen hash while both span policies remain unchanged. No shared `program` implementation or fixture file has been changed in this branch.

The decision and its protected shared scope are tracked as `PCQ-001` in `development/phase-3-compiler/OpenQuestions.md`; the remaining language, interface, and implementation concerns discovered during verification are tracked in the same file.

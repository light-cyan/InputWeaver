# Phase 2 CompiledProgram Verification

## Result

The Phase 2 implementation gate passes on 2026-08-21. The immutable `CompiledProgram` representation, dedicated pause-control channel, finalizer, canonicalizer, validator, deterministic dump, fixtures, and contract tests are implemented as the frozen successor-phase handoff.

## Implemented files

- `src/core/compiled_program.hpp` defines the complete schema, storage, immutable API, builder, finalization result, and structured validation errors.
- `src/core/compiled_program.cpp` implements immutable access, canonical pool remapping, mapping-slot and event-bucket ordering, builder operations, and validated finalization.
- `src/core/program_validator.hpp` and `src/core/program_validator.cpp` implement exact requirement derivation and bounded structural validation.
- `src/core/program_dump.hpp` and `src/core/program_dump.cpp` implement the locale-independent complete diagnostic dump.
- `tests/compiled_program_fixtures.*` implement the four required hand-built programs.
- `tests/compiled_program_tests.cpp` implements contract, canonicalization, coverage, golden, corruption, and validation-limit tests.

## Strict build and tests

`cmd /c script\build.bat` completed successfully and built `InputWeaver.exe`, `InputWeaverTests.exe`, and the platform-independent `CompiledProgramTests.exe` under C++20 with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.

`cmd /c script\test.bat` completed successfully with the following results:

```text
All Phase 1 automated tests passed.
All CompiledProgram contract tests passed.
```

## Contract coverage

- The tap, complete mapping, conditional repeat, and pause-control fixtures finalize successfully and return `std::shared_ptr<const CompiledProgram>`.
- Their canonical dumps are frozen by FNV-1a 64-bit golden values `16250531285494687580`, `6279514400355555682`, `16791086043516604065`, and `16678841533137656072` respectively.
- An additional valid coverage program constructs every `ExpressionOpcode`, every `ActionOpcode`, every unary operator, every binary operator, every value domain, typed user storage, both control-use directions, process-launch requirements, repeat frames, and cooperative backward control flow.
- Pause-control coverage constructs `On`, `Off`, and `Toggle`, proves zero task requirements, and rejects ordinary action-program writes to `BuiltinState::Pause`.
- Equivalent programs with duplicate and differently ordered input pools finalize to identical dumps.
- Corrupt schema, source IDs, ranges, UTF-8, line starts, values, expression stack merges, action control IDs, backward jumps, pause effects, rule kinds, pause-control buckets, ordinary event buckets, mapping links, control requirements, resource requirements, and debug spans are rejected with no immutable program.
- Validation stops at `kMaximumProgramValidationErrors`.

## Static analysis

Each new core translation unit passed GCC `-fanalyzer -fsyntax-only` independently under the strict warning policy:

```text
src/core/compiled_program.cpp
src/core/program_validator.cpp
src/core/program_dump.cpp
```

## Platform boundary

The new core headers and translation units contain no `windows.h`, Win32 handles, Win32 scalar aliases, `VK_*` macros, hooks, `SendInput`, or platform process APIs. The standalone contract test executable links only the standard C++ runtime.

## Handoff state

The compiler and runtime successor plans are stored separately in `development/phase-3-compiler/ImplementationPlan.md` and `development/phase-3-runtime/ImplementationPlan.md`. Both start from the same reviewed Phase 2 commit, consume the frozen contract, and proceed without synchronization with each other.

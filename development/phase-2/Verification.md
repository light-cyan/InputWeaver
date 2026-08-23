# Phase 2 CompiledProgram Verification

## Result

The Phase 2 implementation gate passes on 2026-08-23. The shared `program` module implements the immutable `CompiledProgram`, canonical control identities, dedicated pause-control channel, finalizer, structural validator, deterministic dump, `.weavec` field codec, fixtures, and contract tests.

## Implemented files

- `src/program/compiled_program.hpp` defines the complete data model, storage, immutable API, builder, finalization result, control namespace assignments, and structured validation errors.
- `src/program/compiled_program.cpp` implements immutable access, canonical pool remapping, mapping-slot and event-bucket ordering, builder operations, and validated finalization.
- `src/program/program_validator.hpp` and `src/program/program_validator.cpp` implement exact requirement derivation and bounded structural validation.
- `src/program/program_dump.hpp` and `src/program/program_dump.cpp` implement the locale-independent complete diagnostic dump.
- `src/program/weavec_codec.hpp` and `src/program/weavec_codec.cpp` implement the canonical 16-byte header, normative payload order, fixed-width little-endian field encoding, bounded decoding, scalar checks, and finalization handoff.
- `tests/program/compiled_program_fixtures.*` implement the four required hand-built programs.
- `tests/program/compiled_program_tests.cpp` implements contract, canonicalization, control identity, coverage, golden dump, `.weavec` round-trip, artifact rejection, corruption, and validation-limit tests.

## Strict build and tests

`cmd /c script\build.bat` completed successfully and built `InputWeaver.exe`, `InputWeaverTests.exe`, and the platform-independent `CompiledProgramTests.exe` under C++20 with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.

`cmd /c script\test.bat` completed successfully with the following results:

```text
All Phase 1 automated tests passed.
All CompiledProgram contract tests passed.
```

## Contract coverage

- The tap, complete mapping, conditional repeat, and pause-control fixtures finalize successfully and return `std::shared_ptr<const CompiledProgram>`.
- Their canonical dumps are frozen by FNV-1a 64-bit golden values `10389705910507639397`, `17776357415295911139`, `14182767377470171226`, and `3121414655632945442` respectively.
- Every required fixture and the complete opcode coverage fixture encode to `.weavec`, decode through the shared codec, finalize, reproduce the same deterministic dump, and re-encode to identical bytes.
- Header magic, declared payload length, truncation, trailing fields, enum and Boolean scalar values, payload limits, collection limits, and post-decode structural validation have direct rejection tests.
- The control pool uses four-field portable identities, canonical sorting, deduplication, and dense `ControlRefId` operands shared by event keys, physical-state reads, output actions, mappings, and capability requirements.
- Published HID, Windows, Linux, and macOS identity shapes pass structural validation; namespace, family, qualifier, and reserved-field violations produce bounded validation errors.
- The complete opcode fixture constructs every `ExpressionOpcode`, every `ActionOpcode`, every unary operator, every binary operator, every value domain, typed user storage, executable target selection, process launch, repeat frames, and cooperative backward control flow.
- Pause-control coverage constructs `On`, `Off`, and `Toggle`, derives zero task requirements, and routes PAUSE mutation through the dedicated synchronous channel.
- Equivalent programs with duplicate and differently ordered input pools finalize to identical dumps.
- Corrupt source IDs, ranges, UTF-8, line starts, values, expression stack merges, action control IDs, backward jumps, pause effects, rule kinds, pause-control buckets, ordinary event buckets, mapping links, control requirements, resource requirements, and debug spans produce validation errors with an empty immutable handle.
- Validation stops at `kMaximumProgramValidationErrors`.

## Static analysis

Each `program` translation unit passed GCC `-fanalyzer -fsyntax-only` independently under the strict warning policy:

```text
src/program/compiled_program.cpp
src/program/program_validator.cpp
src/program/program_dump.cpp
src/program/weavec_codec.cpp
```

## Platform boundary

The `program` headers and translation units use platform-independent fixed-width identities and C++ standard library types. The standalone contract test executable links through the standard C++ runtime.

## Handoff state

The compiler and runtime successor plans are stored separately in `development/phase-3-compiler/ImplementationPlan.md` and `development/phase-3-runtime/ImplementationPlan.md`. Both consume the same reviewed `program` contract and proceed independently after the shared commit.

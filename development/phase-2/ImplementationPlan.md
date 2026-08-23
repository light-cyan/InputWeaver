# Phase 2 CompiledProgram Implementation Plan

## Objective

Phase 2 designs and implements the complete immutable `CompiledProgram` contract that separates future Weave compilation from future rule execution. Phase 2 ends when the contract can be constructed, canonicalized, validated, inspected, and tested without a compiler, runtime VM, Windows hook, or Win32 header.

## Authoritative inputs

- `docs/language/grammar.v1.md` defines the Weave v1 syntax and user-visible semantics that the representation must be able to encode.
- `development/phase-3-runtime/ImplementationPlan.md` defines the dispatcher, scheduler, cancellation, mapping, and output-ownership behavior that the representation must drive.
- `development/phase-2/CompiledProgramDesign.md` is the normative representation and validation contract.
- The Phase 1 input and runtime contracts under `src/input/` and `src/runtime/` remain platform independent.

## Phase boundary

Phase 2 includes the `CompiledProgram` data model, canonicalization, structural validation, deterministic inspection, `.weavec` field codec, fixtures, and contract verification. Compiler construction belongs to Phase 3 compiler, and executable rule processing belongs to Phase 3 runtime.

The compiler and runtime start only after the Phase 2 completion gate. They are separate successor phases that consume the frozen Phase 2 contract and do not synchronize their implementation progress with each other.

## Deliverables

- `src/program/compiled_program.hpp` and `src/program/compiled_program.cpp` define every strong ID, primitive, table record, storage field, immutable accessor, builder, canonicalizer, and finalizer in the contract.
- `src/program/program_validator.hpp` and `src/program/program_validator.cpp` recompute requirements and reject malformed storage before an immutable handle can exist.
- `src/program/program_dump.hpp` and `src/program/program_dump.cpp` produce a deterministic complete diagnostic dump.
- `src/program/weavec_codec.hpp` and `src/program/weavec_codec.cpp` implement the canonical `.weavec` field encoding and bounded decoding contract.
- `tests/program/compiled_program_fixtures.hpp` and `tests/program/compiled_program_fixtures.cpp` construct the tap, complete mapping, conditional repeat, and pause-control programs without a compiler.
- `tests/program/compiled_program_tests.cpp` verifies valid construction, canonicalization, immutable access, stable golden dumps, and representative corruption of every cross-table subsystem.
- `development/phase-2/Verification.md` records the completed verification commands and evidence.

## Work packages

### P2.1: representation

- Define the invalid sentinel, strong IDs, ranges, UTF-8 source spans, duration representation, resolved controls, event keys, target settings, typed values, expression instructions, action instructions, ordinary rules, pause-control rules, mappings, requirements, and debug records.
- Define `CompiledProgramStorage` as the complete mutable construction shape.
- Define `CompiledProgram` as an immutable owner with const references and spans only.

### P2.2: construction and canonicalization

- Provide `CompiledProgramBuilder`, explicit requirement derivation, and move-only finalization into `std::shared_ptr<const CompiledProgram>`.
- Canonicalize and deduplicate strings, controls, value references, number constants, and duration constants while remapping every dependent ID.
- Sort mapping slots, pause-control buckets, and ordinary event buckets deterministically while preserving mapping references and global source rule order.
- Return validation errors and no program when finalization fails.

### P2.3: structural validation

- Validate table limits, checked IDs and ranges, UTF-8 and API-bound strings, source lines and spans, typed user values, canonical pools, and debug coverage.
- Validate expression stack types, operator signatures, forward branches, merge states, result paths, declared stack depth, and instruction operands.
- Validate action references, expression signatures, writable values, repeat frames, local targets, cooperative backward edges, reachable `End`, and declared ownership bounds.
- Validate pause-control and ordinary event bucket ordering and coverage, rule invariants, mapping links, globally unique source ordinals, exact merged control uses, and exact recomputed requirements.
- Bound validation output to `kMaximumProgramValidationErrors`.

### P2.4: deterministic inspection

- Dump every semantic table, requirement, instruction operand, range, ID, and retained source span in stable canonical order.
- Use a locale-independent number format and explicit invalid-ID spelling.
- Treat the dump as a diagnostic and golden-test format, not a serialized executable format.

### P2.5: persistent artifact codec

- Encode every `CompiledProgram` field in the normative payload order with fixed-width little-endian scalars and explicit record fields.
- Decode the 16-byte header and payload under configured byte, collection, and string limits.
- Route decoded storage through `FinalizeCompiledProgram` and expose binary decoding errors separately from structural validation errors.
- Verify deterministic bytes, exact payload bounds, truncation handling, scalar checks, capacity limits, trailing-field handling, and full program round trips.

### P2.6: contract fixtures and tests

- Finalize the required tap fixture and prove event, action, control-use, ownership, and timing metadata.
- Finalize the required complete mapping fixture and prove mapping slot, mapping descriptor, rule, and repeat capability metadata.
- Finalize the required conditional repeat fixture and prove typed user state, expressions, constants, repeat frames, cooperative back edges, and action gaps.
- Finalize the required pause-control fixture and prove the dedicated index, stop-only delivery, synchronous effects, and zero task requirements.
- Freeze the complete dump of each fixture with a deterministic golden hash.
- Reject corrupt IDs, ranges, stack merges, backward jumps, event buckets, mapping links, requirements, control requirements, and debug spans.
- Prove canonicalization makes equivalent construction histories produce identical dumps.

### P2.7: verification and freeze

- Build the application, Phase 1 test executable, and platform-independent CompiledProgram test executable under the strict warning policy.
- Run both automated test suites and `-fanalyzer` for the new `program` implementation.
- Audit that the new `src/program/` files contain no Win32 includes or Win32 types.
- Run `git diff --check` and record the final evidence.
- Mark the shared program contract frozen for the two successor phases after review and commit.

## Completion gate

- Every field and invariant in `CompiledProgramDesign.md` has an implementation path and validation path.
- The only way to obtain a `CompiledProgram` handle is successful finalization of canonical validated storage.
- The four required fixtures finalize and their deterministic dumps match frozen golden values.
- Every required fixture encodes, decodes, finalizes, and reproduces the same dump and `.weavec` bytes.
- Representative corruptions across every table family are rejected with bounded structured errors.
- The contract implementation and tests compile without Win32 headers.
- The strict build, all automated tests, `program` `-fanalyzer`, platform-dependency audit, and `git diff --check` pass.
- `development/phase-2/Verification.md` and `AGENTS.md` identify Phase 2 as complete.

## Successor handoff

After the completion gate is reviewed and committed, create the compiler phase and runtime phase from the same Phase 2 commit. The compiler follows `development/phase-3-compiler/ImplementationPlan.md`; the runtime follows `development/phase-3-runtime/ImplementationPlan.md`. Each phase consumes the frozen shared contract and advances independently. A future integration phase combines completed artifacts.

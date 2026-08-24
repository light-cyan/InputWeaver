# Phase 5 Integrity Hardening Modification Plan

## Delivery Principles

- Preserve the compiler and executor command-line interfaces unless a demonstrated correctness requirement forces a documented change.
- Preserve `.weavec` compatibility when the existing format can represent the corrected behavior; introduce a version change only when the shared artifact contract cannot express a required invariant.
- Keep hook-path operations nonblocking and bounded.
- Use only the C++ standard library and the Windows API.
- Make structural moves with behavior-preserving tests before changing algorithms.
- Remove unused abstractions instead of generalizing them without a second current consumer.
- Keep generated binaries and transient validation output under `bin/`; retain only explicitly verified validation assets.

## Stage 1: Characterize Current Boundaries

1. Add an end-to-end Windows output-preparation test matrix for virtual keys, unqualified scan codes, `E0` scan codes including numeric-keypad Enter, `E1` Pause, mouse buttons, and unsupported native identities.
2. Add a deterministic concurrent diagnostic test with at least two runtime producers and one consumer; assert that every accepted record is unique and every rejected record increments the correct drop counter.
3. Add maximum-catalog normalization fixtures and expose a test-only match counter so lookup complexity is asserted without a timing-sensitive benchmark.
4. Record the present public and internal symbol call graph for the redundancy candidates in the problem statement; classify each as required product API, private implementation detail, or removable code.

Exit criterion: the current defects are reproduced by focused tests, and behavior that must remain stable has characterization coverage.

## Stage 2: Rebuild Input Ownership and Correct Native Output Identity

### 2.1 Split Platform-Neutral and Windows-Native Input Types

1. Inventory every type and constant in `src/input/input_types.hpp` by producer, consumer, and semantic domain.
2. Retain only shared platform-neutral concepts with current cross-module use, such as device class, transition, origin, decision, and neutral coordinates where justified.
3. Move raw hook flags, scan code, mouse data, extra information, virtual-key constants, Windows native input records, output recipes, and output batches under `src/platform/windows/runtime/`.
4. Move `ActionQueue` to the Windows runtime module because it currently has one Windows product consumer; extract a generic support queue only if another independent module requires the same abstraction.
5. Rename native and normalized event records so `WindowsNativeInputEvent` and `RuntimeInputEvent` cannot be confused.
6. Correct the existing dependency audit so it enforces the documented boundary that `src/input/` cannot include or encode platform-native identities.

Exit criterion: platform-neutral modules contain no Windows virtual-key values or native hook/injection fields, and the Windows transport types can represent every supported authored native identity without information loss.

### 2.2 Preserve Windows Output Recipes

1. Define a Windows-owned output recipe that can represent virtual-key fallback, exact scan code, extended qualifier, mouse button data, and transition flags without reverse mapping.
2. Keep `RuntimeOutputRequest` platform-neutral and use `ActivatedControl::backendToken` to retrieve the complete committed Windows recipe inside the Windows output port.
3. Carry that recipe through the bounded Windows output queue and construct `INPUT` directly from it in the injector.
4. Grant output capability only when the exact recipe can be emitted; reject unsupported `E1` or exceptional sequences during activation instead of silently changing identity.
5. Remove virtual-key hard-coded reconstruction paths that are no longer required after exact recipes are available.

Exit criterion: every accepted control in the Stage 1 matrix preserves its authored identity, and unsupported identities fail activation with a stable error.

### 2.3 Make Runtime Diagnostic Publication Multi-Producer Safe

1. Add nonblocking producer admission around the existing runtime SPSC ring or replace it with a proven bounded MPSC representation; the selected design must never wait on the low-level hook thread.
2. Count producer contention as a dropped runtime record when a nonblocking admission guard is used.
3. Document producer and consumer ownership beside the ring declarations.
4. Verify shutdown, target-loss, foreground-loss, hook, output, and log-worker interleavings under the concurrent test.

Exit criterion: no accepted record is overwritten, every rejected record is counted, and ThreadSanitizer-independent deterministic tests cover the critical interleaving.

### 2.4 Preserve Platform Launch Errors

1. Replace the launcher result-only return with a platform-neutral result record containing category and numeric platform detail, or add a diagnostic callback owned by the platform adapter.
2. Serialize both fields in runtime JSONL and include stable names for portable categories.
3. Cover executable resolution, invalid command, access denial, allocation failure, cancellation, and `CreateProcessW` failure.

Exit criterion: the observable failure contains the same Win32 error captured by the launcher without adding Windows dependencies to `src/runtime/`.

## Stage 3: Correct Runtime Error Semantics and Internal Ownership

### 3.1 Decompose `ProgramRuntime::Impl`

1. Extract a private active-program state owner for immutable program data, mutable values, physical state, cancellation generation, and lifecycle flags.
2. Extract dispatch-transaction planning and commit so all-or-nothing task and mapping reservation has one invariant boundary.
3. Extract task scheduling and task-slot lifecycle, including deadlines, backoff, instruction budgets, and output budgets.
4. Extract output ownership and rate limiting, including press, repeat, release, cancellation cleanup, and generation checks.
5. Keep diagnostic publication and metrics aggregation behind narrow interfaces that do not expose storage internals.
6. Preserve `ProgramRuntime` as the public facade and avoid introducing cross-module public classes solely to reduce file length.

Exit criterion: each private component has focused tests, ownership is acyclic, and no component needs unrestricted access to the entire previous `State` structure.

### 3.2 Correct Activation Error Quantities

1. Give each value-slot failure a subject that identifies state, number, or duration capacity.
2. Report the requirement and capacity from the same failing dimension.
3. Represent invalid executor scheduler and output-rate configuration separately from compiled-program capacity rejection.
4. Add tests for every failing dimension and assert the complete `code`, `subject`, `required`, and `available` record.

Exit criterion: every activation error quantity names one precise failed limit and the console message reports that same limit.

## Stage 4: Remove Avoidable Work

### 4.1 Index Windows Native Controls at Activation

1. Build direct virtual-key, unqualified scan-code, `E0` scan-code, `E1` scan-code, and mouse-identity lookup tables in `CommitActivation`.
2. Resolve overlap conflicts before commit so each native identity maps to at most one accepted input control.
3. Keep `backendToken` stable for output recipes independently of input lookup order.
4. Replace the `committed_` scan in `Normalize` with indexed lookup.

Exit criterion: normalization performs a bounded number of table accesses independent of activated-control count.

### 4.2 Materialize Compiler Products on Demand

1. Introduce an internal requested-product mode for validate, compile, and dump.
2. Make successful finalization independent of whether an artifact byte vector was requested.
3. Encode `.weavec` only for compile and format the deterministic dump only for dump.
4. Preserve existing diagnostics, exit codes, deterministic dump text, and artifact bytes.

Exit criterion: instrumentation tests prove that validate calls neither encoder nor dump formatter, compile calls only the encoder, and dump calls only the formatter.

## Stage 5: Remove Redundancy and Narrow Public Surfaces

1. Move the shared UTF-8 validator to `src/support/` and use it from compiler source loading and program validation.
2. Keep one compiler path-to-UTF-8 helper in the smallest owning source or a domain-independent support file when multiple modules require it.
3. Add a Windows-local RAII handle type and ordinal comparison helper in one owning Windows support header, then remove the copies in process context and process locator.
4. Remove the unused physical-output packing helpers after confirming that the corrected ownership design does not need them.
5. Remove `CompiledProgramBuilder::TakeStorage` and any unused const storage accessor that has no product or test requirement.
6. Remove queue pair and commit operations, their result enum, and test cases that exist only to exercise those unused operations.
7. Make the production queue publication path the only adapter path and remove `PublishRuntimeBatchToActionQueue` if the restructured Windows session does not use it.
8. Remove `LocateStatusName` and evaluate runtime value readers against a concrete inspection interface; retain only readers that a current product or focused test consumes.
9. Remove diagnostic JSON fields and enum values without a production producer, or connect them to an already existing current fact when that fact is required by the validation contract.

Exit criterion: repository-wide symbol search finds no declared product surface with only declaration, definition, and self-purpose test references; duplicate helper algorithms have one owner.

## Stage 6: Complete Verification and Documentation

### 6.1 Verification Coverage

1. Extend static analysis to every tracked `.cpp` under `src/program/`, `src/compiler/`, `src/runtime/`, `src/ui/cli/`, and `src/platform/windows/`.
2. Make the analyzer gate fail when a new implementation file is not classified rather than relying on a silently incomplete fixed list.
3. Make the existing dependency audit enforce the documented ownership of `src/program/`, `src/input/`, Windows-native transport, and the new private runtime components.
4. Add a current `script/verify_phase5.bat` that runs builds, all tests, complete analyzers, dependency audits, CLI checks, validation self-tests, documentation checks, and `git diff --check`.

### 6.2 Current Windows Validation

1. Create current assets under `validation/` outside `validation/legacy/` only after the correctness and structure stages stabilize.
2. Compile the tracked source during the gate and compare it with the retained artifact when retaining an artifact remains necessary.
3. Generate a validation manifest containing SHA-256 hashes for the `.weave` source, `.weavec` artifact, compiler executable, executor executable, JSONL evidence, console evidence, and verifier.
4. Make the verifier recompute every hash and reject a path-only match.
5. Repeat the visible Windows acceptance procedure for scan-code identity, foreground loss, startup-held inputs, cancellation cleanup, process launch diagnostics, log-drop accounting, and physical force stop.

### 6.3 Documentation

1. Update `docs/runtime-boundaries.md` with the output queue, batch size, corrected activation quantity semantics, and every emitted console metric.
2. Record the internal requested-product behavior and unchanged public CLI in the Phase 5 completion record.
3. Update `AGENTS.md` to name `script/verify_phase5.bat` only after that command exists and passes.
4. Move this phase directory to `development/legacy/` as a content-preserving snapshot only after every completion criterion is met.

## Final Acceptance Criteria

- All focused and repository-wide automated tests pass.
- Every accepted Windows raw control preserves exact output identity through injection preparation.
- Runtime diagnostic publication is multi-producer safe, nonblocking, and accurately counted.
- Native control lookup is independent of activated-control count.
- Validate, compile, and dump materialize only their requested products.
- `src/input/`, `src/runtime/`, and `src/platform/windows/` have explicit non-overlapping ownership, and `src/input/` contains no Windows-native transport representation.
- `ProgramRuntime` retains one public facade with independently testable private components.
- Duplicate helpers and unowned product surfaces identified in the problem statement are removed.
- Static analysis and dependency checks cover every tracked implementation file.
- Current documentation matches all enforced capacities and emitted metrics.
- The hash-bound Phase 5 Windows validation manifest and evidence pass `script/verify_phase5.bat`.

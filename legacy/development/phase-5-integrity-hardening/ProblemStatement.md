# Phase 5 Integrity Hardening Problem Statement

## Objective

Phase 5 restores exact platform-control behavior, trustworthy bounded diagnostics, predictable hook latency, and clear internal ownership while removing product code that has no current responsibility. Correcting the mixed platform-neutral and Windows-native responsibilities in `input_types.hpp` is the primary structural objective because the current transport model causes correctness defects and obscures module ownership.

The public command-line workflow remains `.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe`. The phase does not add a language feature, a third-party dependency, or an in-memory compiler-to-runtime path.

## Current Baseline

The compiler, shared-program, runtime-core, and Windows-adapter test suites pass. The current include graph respects the top-level compiler, program, runtime, CLI, and Windows-platform boundaries. Supplemental GCC analyzer runs also pass for the implementation files omitted by the current analyzer scripts.

The retained combined Phase 3 showcase is stored under `validation/legacy/phase-3/`, while the retained Phase 4 Windows safety assets and their combined gate are stored under `validation/legacy/phase-4/`. These directories are content-preserving snapshots and are outside current acceptance authority because their retained `.weavec` files do not correspond to their source bytes and the evidence does not identify the executed artifact by hash.

The product trust boundary accepts `.weavec` files produced by `InputWeaverCompiler.exe`. Structural loading checks protect format correctness and compatibility under that trusted-origin contract.

## Primary Architecture Problem

### P5-ARC-001: Critical Input Boundary Mixes Neutral Semantics with Windows Transport

`src/input/input_types.hpp` currently supplies the shared vocabulary between hooks, the Windows session, the runtime adapter, the output queue, the injector, and diagnostics. It defines device, origin, transition, and forward-or-suppress decisions; a raw `InputEvent`; an output `Action` and `ActionBatch`; process and coordinate aliases; queue capacities; and physical-output packing helpers.

Several members are native Windows transport details despite the platform-independent directory: virtual-key-valued constants, scan code, raw hook flags, mouse data, extra information, and an output code interpreted by `SendInput`. All production uses of the named control constants are Windows-specific. Meanwhile the normalized `RuntimeInputEvent` lives in `src/runtime/runtime_types.hpp`, so the nominal input module does not own the normalized event model described by the repository layout.

`src/runtime/action_queue.hpp` is also used only by the Windows runtime session and Windows adapter tests. Its element type is the Windows-oriented `ActionBatch`, while its pair and commit APIs have no product caller.

This ownership ambiguity contributes directly to P5-COR-001 because the generic output DTO cannot represent an authored Windows scan-code recipe.

Required property: `src/input/` contains only genuinely platform-neutral semantic types with multiple domain consumers; raw Windows hook records, native control constants, injection recipes, and the Windows output queue belong under `src/platform/windows/`. A generic bounded queue belongs in `src/support/` only if a second independent consumer justifies the abstraction.

## Correctness and Observability Problems

### P5-COR-001: Authored Windows Scan-Code Identity Is Lost on Output

`WindowsControlCatalog::Resolve` retains an authored Windows scan code and its none, `E0`, or `E1` qualifier in `WindowsControlBinding`. `WindowsRuntimeOutputPort::Publish` then emits only the mapped virtual key, and `InputInjector` maps that virtual key back to a scan code. The reverse mapping is not identity-preserving.

For example, `Windows.ScanCode(0x1C, E0)` resolves to `VK_RETURN`, but the injector reconstructs scan code `0x1C` without the extended flag and therefore emits the main Enter identity instead of the numeric-keypad Enter identity. Other scan codes can collapse to a different virtual-key-derived scan code.

The defect is enabled by the transport shape: `Action` carries only one generic `code` field and cannot represent the complete Windows output recipe. Existing tests confirm that a binding stores its scan qualifier but do not verify end-to-end authored identity through publication and injection preparation.

Required property: every accepted raw Windows output control must either preserve its authored native identity through the queue and injector or be rejected during activation as lacking output capability.

### P5-CON-001: The Runtime Diagnostic Ring Has Multiple Producers

`DiagnosticLog::runtimeRing_` is an `SpscDiagnosticRing`, whose write index is safe only for one producer. Runtime diagnostics can be drained after low-level hook handling and from output-thread target-loss or target-eligibility handling. Two producers can therefore read the same write index, write the same slot, and both publish the same next index.

The overwritten record is not observed as a capacity rejection, so `runtime_log_drops` can remain zero even though a record was lost. This invalidates the meaning of a zero-drop acceptance result.

Required property: runtime-diagnostic publication must remain nonblocking on the hook path, must have defined multi-producer ownership, and must count every rejected record.

### P5-DIA-001: Process Launch Diagnostics Discard the Platform Error

`WindowsProcessLauncher` records `ExecutableResolutionError` and the Win32 error returned by resolution or `CreateProcessW`. The platform-independent `RuntimeProcessLauncher` interface returns only `RuntimeLaunchResult`, and the runtime diagnostic stores only that coarse result. The Windows session never adds the retained platform error to a product-visible record.

Required property: a launch failure record must identify the portable failure category and the platform error without making the runtime core depend on Windows headers.

### P5-DIA-002: Activation Error Quantities Can Describe Different Limits

The combined value-slot check reports the maximum of state, number, and duration requirements together with the minimum of their capacities. Those values can come from different slot kinds. Scheduler and output-rate configuration failures can also report a valid sibling quantity while omitting the field that actually failed.

Required property: `code`, `subject`, `required`, and `available` must describe the same failing dimension, and invalid executor configuration must be distinguishable from a compiled-program capacity failure.

## Efficiency Problems

### P5-PERF-001: Native Control Normalization Scans Every Active Binding

For each keyboard or mouse message delivered to the low-level Windows hook, `WindowsControlCatalog::Normalize` walks `committed_` from the beginning until one binding matches. With `C` activated controls, each native event performs up to `C` `Matches` checks, including events that are irrelevant to the program.

This does not mean the runtime scans every rule. The issue occurs earlier, while converting a native hook event into one compiled `ControlRefId`. A program with ten controls performs at most ten binding checks per event; a program at the documented 4096-control capacity can perform approximately 4096 checks for each key-down and key-up event on the system input path.

Required property: activation must build lookup tables keyed by mouse identity, virtual key, and scan-code qualifier so native normalization performs constant-time lookup or examines only a small collision list.

### P5-PERF-002: Every Compiler Command Materializes Every Output Form

`CompileValidatedSource` always calls both `DumpCompiledProgram` and `EncodeWeavec`. Consequently, `compile` allocates a complete text dump that it does not present, `dump` allocates a binary artifact that it does not write, and `validate` creates both before clearing the dump.

The compiler dump is a deterministic human-readable serialization of the finalized compiled program. It prints settings, derived requirements, canonical pools, expression and action instructions, rules, buckets, and debug/source mappings. It is useful for inspection, golden tests, determinism checks, and save-load equivalence. It is not Weave source, it is not the `.weavec` binary, and the executor does not consume it.

Required property: validation builds only the finalized in-memory program, compilation additionally encodes `.weavec`, and dump additionally formats the deterministic text representation.

## Additional Internal Structure Problem

### P5-ARC-002: `ProgramRuntime::Impl` Is a Single Internal Subsystem Boundary

`src/runtime/program_runtime.cpp` contains approximately 2790 physical lines. Its active state and implementation coordinate activation, variables, `PAUSE`, physical synchronization, rule dispatch, event transactions, mappings, task slots, scheduling, cancellation generations, output ownership, output rate limiting, diagnostics, metrics, and the task thread.

The public `ProgramRuntime` facade is cohesive and should remain the runtime entry point. The problem is that private responsibilities cannot be tested or changed independently, invariants are distributed across distant functions, and a small ownership or scheduling change requires reasoning about most of the file.

Required property: private state must be grouped by dispatch, scheduling, output ownership, mutable values, diagnostics, and metrics while the public facade and one implementation control flow preserve behavior.

## Redundancy Problems

### P5-RED-001: Domain-Independent Helpers Are Reimplemented

- UTF-8 validation is implemented separately in `src/compiler/source.cpp` and `src/program/program_validator.cpp`.
- Filesystem-path-to-UTF-8 conversion is implemented twice inside the compiler module.
- Windows handle ownership and ordinal case-insensitive comparison are implemented independently in `process_context.cpp` and `process_locator.cpp`.

Required property: one owning utility implements each stable primitive, and callers do not copy algorithms across module boundaries.

### P5-RED-002: Product Surfaces Exist Without Product Callers

- `PackPhysicalOutputState`, `PhysicalOutputGeneration`, and `PhysicalOutputIsDown` have no caller.
- `CompiledProgramBuilder::TakeStorage` has no caller.
- `ActionQueue::TryPushPair`, `ActionQueue::TryPushWithCommit`, and `ActionQueuePushResult` are exercised only by tests and have no product path.
- `PublishRuntimeBatchToActionQueue` is a public adapter helper used only by tests; production supplies its own publication thunk.
- `LocateStatusName` has no caller.
- `ProgramRuntime::ReadUserNumber` and `ReadUserDuration` have no caller and no current observable interface that requires them.

Each surface must either have a current owner and behavior test tied to a product facility or be removed. Symmetry, possible future use, and test-only exercise are not sufficient ownership.

### P5-RED-003: Diagnostic Schema Fields Are Permanently Defaulted

`HookDiagnosticRecord::ruleId`, `queueResult`, and `aggregateCount` are not assigned meaningful production values. The JSON formatter therefore emits fields that imply observability but currently carry only defaults. `QueueResult::CommitRejected` also corresponds to an unused queue API.

Required property: each emitted field represents an observed current fact; fields without a producer are removed together with their unused vocabulary.

## Verification and Documentation Problems

### P5-VER-001: Static Analysis and Dependency Audits Do Not Cover All Owned Sources

The runtime analyzer omits `low_level_hooks.cpp`, `process_context.cpp`, and `process_locator.cpp`. Neither analyzer covers the shared program implementations. The dependency audit has no rule for the program module, so a future program-to-compiler, program-to-runtime, UI, or platform dependency would not fail the gate.

Required property: every tracked C++ implementation belongs to exactly one analyzer set and one dependency policy, and the gate fails when a new implementation is unclassified.

### P5-DOC-001: Runtime Boundary Documentation Is Incomplete

The runtime boundary guide does not include the 256-entry output queue or eight-action batch capacity and omits several console metrics already emitted by the executor. Activation quantity semantics also overstate what the current implementation guarantees.

Required property: the guide enumerates every enforced fixed boundary and every current console field after implementation behavior is corrected.

## Completion Standard

Phase 5 is complete only when the input transport boundary is platform-correct, the critical identity and diagnostic races have regression tests, native normalization has constant-time behavior, the input and runtime internals have explicit ownership, redundant surfaces are removed, all current C++ sources are covered by analysis and dependency policy, current documentation matches implementation, and the repository verification gate passes.

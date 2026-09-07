# Phase 5 Redundancy and Internal Structure Inventory

## Purpose

This inventory records the current producer and consumer graph for the redundancy and internal-structure findings in the Phase 5 problem statement. A symbol is a retained product surface only when a current product path or focused behavioral test requires it.

## Duplicate Primitive Implementations

| Primitive | Current definitions | Current product consumers | Classification and disposition |
|---|---|---|---|
| UTF-8 validation | `src/support/utf8.hpp::IsValidUtf8` | Compiler source loading and shared-program string validation | Required domain-independent primitive with one owner. |
| Filesystem path to UTF-8 | `src/compiler/path.hpp::PathToUtf8` | Source display-path construction and compiler file-read diagnostics | Required compiler primitive with one owner. |
| Windows handle ownership | `src/platform/windows/runtime/windows_support.hpp::UniqueHandle` | Token, process, and snapshot lifetime management | Required Windows primitive with one owner. |
| Ordinal case-insensitive path comparison | `src/platform/windows/runtime/windows_support.hpp::EqualOrdinalIgnoreCase` | Canonical path comparison, selector matching, and process enumeration | Required Windows primitive with one owner. |

## Surfaces Without Product Callers

| Surface | Declaration and definition graph | Current consumers | Classification and disposition |
|---|---|---|---|
| Physical-output state packing helpers | Former inline helpers in `src/input/input_types.hpp` | None | Removed. |
| `CompiledProgramBuilder::TakeStorage` | Former builder method | None | Removed. |
| `CompiledProgramBuilder::Storage() const` | Former builder method | None | Removed; current builder consumers use the mutable overload. |
| Queue pair and commit APIs | Former methods and result enum in the output queue | None | Removed with their self-purpose tests. |
| Queue inspection counters | Former approximate-size and rejected-push test accessors | None | Removed; boundary tests assert observable acceptance and rejection. |
| Test publication adapter | Former public output-queue publication helper | None | Removed; tests use the product publication path with a local queue thunk. |
| Process locator status naming | Former `LocateStatusName` surface | None | Removed. |
| Launcher retained-error accessors | Former launcher last-resolution and last-Win32-error state | None | Removed; each launch returns one complete outcome. |
| Process-context inspection surfaces | Former stored integrity getters and process-integrity query wrapper | None | Removed; initialization returns the current validation outcome. |
| `InputInjector::Tag` | Former injector accessor | None | Removed. |
| `ProgramRuntime::ReadUserNumber` | Former public facade method and `Impl` access | None | Removed. |
| `ProgramRuntime::ReadUserDuration` | Former public facade method and `Impl` access | None | Removed. |
| `ProgramRuntime::ReadUserState` | Public facade method and `Impl` access | Focused runtime tests inspect state mutation and event-snapshot semantics | Required test observability today; retain until a narrower runtime inspection interface owns the same current tests. |

## Diagnostic Fields Without Producers

| Field or value | Current write graph | Current read graph | Classification and disposition |
|---|---|---|---|
| `HookDiagnosticRecord::ruleId` | No current field | None | Removed because no production fact supplied it. |
| `HookDiagnosticRecord::queueResult` | No current field | None | Removed because no production fact supplied it. |
| `QueueResult::CommitRejected` | No current value | None | Removed with the unused queue commit API. |
| `HookDiagnosticRecord::aggregateCount` | No current field | None | Removed because records are not aggregated. |

## Input Transport Ownership Graph

| Current record or alias | Producers | Consumers | Current semantic owner |
|---|---|---|---|
| `InputEvent` | Windows low-level keyboard and mouse normalization | Windows session diagnostics, force-stop recognition, and control catalog normalization | Windows runtime; rename to `WindowsNativeInputEvent`. |
| `Action` and `ActionBatch` | Windows runtime output port | Windows action queue, session output thread, injector, and injection diagnostics | Windows runtime; replace the generic code field with an exact Windows output recipe. |
| `SelfTag`, raw flags, scan code, mouse data, timestamp, and extra information | Windows hooks and executor setup | Windows classifier, diagnostics, injector, and session | Windows runtime or Windows diagnostics according to the consuming fact. |
| `DeviceKind`, `InputOrigin`, `Transition`, `InputDecision`, and neutral screen coordinates | Windows adapter and platform-independent runtime boundary | Runtime core, Windows route adapter, hooks, and diagnostics | Shared platform-neutral input vocabulary. |
| `ActionQueue` | Windows output publication | Windows session output thread | Windows runtime; no generic support consumer exists. |

## `ProgramRuntime::Impl` Responsibility Graph

| Responsibility cluster | Current state and call path | Required private owner |
|---|---|---|
| Active program state | `State` owns immutable program data, activated controls, lifecycle flags, and responsibility-owned state records | One active-program lifetime owner. |
| Event dispatch transaction | `DispatchState` owns mapping state, the work queue, and bounded transaction scratch | One dispatch storage boundary used by plan-and-commit control flow. |
| Task scheduler | `SchedulerState` owns task slots, expression scratch, ready and timed order, suspension count, and the pump lock | One scheduler storage boundary. |
| Output ownership | `OutputState` owns global counts, publication sequence, and the output-rate window | One output storage boundary guarded by its ownership lock. |
| Mutable values and physical state | `MutableState` owns value locks, `PAUSE`, user values, and physical synchronization | One mutable-state storage boundary with focused read and expression views. |
| Diagnostics and metrics | `DiagnosticBuffer` and `MetricCounters` own their respective storage | Separate bounded diagnostics and snapshot aggregation. |

The public `ProgramRuntime` facade remains the integration boundary. Phase 5 extraction is private implementation decomposition and does not create additional App/UI entry points.

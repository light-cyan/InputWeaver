# Phase 19 Results

## Delivered behavior

- Debug uses the runtime's shared mouse observation, cycle, and occurrence values. Snapshot reads refresh and copy a coherent view under the runtime variable transaction lock, including unopened origins and typed zero periods. Debug observes Mouse for key-only programs as well.
- Named completions retain semantic cycle numbers and their raw-input correlation. Work reservations carry the exact tick identity, and each task publishes its existing fixed selection for every source through small bounded records.
- Protocol version 6 carries source definitions, complete live snapshots, normalized raw mouse data, completion records, and task selections. The pipe writer samples live state every 50 ms and preserves the existing final stream acknowledgment.
- The reducer correlates raw-first and cycle-first records, keeps numeric input out of pressed-control state, and retains older or empty task selections across live STATE replacement. A retained tick still supplies its trigger after its raw input leaves the bounded history.
- STATE renders Mouse and named current/latest views. EVENTS shows raw input and semantic cycle identities with boundary data. ACTION EXECUTIONS shows the actual trigger and complete fixed source selections. Mouse rows wrap in the existing viewports.
- JSONL mouse input includes position and all four normalized deltas. Pointer output includes its operation, requested arguments, and prepared origin/destination or wheel amount through the existing bounded diagnostic path.

## Requirement evidence

| Requirement | Authoritative implementation and verification |
| --- | --- |
| Named events, distance/time expressions, both field views, all Mouse fields, pointer actions, restart, and persisted types | `tests/compiler/compiler_mouse_tests.inc` covers compilation, field typing, declaration identity, deterministic artifact round trips, and dumps. |
| Distance and signed wheel accumulation, dynamic period latching, time phase, idle behavior, interpolated origins, and independent sources | `src/runtime/mouse_state.cpp`, `src/runtime/mouse_fields.cpp`, and `tests/runtime/mouse_state_tests.cpp`. |
| Exact own-cycle and cross-source selections, shared subscriptions, first access after wait, restart, and qualification lifecycle | `tests/runtime/program_runtime_mouse_tests.inc` exercises runtime tasks, live state, fixed views, and capture-independent semantic identities. |
| Windows physical normalization, fractional output, negative coordinates, reachable desktop edges, target checks, shared output order, and dry-run | `tests/runtime/windows_mouse_adapter_tests.inc` and `tests/runtime/windows_pointer_output_tests.inc` exercise the adapters and the compiled-program-to-native-output path using a fake sender or dry-run. |
| One mouse/cycle wire representation, complete state replacement, raw/cycle ordering, old and empty task selections | `tests/debug/debug_mouse_protocol_tests.inc` and `tests/debug/debug_mouse_client_tests.inc`. |
| Live writer sampling, source metadata, bounded per-source publication, capture restart, and ordered shutdown | `tests/debug/windows_mouse_debug_tests.inc` exercises the native server/client pipe with a snapshot callback. |
| Named current/latest STATE, raw and tick EVENTS, fixed task selection, and narrow rendering | `tests/ui/tui_mouse_tests.inc` renders the actual controller at wide and narrow dimensions and checks displayed values and identities. |
| Numeric JSONL and existing privacy/bounds | `tests/runtime/windows_mouse_diagnostic_tests.inc` verifies negative coordinates, fractional wheel amounts, pointer arguments, and prepared coordinates; the existing platform suite verifies privacy and diagnostic limits. |

## Verification

The following commands completed successfully:

```bat
script\build_runtime_tests.bat
script\test_runtime.bat
script\build_executor.bat
script\build_executor_tests.bat
script\test_executor.bat
script\build_tui.bat
script\build_app_tests.bat
script\test_app.bat
script\verify_project.bat
git diff --check
```

The final project gate rebuilt all four products and every test executable, passed every test suite, analyzed all 69 source implementations, and audited dependencies for all 69 implementations. New scenarios exercise supported behavior and normal state transitions. Mouse codec, reducer, server, and formatting additions reside in focused companion files and reuse the existing runtime, transport, and UI structures.

## Cross-phase handoff

The syntax and semantics baseline was committed first as `cbaf1b5`. Each implementation phase has a committed execution plan and a completion checkpoint:

| Phase | Execution plan | Completion |
| --- | --- | --- |
| 16: compiler and shared program | `88b086d` | `0f93830` |
| 17: platform-independent runtime | `cb0a6b7` | `3188e90` |
| 18: Windows observation and output | `f38c1c2` | `5f39892` |
| 19: Debug | `22ac9a6` | This phase's completion commit. |

Current language and operational behavior are recorded in `docs/grammar.md`, `docs/runtime-boundaries.md`, and `docs/tui-guide.md`. The shared Phase 16 design and Phase 19 execution records are retained as content-preserving phase archives.

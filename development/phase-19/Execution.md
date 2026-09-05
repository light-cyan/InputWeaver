# Phase 19: Mouse Debug State and Event Correlation

## Inputs and scope

Use the shared [mouse language design](../phase-16/Grammar.md), the current [language contract](../../docs/grammar.md), and Windows checkpoint `5f39892`. Deliver mouse observation and named source views in STATE, raw and periodic events in EVENTS, and fixed completed selections in ACTION EXECUTIONS. Complete JSONL numeric diagnostics and the cross-phase verification gate.

## Runtime observations

- Reuse the runtime's mouse observation and cycle value structures for debug snapshots. Place shared value types where runtime and debug can use them without exposing mutable accumulator internals.
- Add a coherent snapshot query under the existing variable transaction lock. Refresh pointer position and idle state, then copy the global observation and every source's current/latest views. Apply the same unopened-origin and typed-zero rules as expression field reads.
- Keep mouse observation active when a Debug port is attached, including programs whose rules only reference keys. Capture start/stop, refresh cadence, and source display do not reset accumulation, completion identities, or physical movement history.
- Carry the exact source and completed-cycle identity from dispatch through work reservation to the task's debug trigger. Emit each completed cycle with its existing semantic sequence and its originating raw input correlation, even if no subscription matches.
- Publish the task's already-fixed selection for each source when its execution record begins. Use small per-source records through the existing bounded producer queue; copy values immediately rather than retaining spans into task storage. Empty selections are explicit, and a selection can refer to a completion from before capture began.

## Protocol and transport

- Advance the local Debug protocol to version 6. Extend raw input with position and the four normalized deltas. Reuse one cycle codec for current state, latest completions, occurrence records, and task selections.
- CaptureStarted carries source definitions and a complete mouse snapshot. A periodic mouse-state message replaces the complete observation/source snapshot atomically in the reducer. Preserve existing scalar and array updates.
- Add completed-cycle and execution-selection records. A rule execution names its raw input and optional semantic cycle; per-source selection records attach to its execution marker. Keep semantic cycle identities independent of capture epochs.
- Sample live STATE on the writer side every 50 ms while capturing, using a runtime snapshot callback outside pipe and producer locks. This updates idle state without introducing high-frequency state records into the hook queue. Finalization completes the existing ordered stream and acknowledgment flow.
- Keep the current bounded transport and overflow recovery behavior. Use focused codec/server companion files for the mouse additions and avoid enlarging the existing implementation files unnecessarily.

## Client and presentation

- Keep raw events and named ticks in the existing recent-event model, with separate raw-input and semantic-cycle identities. Extend pending correlation to handle cycle and rule records that arrive before their raw input.
- Numeric mouse events do not enter pressed-control state. Preserve the existing down/up and repeat reduction paths.
- STATE shows current Mouse position, latest delta tuple, moving/idle values, and named current/latest source views. Display source names from declarations, current progress and remaining amount, origins/endpoints, net displacement/path length or signed wheel remainder, and explicit completion validity.
- EVENTS identifies raw mouse transitions and each named tick, including its source, completion identity, boundary data, and common raw input association.
- ACTION EXECUTIONS shows the actual trigger and its fixed per-source selections, including older or empty selections. Replacing the live STATE snapshot never changes a task's stored selection. Source rows are complete before presenting their selection as available.
- Put mouse formatting in a focused TUI companion using the existing numeric/time formatting, wrapping, and viewports. Retain the current three-region layout.
- JSONL hook records include x/y/dx/dy/wheel_x/wheel_y; pointer injection records include operation, requested numeric arguments, and prepared origin/destination or wheel amount. Preserve existing privacy redaction and bounded logging.

## Execution and verification

1. Implement the shared snapshot values, runtime query, semantic trigger propagation, and fixed selection publication. Test live state, multiple same-input completions, cross-source selection, first access after wait, and capture-independent identities.
2. Implement protocol codecs, server sampling/publication, and reducer correlation. Test positive round trips, raw/cycle ordering, complete state replacement, old task selections, empty selections, and capture restart with ongoing statistics.
3. Implement STATE/EVENTS/execution formatting and numeric JSONL records. Add positive rendering and diagnostic examples using declared source names, negative coordinates, fractional wheel data, and narrow viewports.
4. Update current documentation, run the commands below, record results, archive completed phase snapshots, and commit the completed phase. Preserve the shared Phase 16 design until this handoff is complete.

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

Completion requires compiler, generic runtime, Windows input/output, and Debug behavior to remain consistent with the current language contract and to pass the complete project gate.

# Phase 17: Platform-independent Mouse Runtime

## Inputs and scope

Use the approved [mouse language design](../phase-16/Grammar.md), the [compiler contract](../../docs/grammar.md), and compiler checkpoint `0f93830`. This phase implements observation, accumulation, field evaluation, task snapshot selection, restart, and pointer output through runtime ports. Windows sampling/injection belongs to Phase 18; Debug transport/presentation belongs to Phase 19.

## Common structures

- Add a small mouse-state component with one current-cycle representation for distance, duration, and signed wheel progress. Completed cycles copy the same numeric payload and carry a monotonically increasing semantic occurrence identity.
- Normalize each mouse input into position, pixel displacement, and horizontal/vertical detents. Keep the most recent four-component tuple independently of source statistics. Pointer polling updates position and observation time while preserving physical deltas and the last effective movement time.
- Reuse the existing variable transaction lock for consistent observation and period evaluation. Each input updates all qualifying sources before matching. Raw rules run first, followed by sources in declaration order and each source's cycles in completion order. All these conditions and newly opened periods see the same variable snapshot.
- Use existing rule buckets and task reservation. A task's preallocated completion-selection array copies each source's latest completed cycle at reservation; a tick overrides its own entry with the exact triggering cycle. All task expression paths use that selection, including their first read after a wait.
- Extend the existing output request with a pointer operation and numeric arguments, sharing publication order, cancellation generation, routing, output budgets, and result handling. Relative movement remains relative until the backend executes the request. Query pointer position through the route port and advertise pointer publication through the output port.
- Keep accumulation, field access, and runtime integration in focused companion implementations. Add positive tests rather than expanding the existing large test files.

## Period and observation decisions

- Latch a positive period on the first qualifying input after activation or restart. Before opening a period, `period`, `progress`, and `remaining` read as zero of the declared type. Coordinates use the current observed pointer until the first included input establishes the cycle origin; displacement, distance, and wheel progress are zero.
- Every completion immediately opens its next period using the same input's variable snapshot, even if there is no remainder. Subsequent variable changes affect the following opening. A failed dynamic period evaluation clears the incomplete period, reports a source diagnostic, and retries on the next qualifying input; it preserves an already completed record.
- Distance and wheel progress survive ordinary idle. Wheel progress is signed and reversals cancel the remainder. Movement origins use the start of the first included displacement. Boundary interpolation apportions one observed input across consecutive periods, conserving its displacement and path length.
- The first effective move starts a continuous time span and assigns its displacement to that span at elapsed time zero. Subsequent effective inputs less than `MOUSE_IDLE_TIMEOUT` apart extend it. An interval equal to the timeout starts a new span. Zero movement, scroll, and button reports do not extend physical movement.
- Time boundaries keep their logical phase. Apportion the displacement between two effective input timestamps proportionally across crossed boundaries. Idle observation clears the unfinished time cycle; stationary time produces no ticks. The latest completed cycle survives ordinary idle.
- Raw numeric input replaces all four latest delta components, filling inactive axes with zero. Keys, buttons, elapsed time, pointer polling, restart, and source resets preserve that tuple. Startup sets it to zero and starts idle time with `moving=off`.
- Activation, generation changes, and loss of source input qualification clear current and latest source records. Global physical observation continues. Source restart clears only that source; task completion selections already reserved remain unchanged.
- A missing completed view returns `valid=off`; other completed fields yield an expression fault. Such a condition does not match and reports a diagnostic; an action fault ends only its task. Existing short-circuit evaluation supplies the normal guard.

## Integration and verification

1. Implement the accumulation/field component with positive tests for distance overshoot and origins, signed wheel cancellation, time continuity and phase, dynamic latching, multiple sources, idle observation, and resets.
2. Integrate input transactions, live reads, completion selections, source restart, and generation reset. Test own-source and cross-source snapshots, multiple cycles from one input, waiting tasks, shared subscriptions, and first access after wait.
3. Integrate pointer publication through the common output envelope and capability checks. Test expression evaluation, request ordering, task interaction, and a fake pointer backend.
4. Update current language and runtime boundary documentation, run the canonical checks below, record results, and commit the completed phase.

```bat
script\build_runtime_tests.bat
script\test_runtime.bat
script\build_executor.bat
script\build_executor_tests.bat
script\test_executor.bat
script\build_compiler.bat
script\build_compiler_tests.bat
script\test_compiler.bat
script\analyze_all.bat
script\audit_dependencies.bat
git diff --check
```

The completed feature remains subject to the cross-phase `script\verify_project.bat` gate after Windows and Debug implementation.

## Verification prerequisite

The existing Windows Debug server shutdown test reproduced a disconnected-client race during phase verification. Connection teardown must notify a concurrently waiting `Stop()` after joining the writer, including when the writer exits normally on the connection-stop event. Apply this notification correction and use the existing shutdown tests to verify it.

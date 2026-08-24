# Phase 4 Verification Record

## Status

The complete automated Phase 4 gate passed on 2026-08-24 with MinGW-w64 GCC and G++ 15.1.0 on Windows.

The retained physical Windows keyboard evidence also passed on 2026-08-24 after the operator confirmed the visible target-window behavior and the implementation owner ran the evidence validator.

## Gate

The canonical command is `script\verify_phase4.bat`. It completed the Phase 3 build, compiler tests, shared-program tests, runtime tests, Windows adapter tests, compiler and runtime static analysis, dependency audit, and diff check. It then rebuilt both example programs and byte-compared their deterministic checked-in artifacts, verified the `--allow-exec` command-line authority boundary, exercised normal pre-loss completion, target-cancelled completion, and stale post-loss output fixtures through the example-owned retained-evidence validator, and checked the Chinese safety guide, example-owned manual test procedures, and canonical commands.

## Runtime Stress

The release-build `ProgramRuntimeTests.exe` completed 500 consecutive process-isolated runs with a five-second timeout per run. Every run exited successfully, including the production task-thread, continuously-ready scheduler, force-stop wakeup, target-transition transaction, output-rate, ownership cleanup, and timed cancellation fixtures.

## Evidence Matrix

| Safety boundary | Automated evidence |
| --- | --- |
| Reversible target eligibility | Mapping down and repeat generations become stale on foreground loss, release traffic remains publishable, fresh input works after return, a held source cannot remap, an in-flight event transaction aborts on a generation change, timed tasks cannot resume from a stale generation, immediate keyboard dispatch rejection and injection-time route rejection both become reversible ineligibility, and terminal target loss releases ownership. |
| Physical keyboard initialization | Windows bindings retain stable keyboard query recipes, left and right modifiers remain distinct, activated keyboard controls are seeded on the hook thread before ordinary message dispatch, startup-held sources create no output, unqueryable event sources forward until an observed release, unqueryable physical-state requirements fail activation, and startup-seeded modifiers retain the physical force-stop path. |
| Bounded hook dispatch | Activation independently rejects excessive pause rules, ordinary rules, predicate steps, and mapping operations; exact-capacity fixtures activate; forged serialized requirement summaries fail structural validation; the maximum accepted event fixture executes within the derived bound; transient task and queue exhaustion fails open; PAUSE and variable locks use nonblocking acquisition. |
| Task progress and scheduling | Instruction and output budgets cancel only the producing task, zero-duration waits and gaps cannot reset progress, positive timed suspension resets progress, maximum continuously-ready tasks enter scheduler backoff, physical force stop wakes cancellation, and counting-semaphore wakeups complete without a lost-wakeup hang. |
| Output safety | Global ownership emits first-down and final-up edges; active mapping ownership is directly released by PAUSE cancellation, physical force stop, target loss, runtime output failure, and normal shutdown; output-rate exhaustion is task-local or mapping-local; cleanup ups bypass ordinary rate limits; stale generations cannot publish non-release work; output injection performs a final live-generation check; and backend cleanup failures retain fail-safe behavior. |
| Process launch authority | Runtime and Windows launcher permission default to denied, an exec-requiring artifact fails before control activation, denied Windows launch performs no native creation, explicit capacity grants retain launch behavior, and `--allow-exec` is accepted only in compiled-program mode. |
| Diagnostics | Target eligibility, physical synchronization, task-budget, output-rate, cancellation, ownership, mapping, launch, and failure records use the bounded runtime diagnostic path and preserve the existing dropped-record metric; blocking status messages and final post-flush diagnostic metrics are visible through redirected console capture, and compiled-program logging retains activated controls without requiring full input tracing. |

## Windows Acceptance Procedure

The current physical-keyboard manual test procedure is `example\phase-4-safety\ManualTest.md`. `example\phase-4-safety\run.bat main` and `example\phase-4-safety\run.bat startup-held` produce two JSONL files and two matching live console transcripts in the same directory. The runner does not enable full input tracing: activated keyboard controls and physical F12 remain observable while ordinary mouse movement is excluded. The physical operator performs and reports only the visible target-window observations; the implementation or acceptance owner runs `example\phase-4-safety\verify.bat` over all four artifacts. The validator rejects nonzero exits, injection or cleanup faults, retained output downs, C output allocated at or after the target-loss sequence in its generation, missing target transitions, missing F7 cancellation before C, missing budget or force-stop diagnostics, startup-held output before release, diagnostic drops, JSONL truncation, and malformed metrics.

## Retained Windows Evidence

The retained main evidence contains 480 records: 352 hook records, 53 injection records, and 75 runtime records. All 53 injection records completed without failure, 39 A output lifecycles were paired, ten target-loss transitions each produced a reversible target cancellation and ten target returns were recorded, the instruction-budget diagnostic occurred once, no launch failure occurred, and physical F12 produced the force-stop cancellation. The first F7 task completed C at output sequence 44 before the target-loss boundary at sequence 46; the second emitted B in generation 10 and reached target loss at sequence 48, cancellation generation 11, with no C allocated at or after that boundary.

The retained startup-held evidence contains 130 records: 90 hook records, 12 injection records, and 28 runtime records. No A output occurred before the first physical F6 release, six complete fresh A lifecycles occurred after release, and the modifiers held before activation completed the physical F12 force-stop path. The resulting `Ctrl+Shift+A` target behavior is an expected composition of the retained physical modifiers and the fresh F6-to-A mapping.

Both console transcripts report exit code zero, zero injection failures, zero hook, injection, and runtime diagnostic drops, and no JSONL truncation. The session-wide maximum hook times were 199 microseconds for the main run and 35 microseconds for the startup-held run; the corresponding retained configured-control JSONL maxima were 84 and 35 microseconds. The operator confirmed the required visible mapping, foreground-loss cleanup, stale-task cancellation, continued responsiveness, process launch, startup-held behavior, and physical force stop.

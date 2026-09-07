# Phase 4 Keyboard Runtime Safety Plan

## Status

This plan is the active Phase 4 development authority for keyboard runtime safety.

## Objective

Phase 4 hardens the compiled keyboard mapping path against target eligibility changes, preexisting physical state, excessive synchronous dispatch work, non-suspending tasks, excessive output publication, process-launch authority mistakes, and shutdown or failure cleanup faults.

Completion requires every accepted program to operate within explicit synchronous and asynchronous resource limits, every owned keyboard output to have a reliable release path, and every safety transition to preserve the physical force-stop path.

## Baseline

- The implemented control flow is `.weave -> InputWeaverCompiler.exe -> .weavec -> InputWeaver.exe`.
- The compiler derives exact per-program requirements and the runtime validates structural and capacity requirements before activation.
- The runtime owns immutable program activation, physical key state, PAUSE state, rule dispatch, tasks, cancellation generations, mappings, and output ownership.
- The Windows executor owns target discovery, target validation, low-level keyboard capture, native normalization, output publication, injection, process launch, and session shutdown.
- The existing verification gate builds and tests the compiler, shared program contract, runtime core, and Windows adapters, performs static analysis, audits dependencies, and checks the working diff.

## Selected Safety Semantics

### Physical input

- Only non-injected physical candidates update runtime physical key state or trigger user rules.
- Physical key state is updated for every observed physical keyboard down or up transition regardless of current target eligibility or PAUSE state.
- A temporarily ineligible target causes physical events to pass through without creating mappings or tasks.
- Physical Ctrl+Shift+F12 remains independent of target eligibility, PAUSE state, compiled rules, task capacity, output capacity, and process-launch authority.
- A capacity or safety-budget rejection must not consume a physical event whose requested transaction was not committed.

### Target eligibility

- Target process liveness and temporary foreground eligibility are separate runtime conditions.
- Temporary foreground loss advances the cancellation generation, cancels tasks, clears active mappings, releases all owned outputs, and keeps the compiled program available for a later foreground return.
- Target process exit stops the current session after cancellation and output cleanup.
- Foreground return permits only future eligible events to dispatch; it never synthesizes a source down transition for a key that remained held while the target was ineligible.
- Output down and repeat publication requires current target eligibility immediately before queueing and immediately before injection.
- Output release publication bypasses target eligibility, cancellation generation, and output-rate rejection so cleanup always retains a publication path.

### Physical state initialization

- Runtime activation seeds queryable physical keyboard state before ordinary dispatch becomes available.
- Seeding physical state never creates a mapping, task, output transition, or rule match.
- A control used by a physical state expression must provide a reliable initial-state capability on the selected backend.
- An event-source control that cannot provide an initial state begins unsynchronized, forwards input without dispatch, and becomes synchronized after an observed physical up transition.
- Force-stop controls are seeded before the low-level keyboard hook begins ordinary event processing.

### Synchronous dispatch

- Activation enforces explicit limits for pause rules per event, ordinary rules per event, predicate instructions per event, mapping operations per event, transaction items per event, and tasks per event.
- The limits are checked against the exact `ProgramRequirements` values derived from the immutable compiled program.
- No source loading, process launch, output injection, unbounded allocation, unbounded collection traversal, or task execution occurs on the low-level keyboard hook path.
- Every accepted hook-path loop is bounded by an activation-validated program requirement or a fixed runtime capacity.
- Programs exceeding a synchronous dispatch limit are rejected before hooks are installed.

### Task execution

- Every action instruction consumes a task progress budget.
- Task completion, task cancellation, or a positive-duration timed suspension resets the applicable progress interval.
- Cooperative yield, loop back edges, and zero-duration waits do not reset a non-suspending progress budget.
- A task that exceeds its instruction or output budget is cancelled, records one bounded safety diagnostic, and releases all task-owned outputs.
- A task-budget failure does not cancel unrelated tasks unless output cleanup fails or a session-wide safety circuit opens.
- The task worker applies a bounded backoff after a configured number of continuously ready scheduling quanta so ready-only work cannot monopolize a processor indefinitely.

### Output safety

- Global ownership remains the sole authority for publishing keyboard down and up edges.
- The first owner publishes down, the final owner publishes up, and cancellation cleanup releases ownership in reverse task order.
- Output releases are reserved safety traffic and are never rejected solely because ordinary output reached a rate limit.
- Ordinary output publication has a monotonic-time rate budget with a fixed-capacity implementation.
- Exceeding the ordinary output rate budget cancels the producing task or mapping transaction and initiates ownership cleanup.
- Queue capacity failure, partial injection, repeated injection failure, and cleanup publication failure retain fail-safe shutdown behavior.
- Stale cancellation generations cannot publish down or repeat transitions.

### Process launch authority

- Process launch permission defaults to denied in runtime capacities and Windows session construction.
- A compiled program requiring process launch is rejected before hooks are installed unless the current invocation explicitly grants process-launch authority.
- The current command line remains the authority boundary through an explicit `--allow-exec` option in compiled-program mode.
- Denied authority never resolves an executable, creates a process, or partially activates the program.
- Granted authority retains the existing direct process creation, executable resolution, working-directory, cancellation, and failure behavior.

## Work Package 1: Reversible Target Eligibility

### Runtime state model

- Add an explicit temporary target-eligibility state that is independent of program acceptance, fatal shutdown, and target process liveness.
- Refactor target-loss cancellation so temporary foreground loss performs generation invalidation and cleanup without permanently disabling future dispatch.
- Keep target process exit, force stop, fatal failure, and shutdown as terminal session conditions.
- Make physical state update precede target-eligibility dispatch decisions.
- Preserve PAUSE semantics after target eligibility returns.

### Windows foreground observation

- Add a bounded Windows foreground transition observer using the Windows event mechanism and the existing target process identity.
- Deliver foreground changes to the runtime outside the low-level keyboard callback.
- Treat a missing foreground window, a foreground query failure, or a confirmed non-target foreground window as temporarily ineligible.
- Revalidate target process identity and liveness before accepting a foreground return.
- Keep the existing immediate injection-time route validation as a second guard against races.

### Required tests

- Acquire a full mapping, lose foreground eligibility before source release, and prove that the mapped output receives one final up transition.
- Return foreground eligibility and prove that a fresh source press can create a new mapping.
- Hold a source across foreground return and prove that no synthetic mapping down occurs until a physical up followed by a fresh down.
- Cancel waiting tasks on foreground loss and prove that their stale generations cannot publish later output.
- Race foreground loss against queued down, repeat, and up transitions and prove that releases remain deliverable.
- Exit the target process during active ownership and prove that cleanup completes before session teardown.

## Work Package 2: Physical Keyboard State Initialization

### Activation data

- Extend Windows control bindings with an explicit initial-state query capability and a stable query recipe.
- Expose activated event-source and physical-state requirements to the session seeding step without adding platform details to the runtime core.
- Add a runtime seeding entry point that updates physical state without dispatching rules.
- Finish all seeding before the runtime accepts ordinary transactions.

### Unsynchronized controls

- Track event-source synchronization separately from held state.
- Forward events from an unsynchronized source without dispatch until a physical up establishes a released baseline.
- Reject activation when a physical state expression requires a control whose initial state cannot be queried reliably.
- Record one bounded activation or synchronization diagnostic with the affected control identity.

### Required tests

- Seed left and right modifiers independently and verify held and idle predicates before the first captured event.
- Seed an already-held mapping source and prove that startup does not generate output.
- Release a startup-held source, press it again, and prove that only the fresh press dispatches.
- Activate an unqueryable event source and prove that the first unsynchronized press is forwarded until release establishes a baseline.
- Reject a physical-state requirement that lacks initial-state capability before hook installation.
- Seed force-stop controls and prove that the physical stop combination works when modifiers were held before startup.

## Work Package 3: Bounded Hook-Path Dispatch

### Capacity contract

- Add runtime capacities for maximum pause rules per event, maximum ordinary rules per event, maximum predicate steps per event, and maximum mapping operations per event.
- Validate every new capacity in `ProgramRuntime::Activate` alongside the existing control, value, mapping, stack, task, transaction, ownership, and diagnostic capacities.
- Preserve exact requirement derivation and structural validation so an artifact cannot understate its synchronous cost.
- Select conservative default capacities from worst-case release-build measurements and freeze the selected values in tests.
- Report the exact required and available values on activation failure.

### Hook-path implementation audit

- Audit the complete compiled-program keyboard callback path for allocation, blocking operating-system calls, unbounded scans, and lock duration.
- Ensure event-bucket and pause-bucket lookup remain bounded and independent of unrelated program rules.
- Ensure predicate evaluation uses the validated maximum instruction count and stack depth.
- Ensure diagnostics use fixed-capacity publication and never perform file I/O on the callback path.
- Preserve the existing hook-duration metric for Windows acceptance evidence.

### Required tests

- Reject each synchronous requirement independently when it exceeds its runtime capacity.
- Accept each requirement exactly at its runtime capacity.
- Tamper with serialized requirement values and prove structural validation rejects understated values.
- Dispatch a worst-case accepted event bucket and prove that evaluation stops within the derived step count.
- Saturate task and transaction capacity and prove the uncommitted physical event is forwarded.
- Exercise concurrent variable updates while dispatch evaluates predicates and prove bounded, deterministic results.

## Work Package 4: Task Progress and Output Budgets

### Task progress accounting

- Add fixed-width per-task instruction, output, and continuously-ready counters.
- Increment the instruction counter before executing each action instruction.
- Define a positive-duration timed suspension from the monotonic runtime clock rather than the authored duration alone.
- Cancel a task before executing the instruction that would exceed its configured budget.
- Preserve ownership cleanup and task-slot reuse after budget cancellation.

### Scheduler backoff

- Track consecutive scheduling quanta that complete with ready work and no positive-duration suspension.
- Apply a bounded monotonic-clock wait after the configured continuously-ready threshold.
- Wake the worker immediately for cancellation, force stop, shutdown, target eligibility loss, and newly committed external work.
- Keep timed deadlines ordered and prevent backoff from delaying an earlier cleanup or cancellation deadline.

### Output rate accounting

- Add a fixed-capacity monotonic output budget for down and repeat transitions.
- Associate task output with the producing task so a rate rejection cancels the correct owner.
- Apply mapping output limits without compromising the source-up cleanup path.
- Exempt up transitions and cleanup retries from ordinary rate accounting.
- Open a session-wide safety circuit only when cleanup cannot restore released ownership or repeated backend failures exceed the existing threshold.

### Required tests

- Cancel an unconditional non-suspending loop within the frozen instruction budget.
- Prove that zero-duration wait and zero-duration gap do not evade the progress budget.
- Allow a long-running loop that reaches positive-duration suspension within every budget interval.
- Cancel an excessive finite repeat before counter exhaustion or processor monopolization.
- Run the maximum task count with continuously ready work and prove scheduler backoff remains responsive to force stop.
- Exceed the ordinary output rate budget and prove that the producing task is cancelled and every owned control is released.
- Prove that cleanup up transitions still publish after ordinary output rate exhaustion.

## Work Package 5: Explicit Process Launch Authority

### Runtime and command line

- Change the default runtime capacity to deny process launch.
- Add process-launch permission to compiled-program session options and pass it into both runtime capacities and the Windows process launcher.
- Accept `--allow-exec` only with compiled-program execution.
- Reject a program whose derived requirements include process launch when authority is absent.
- Update the existing executable-launch acceptance command to grant authority explicitly.

### Required tests

- Deny a process-launch program before control activation and hook installation.
- Prove that denial performs no executable lookup and no process creation call.
- Grant authority and retain the existing successful launch behavior.
- Retain cancellation immediately before process creation.
- Retain bounded launch-failure diagnostics and task-local failure behavior.

## Work Package 6: Safety Diagnostics and Failure Policy

### Diagnostic contract

- Add bounded diagnostic reasons for temporary target ineligibility, physical-state synchronization failure, task progress exhaustion, output rate exhaustion, and process-launch denial.
- Preserve source spans for task-originated safety failures.
- Keep diagnostic loss observable through the existing dropped-record metrics.
- Keep diagnostic publication independent of cleanup success.

### Failure classification

- Treat a single task budget breach as task-local cancellation.
- Treat synchronous activation-limit failure as pre-activation rejection.
- Treat temporary target ineligibility as reversible generation cancellation.
- Treat target process exit, force stop, unrecoverable cleanup failure, repeated injection failure, and internal invariant failure as terminal session conditions.
- Preserve physical input forwarding whenever no committed consumption decision exists.

## Verification Plan

### Automated verification

- Add deterministic unit tests for every selected safety semantic and every new capacity boundary.
- Add runtime property tests that compare published down and up ownership edges across completion, cancellation, target changes, PAUSE changes, capacity rejection, backend failure, and shutdown.
- Add adversarial stress fixtures for maximum accepted rule buckets, maximum predicate work, maximum tasks, continuously ready loops, output pressure, and repeated target transitions.
- Extend artifact contract fixtures for every added requirement and reject forged requirement summaries.
- Run strict compiler and runtime warnings as errors and GCC static analysis on all changed safety paths.
- Preserve the compiler, runtime, program, Windows adapter, dependency, and diff gates from Phase 3.
- Add `script/verify_phase4.bat` as the canonical combined Phase 4 gate.

### Windows acceptance

- Compile and run a target-scoped keyboard mapping program with logging enabled.
- Use `example/phase-4-safety/run.bat` for both the main and startup-held modes so each run retains matching JSONL and live flushed console evidence beside the example.
- Keep full input tracing disabled for this keyboard-only acceptance while retaining hook records for activated keyboard controls and the physical force-stop chord.
- Hold a mapped source, change foreground ownership through the keyboard, release the source, and verify a paired output release and zero retained ownership.
- Return to the target and verify a fresh source lifecycle works normally.
- Start with selected modifiers and one mapped source already held, verify no startup output, release the mapped source while retaining the modifiers, and verify subsequent fresh input and the pre-seeded force-stop chord.
- Run a deliberately non-suspending task and verify bounded cancellation, continued physical responsiveness, and successful force stop.
- Verify process-launch denial without authority and successful activation with explicit authority.
- Require zero injection failures, zero cleanup failures, zero stale-generation output, zero dropped safety diagnostics, and a recorded maximum keyboard-hook duration.
- Assign visible target-window actions and observations to the physical operator, then have the implementation or acceptance owner run `example/phase-4-safety/verify.bat` over both JSONL files and both post-flush console transcripts.

## Implementation Order

1. Freeze the selected safety semantics in runtime and Windows adapter tests.
2. Implement reversible target eligibility and foreground transition observation.
3. Implement physical keyboard state seeding and unsynchronized-source handling.
4. Enforce synchronous dispatch capacities during activation.
5. Implement task progress, scheduler backoff, and output rate budgets.
6. Implement explicit process-launch authority.
7. Complete safety diagnostics, adversarial stress coverage, and Windows acceptance.
8. Add the combined Phase 4 verification gate and update current implementation documentation.

## Completion Criteria

- A full mapping cannot retain an owned output across temporary foreground loss, target exit, PAUSE cancellation, force stop, runtime failure, or normal shutdown.
- Startup with physical keys already held cannot trigger a mapping or action and cannot make the force-stop path unavailable.
- Every accepted program has explicit enforced bounds for synchronous keyboard dispatch work.
- A non-suspending task cannot monopolize a processor or publish unlimited output.
- Ordinary output exhaustion cannot block required output releases.
- A process-launch program cannot activate without explicit launch authority.
- All automated Phase 3 and Phase 4 gates pass from a clean build.
- The retained Windows acceptance evidence satisfies every listed safety metric and lifecycle assertion.

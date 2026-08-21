# Runtime Successor Phase Implementation Plan

## Objective

This successor phase implements deterministic execution of a validated immutable `CompiledProgram` against fake platform ports and then implements the Windows-facing runtime adapters required by that execution. It owns execution only and is complete when every frozen Phase 2 fixture behaves correctly under deterministic tests and all safety gates pass.

## Plan coverage

This is the complete implementation and verification plan for the runtime successor phase, not an initial subset or an exploratory sequence. Work packages R1 through R7 cover activation, deterministic core execution, safety behavior, diagnostics, and the Windows runtime adapters required to execute a validated program. Source compilation, application integration, TUI work, and installation are outside this phase.

The work packages may be refined into implementation checklists inside this directory, but such checklists may not reduce the objective, normative inputs, semantic obligations, tests, safety gate, or completion gate defined here.

## Normative inputs

- `docs/language/grammar.v1.md` is authoritative for user-visible Weave v1 matching, action, mapping, timing, cancellation, state, target, and input-origin semantics even though runtime tests consume fixtures rather than source text.
- `development/phase-2/CompiledProgramDesign.md` is authoritative for the immutable representation, instruction effects, structural invariants, resource requirements, activation contract, and Phase 1 output boundary.
- `src/core/compiled_program.*`, `src/core/program_validator.*`, and `src/core/program_dump.*` are the executable Phase 2 contract consumed by runtime code and fixture tests.
- `development/OpenDesignIssues.md` records active decisions that must be resolved before affected runtime behavior can pass its completion gate.

A contradiction between the language specification and the compiled-program contract is a phase blocker. The runtime does not reinterpret fields, infer missing semantics from source text, or create semantic side tables that are not derivable from the validated contract and platform capabilities.

## Independence rule

This phase starts from the reviewed Phase 2 completion commit and proceeds without synchronization with the compiler successor phase. It does not wait for lexer, parser, binder, or lowering work, consume compiler branch commits, share progress gates, or modify compiler-owned files. Tests obtain programs exclusively from the frozen Phase 2 fixture builders.

The normative Phase 2 design and frozen core implementation are dependencies. A discovered representation gap is recorded as a runtime-phase blocker for a future contract revision; this phase does not reinterpret fields, add runtime-private semantic side tables, or change the contract unilaterally.

## Owned paths

- `src/runtime/` contains activation, runtime values, physical state, expression VM, dispatcher, task VM, cooperative scheduler, mapping state, transactions, cancellation, and output ownership.
- `src/platform/windows/` may gain narrow runtime ports and adapters while preserving the existing normalization and injection boundary.
- `tests/runtime/` contains fake-clock, fake-output, fake-target, fake-launcher, capacity, cancellation, ownership, and fixture execution tests.
- `script/build_runtime_tests.bat` provides an independent strict build entry point when a dedicated script is useful.
- Runtime documentation remains under `development/phase-3-runtime/`.

This phase does not modify `src/compiler/`, compiler tests, source syntax, parsing, binding, or lowering.

## Input and boundary

```text
shared_ptr<const CompiledProgram>
    -> structural and activation validation
    -> RuntimeState
    -> normalized physical event
    -> event transaction
    -> mapping operation or task
    -> output primitive
    -> existing bounded Phase 1 injection boundary
```

Runtime tests never need `.weave` source compilation. Program source spans and strings are used only for diagnostics and task-thread `Exec` behavior.

## R1: activation and fixed storage

- Revalidate the program structurally and verify backend control capabilities, task pool, transaction scratch, queue, expression stack, repeat frames, ownership records, mapping state, value slots, hook steps, and process-launch policy.
- Allocate and initialize all mutable runtime storage before program publication or hook installation.
- Initialize user values, built-in `PAUSE`, settings-backed built-in durations, physical state, mapping slots, task pools, ownership pools, timer queues, and diagnostic snapshots.
- Make activation transactional and retain the previously active program when replacement activation fails.
- Retain the immutable program through `std::shared_ptr<const CompiledProgram>` and store typed IDs plus local positions in mutable runtime records rather than pointers into program tables.

## R2: expression VM

- Evaluate typed expression programs using preallocated stacks and resolved IDs only.
- Implement constants, values, physical held state, unary operations, binary operations, forward branches, short-circuit behavior, and typed return.
- Implement finite-number and duration arithmetic rules, division and modulo faults, stack faults, and source-span diagnostics.
- Apply fatal predicate-fault and task-expression-fault behavior without partial event consumption or leaked output ownership.

## R3: physical state and dispatcher

- Convert normalized physical keyboard down into `Down` or `Repeat` after updating stable physical state, and handle mouse button down and up.
- After physical-state update, force-stop recognition, and applicable target and pointer routing checks, binary-search the dedicated pause-control buckets before consulting the current `PAUSE` value.
- Evaluate pause-control predicates against the event snapshot in source order; the first match synchronously applies `On`, `Off`, or `Toggle`, returns its consume or observe decision, and creates no ordinary event transaction, mapping, or task.
- When no pause-control rule matches, forward immediately while `PAUSE` is off and enter ordinary mapping and rule dispatch only while `PAUSE` is on.
- Binary-search event buckets, evaluate every rule for one physical event against one stable logical-state snapshot, preserve source order, combine delivery and flow, and collect actions or mappings in fixed transaction scratch.
- Process the active mapping lifecycle before ordinary event rules so a source repeat or release cannot be intercepted by a later stop rule, while still allowing ordinary rules to observe the event afterward.
- Reserve the complete event transaction before committing mapping activation or returning suppression.
- Forward the triggering event and request controlled shutdown on predicate or internal invariant failure. A transient transaction-capacity rejection forwards the event, commits no task or mapping state, publishes a bounded visible error, and leaves the runtime active.

## R4: action VM and cooperative scheduler

- Execute action programs by program ID and local position without storing instruction pointers.
- Give every task independent instruction position, repeat frames, wait state, wake deadline, cancellation generation, and task-owned output records while sharing immutable action code.
- Implement press, release, timed tap, cancellable wait, action gap, set, toggle, exec, forward branches, one-time repeat-limit initialization, per-iteration while conditions, cooperative back edges, yield, and end.
- Use one cooperative task thread with ready and timed queues, a fake monotonic clock in tests, and cancellable waits in production; no macro receives a dedicated operating-system thread.
- Execute adjacent non-waiting instructions in one task slice until the task reaches a gap, wait, tap hold, yielded back edge, end, fault, or cancellation; timed tasks leave the ready queue without blocking the task thread.
- Run process launch on the task thread through an injected platform launcher port, resolve the executable before launch, use the resolved executable's containing directory as the child working directory, and never launch on the hook path.
- Preserve the authored command for the native process API, introduce no implicit shell or wait, release temporary process handles after a successful launch, and end only the current task on a launch failure.
- Release all remaining task ownership on normal end, cancellation, task fault, target invalidation, and shutdown.

## R5: complete mappings and output ownership

- Store one optional active mapping ID per source mapping slot.
- Process active repeat and release lifecycle behavior before ordinary event rules and clear a mapping only after a committed release.
- Merge task and mapping ownership so each output control emits a down only at the global zero-to-one transition and an up only at the one-to-zero transition.
- Treat ownership state as the cleanup source of truth; cancellation may discard ordinary unsent output only after stale producers are invalidated, and it must preserve or regenerate releases required for controls whose down transition was sent.
- Treat unowned task release, ownership counter overflow, partial publication, and cleanup failure according to the frozen safety policy.
- Convert committed output primitives to the existing bounded `ActionBatch` path without allowing self-injected output to re-enter user rules.

## R6: cancellation, reload, and diagnostics

- Implement cancellation generations for `PAUSE`, reload, target loss, fatal failure, force stop, and shutdown.
- On an invalidating transition, advance the monotonic generation before accepting another transaction, reject stale publications, wake the scheduler, discard old-generation ready and timed tasks, clear mappings, and release ownership in deterministic order.
- Ensure an off-on pause cycle cannot revive a task from the earlier generation. A real pause-control value change performs the invalidating transition synchronously; an idempotent `On` or `Off` still returns its delivery decision without advancing the generation.
- Publish bounded immutable snapshots containing program identity, source spans, rule decisions, task positions, deadlines, cancellation reasons, evaluation faults, and ownership changes.
- Keep console, file, process, waiting, allocation, and injection work outside the hook callback.

## R7: Windows adapters and Phase 1 boundary

- Extend Windows normalization to produce runtime event transitions while retaining origin classification and self-tag isolation.
- Implement the shared backend control catalog and capability lookup for event sources, physical-state reads, output down/up, output repeat, logical or physical identity, scan-code recipes, extended keys, exceptional sequences, and layout behavior.
- Connect target and pointer routing checks at dispatch and injection time.
- Resolve the configured target to retained process identity rather than a reusable PID alone, and invalidate execution when that identity exits or no longer satisfies foreground or pointer routing policy.
- Preserve the explicit limitation that Windows `SendInput` is system-wide rather than PID-bound; target and pointer revalidation reduce but cannot eliminate the routing race.
- Connect committed runtime outputs to the existing fail-open fixed-capacity queue, injection cleanup, queue-full visibility, and controlled shutdown path.
- Keep the Phase 1 fixed-rule mode available as a regression oracle; application mode selection belongs to a future integration phase.

## Test matrix

- Activation tests cover every requirement at acceptance and rejection boundaries and prove transactional publication.
- Expression tests cover every opcode, operator signature, stack depth, branch merge, short circuit, physical read, user value, built-in value, finite-number rule, and duration fault.
- Dispatcher tests cover all ordinary arrows, pause-control `On`, `Off`, and `Toggle`, pause delivery, first-match source order, recovery while paused, injected-input bypass, idempotent effects, rule overlap, stable snapshots, empty action rules, mapping precedence, atomic reservation, and fail-open failure.
- Scheduler tests cover every action opcode, fake time, tap phases, gap timing, task ordering, nested repeat frames, cooperative loops, process launching, and faults.
- Ownership tests cover overlapping tasks and mappings, repeated acquisition, unowned release, cancellation during tap, target loss, partial injection, normal end, and shutdown.
- Fixture tests execute the tap, mapping, conditional repeat, and pause-control programs from Phase 2 without invoking compiler code.
- Windows regression tests cover normalization, self-tag loopback, target routing, queue full, partial injection, cleanup, and force stop.
- Windows control tests cover every catalog entry's declared input, physical-state, down/up, and repeat capabilities plus normal, E0, E1, layout-sensitive, and mouse output recipes.

## Required semantic preservation

- One physical event updates physical state before predicate evaluation, and every rule scanned for that event sees one stable logical-state snapshot unaffected by tasks selected by the same event.
- Physical pause-control dispatch runs before the ordinary pause guard, never accepts injected input, applies only one synchronous effect, and creates no action task or mapping transaction.
- Event transaction publication is all-or-nothing: selected mappings, tasks, consumption, and output state are committed together or the event is forwarded with no partial state.
- Empty action rules apply delivery and flow without creating tasks, and active complete mappings retain their latched target through source repeat and release.
- Ready and timed tasks execute on one cooperative task thread; explicit gaps, waits, tap duration, and yielded loop back edges are the only scheduling boundaries defined by their action semantics.
- Cancellation generations invalidate executing, ready, timed, and racing work across pause, reload, target loss, fatal failure, force stop, and shutdown.
- Output ownership, rather than queue contents, determines required release behavior on every task exit and global cleanup path.
- Self-injected and third-party injected input bypasses physical state and user rules, while the physical force-stop recognizer runs before target, pause, and rule dispatch guards.
- Target identity and pointer routing are checked before dispatch and immediately before injection, without claiming that system input injection is process-bound.
- Hook-path work remains bounded and performs no allocation, wait, process creation, console or file access, or input injection.
- Static capacity insufficiency rejects activation; transient precommit capacity exhaustion forwards the event, publishes no partial state, reports through the bounded visible-error path, and keeps the runtime active.

## Safety gate

- Hook-path execution performs no allocation, waiting, file access, console access, process creation, or input injection.
- No event is consumed before its complete mapping and task transaction has capacity.
- Any capacity failure is fail-open and uses the bounded visible-error path outside the hook callback.
- Self-injected and third-party injected events never enter user rules.
- Every task exit and cancellation reason releases remaining ownership, and no shutdown path leaves an active task, timer, mapping, or output.
- Every executable backward action edge yields to the scheduler.
- Activation installs no hooks until structural, capability, capacity, target, and policy checks all succeed.

## Completion gate

- Every frozen Phase 2 fixture executes deterministically with fake time and fake output.
- Every expression and action opcode has valid, boundary, and fault coverage.
- Rule dispatch, complete mappings, transaction reservation, scheduling, cancellation, and ownership satisfy the documented semantics without compiler code.
- Fixed capacities pass acceptance, rejection, and stress boundaries while preserving fail-open behavior.
- Strict warnings, runtime tests, runtime `-fanalyzer`, dependency audit, Windows regression checks, and `git diff --check` pass.
- Verification evidence is recorded under `development/phase-3-runtime/` without requiring a compiler milestone.

## Open design gates

The runtime may implement work that does not depend on an active issue, but it may not declare completion while an issue in `development/OpenDesignIssues.md` affects stable event snapshots, the Weave v1 backend control identity or output recipes, or executable resolution. Each resolved decision must be reflected in the language specification when user-visible, in the Phase 2 contract when representational, and in runtime tests before implementation is accepted. `docs/language/grammar.v2.md` does not change this branch's frozen v1 contract.

## Handoff artifact

The completed artifact is a runtime activation and execution API that consumes `std::shared_ptr<const CompiledProgram>` plus platform ports. A future integration phase may connect a completed compiler, application mode, and TUI; this phase does not perform or wait for that integration.

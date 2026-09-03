# Phase 14: Unified Executor Session

## Objective

Phase 14 makes one `WindowsProgramRuntimeSession` represent the complete live lifetime of an activated `InputWeaver.exe` program. Debug transport, program state, input hooks, output processing, task scheduling, and the replaceable executable-target binding all belong to that lifetime. Target discovery no longer gates session creation and target loss no longer changes the identity of the session.

The user-visible invariant is: once an executor reports that its program session is active, the same `ProgramRuntime`, debug endpoint, hooks, variables, arrays, and `PAUSE` state remain alive until an explicit or fatal executor stop.

## Ownership Boundary

`RunWindowsExecutor` remains the process bootstrap and policy boundary. It loads and validates the artifact, resolves command-line target overrides, starts diagnostics, acquires the Host or console stop event, constructs one `WindowsProgramRuntimeSession`, coordinates executable-target discovery, waits for stop conditions, prints final metrics, and releases process-level resources.

`WindowsProgramRuntimeSession` becomes the sole live runtime owner. It owns the optional `WindowsDebugServer`, the stable executable-target binding slot, `ProgramRuntime`, the Windows runtime ports, the task and output threads, the low-level keyboard and mouse hooks, the foreground notification hook, session synchronization events, and the session metrics snapshot.

`TargetProcessContext` is a replaceable binding inside the session rather than a prerequisite supplied to the session constructor. Executable mode uses one stable storage location whose contents move through unbound and bound states. Global mode does not expose an executable-target binding to the routing or hook components.

`WindowsDebugClient` remains owned by the Host-side application adapter. It connects to the executor session by executor PID and debug token and does not participate in target discovery.

## Session Invariants

- One executor process activates at most one live `WindowsProgramRuntimeSession`.
- Session startup never waits for an executable target.
- Debug pipe creation never waits for an executable target.
- Program activation never requires a currently bound executable target.
- The low-level input hooks are installed once per session and remain installed while the session waits for the first target or a replacement target.
- Target absence sets executable routing ineligible but does not stop the session.
- Target replacement changes only the target binding and target eligibility.
- User variables, arrays, random state, physical input state, `PAUSE`, debug capture, and the program serial remain owned by the same `ProgramRuntime` across target replacement.
- Physical exit rules are evaluated before target eligibility and therefore remain available while the target is absent or not foreground.
- `PAUSE` rules and ordinary mappings remain behind target eligibility and routing checks.
- Host stop, console stop, debug protocol stop, physical exit, hook failure, output failure, and fatal runtime failure converge on the session's idempotent stop path.
- Debug transport starts before runtime publication can occur and stops only after runtime publication has ended.

## Lifecycle

### Process Bootstrap

The executor reads and structurally validates the `.weavec` artifact, resolves the effective target kind and selector, starts the diagnostic log, and validates the inherited or locally created stop event before constructing the session. A failure in this stage occurs before a live session exists.

### Session Start

`WindowsProgramRuntimeSession::Start` performs the following ordered work without target discovery:

1. Create the session shutdown, target-loss, producer-completion, and output synchronization events.
2. In debug mode, construct and start `WindowsDebugServer` with the compiled program, a callback that wakes the session input thread, and a callback that requests session stop.
3. Create the control catalog, stable target binding view, route port, process launcher, runtime clock, output port, `ProgramRuntime`, input adapter, and low-level hook owner.
4. Activate `ProgramRuntime` with the effective target kind.
5. Initialize executable target eligibility from live binding validity; an initially unbound executable target starts ineligible, while a global target starts eligible.
6. Start the output thread and runtime task thread.
7. Install the keyboard, mouse, and required foreground hooks and wait for the hook thread to report readiness.
8. Let the hook thread seed physical state and consume any debug capture command that arrived during startup.
9. Return success only after the program is active and the hook thread is ready.

The debug server may complete its pipe handshake before the hook thread is ready. A `StartCapture` command received in that interval remains a pending control request. Capture becomes trusted only after the hook thread consumes the request and `BeginCapture` publishes the complete initial input and runtime-state snapshot.

### Target Coordination

After session startup, `RunWindowsExecutor` enters the target coordinator. Global mode proceeds directly to the session wait. Executable mode repeatedly performs the following cycle:

1. If the session target binding is unbound, locate and validate a matching process while also waiting for the external stop event and the session stopped event.
2. Move the validated candidate into `WindowsProgramRuntimeSession::AttachTarget`.
3. Recompute target eligibility from exclusion and foreground state and wake the hook thread.
4. Wait for session stop, external stop, or target loss.
5. On target loss, return to discovery without recreating any session-owned component.

Target discovery remains on the executor coordinator thread. It does not block the hook, task, output, debug server, or debug writer threads and does not require a new worker thread.

### Target Loss and Replacement

The hook thread observes the bound process handle. When the handle signals, it serializes target loss with input handling, clears the binding under the target mutex, tells `ProgramRuntime` to cancel target-scoped work, advances the runtime generation, releases owned outputs, sets target eligibility false, and signals the coordinator's target-loss event.

`AttachTarget` accepts only a validated candidate while the session is running and currently unbound. It moves the candidate into the stable binding slot under the target mutex, computes current foreground eligibility, and wakes the hook thread. It does not activate or reload the program and does not start a new debug capture generation.

### Session Stop

`RequestStop` is idempotent and safe from the executor coordinator, debug server, hook thread, and fatal runtime callback. The first request marks shutdown, signals the session shutdown and output wake events, wakes the hook thread, and requests `ProgramRuntime` shutdown. Later requests only preserve the signaled state.

`Wait` completes shutdown in this order:

1. Stop accepting new runtime work and advance the cancellation generation.
2. Cancel tasks and mappings and publish required release outputs.
3. Let the hook thread complete its bounded release grace period, then uninstall foreground, mouse, and keyboard hooks.
4. Stop the runtime task thread and pump remaining cancellation cleanup.
5. Deactivate `ProgramRuntime` after owned outputs have been released.
6. End debug capture after runtime publication has ended.
7. Drain and join the output thread.
8. Snapshot metrics.
9. Stop the debug server and close the pipe after runtime and hook producers can no longer publish.
10. Clear the target binding and close session events.

The inherited Host stop event and the locally created console stop event are process-level stop inputs. The executor coordinator translates either signal into `WindowsProgramRuntimeSession::RequestStop`. TUI `[X]` therefore remains independent of target presence, debug capture state, and hook availability.

## Target Contract Split

The route contract separates activation topology from live process state.

`RuntimeRoutePort::ValidateTarget` validates that the selected target kind is supported by the assembled route. For Windows global mode this requires no executable binding view; for Windows executable mode this requires a stable executable binding view but does not require that view to be bound.

`RuntimeRoutePort::TargetValid` reports whether the live route currently has a usable target. Global mode is always live; executable mode requires a bound and alive process.

`RuntimeRoutePort::CanDispatch` and `CanInject` apply live foreground, exclusion, pointer-target, liveness, and control-capability checks. These checks remain runtime gates and are not activation prerequisites.

`ProgramRuntime::Activate` initializes `targetEligible` from `TargetValid` after `ValidateTarget` succeeds and before the state becomes active. This avoids a transient eligible window for an initially unbound executable target without adding a startup cancellation generation. Test route implementations retain their existing behavior when they report a valid live target.

## Input and Debug Flow

The session installs one keyboard hook and one mouse hook. Debug observation, exit rules, `PAUSE` rules, mappings, and ordinary rules are stages of the same input path rather than separate listeners.

For each physical candidate, the hook path normalizes the native input, begins debug correlation when capture is active, updates physical state, evaluates exit rules, applies target eligibility and route checks, evaluates `PAUSE` rules, applies `PAUSE`, dispatches ordinary mappings and rules, publishes the final debug input disposition, drains diagnostics, and requests stop when exit or fatal state is observed.

While an executable target is unbound, the hook continues to update physical state, evaluate exit rules, and publish debug inputs. The target-eligibility gate forwards the event before `PAUSE` or ordinary dispatch. Rebinding therefore resumes from the current physical state without reconstructing the hook or debug connection.

The debug control path is `WindowsDebugClient -> named pipe -> WindowsDebugServer -> pending capture request -> hook-thread wake`. The debug data path is `hook or ProgramRuntime -> WindowsDebugServer bounded queue -> writer thread -> named pipe -> WindowsDebugClient`. Neither path references the target locator or target lifetime.

## Failure Rules

- Debug server creation failure aborts session startup before runtime and hooks become active.
- Runtime activation failure closes the already-created debug endpoint during session cleanup and reports the activation error through executor output.
- Hook startup failure requests session stop, completes constructed-component cleanup, and reports the Win32 startup error.
- A target search API failure is fatal to executable target coordination and requests session stop.
- A candidate that exits or changes identity during validation is discarded and discovery continues.
- Debug client disconnection ends the current capture and leaves the executor session alive so the server can accept a same-session reconnection.
- Target loss is a normal unbound transition and does not produce executor failure.
- Output circuit-breaker and fatal runtime conditions request the same session stop path used by explicit stop controls.

## Production Change Set

### `src/platform/windows/runtime/windows_executor.cpp`

- Remove `RuntimeDebugBinding` and executor-owned `WindowsDebugServer` lifetime assembly.
- Create the runtime session before the first executable-target lookup.
- Replace the separate initial-target and replacement-target paths with one unbound/bound coordinator loop.
- Always pass the session stopped event into target discovery so physical exit, debug stop, and fatal runtime stop can end initial discovery.
- Preserve process-level stop-event ownership, diagnostic startup, final metric output, and target selection behavior.

### `src/platform/windows/runtime/program_runtime_session.hpp`

- Replace the borrowed debug server option with the debug session token.
- Remove the borrowed `TargetProcessContext*` constructor parameter.
- Keep `AttachTarget`, stop, wait, stopped-event, target-loss-event, and metrics operations as the session control surface.

### `src/platform/windows/runtime/program_runtime_session.cpp`

- Own the optional debug server and stable executable-target storage.
- Start the debug server before constructing and activating runtime components.
- Route debug wake and stop callbacks directly to the session implementation.
- Supply the stable target view to the route port and hooks only in executable mode.
- Stop the debug server after hook and runtime publication has ended.

### `src/platform/windows/runtime/runtime_route_adapter.cpp`

- Make executable activation validation require the stable binding view rather than a currently valid process handle.
- Retain live liveness checks in `TargetValid`, `CanDispatch`, and `CanInject`.

### `src/runtime/program_runtime_activation.cpp`

- Initialize the candidate runtime state's target eligibility from `RuntimeRoutePort::TargetValid` before publishing the candidate as active.

### Current Product Documentation

- Update the runtime lifecycle description so initial target absence and later target loss are documented as the same unbound state.
- Keep Debug, target replacement, input ordering, and stop controls described as current observable behavior after implementation is complete.

## Design Review

### Ownership Review

The design removes the current lifetime inversion in which `TargetProcessContext` is created outside the runtime session and must already be valid before the session can start. All pointers retained by the route and hook components refer either to session-owned storage or to session-owned services with a longer lifetime than their consumers.

The debug server is constructed before `ProgramRuntime` because it is the runtime debug event port. It is destroyed after `ProgramRuntime`, hooks, and producer threads stop, so no producer can publish through a destroyed port.

### Concurrency Review

Target mutation remains protected by the existing target mutex. Target loss and input callbacks execute on the hook thread, while attachment occurs on the coordinator thread and output validation occurs on the output thread. The stable target storage prevents pointer replacement races; only its contents change under the mutex.

Debug commands received before hook readiness remain represented by the existing atomic pending request. The wake callback may run before the hook owner exists and may safely do nothing because hook startup explicitly processes pending control requests before reporting readiness.

Session stop remains monotonic through the existing atomic shutdown flag and manual-reset shutdown event. Target discovery observes both the session stopped event and the process-level stop event, so no target state can strand shutdown.

### Behavioral Review

The design preserves global routing, target selection, ambiguity handling, exclusion precedence, dry-run behavior, debug framing, target replacement state retention, output cleanup, and final metrics. It changes only the initial executable-target state from "session not created" to "session active and target unbound."

The physical exit path requires no new global hotkey or second hook because exit rules already precede target eligibility in `ProgramRuntime`. Starting the existing hook before discovery makes the existing rule semantics available during the unbound state.

The Host `[X]` path remains independent through the inherited stop event. Debug `[C]` remains a capture command and never changes executor session lifetime.

### Complexity Review

The design reuses the existing session, target context, locator, route port, debug server, hook thread, stop events, and attachment operation. It removes duplicated initial and replacement target paths and removes the temporary debug-to-runtime binding object. It introduces no additional worker thread, polling algorithm, protocol message, third-party dependency, or second input hook.

## Completion Criteria

- An executable-target session creates and activates `ProgramRuntime` before target discovery begins.
- Debug mode creates its named pipe and completes Host connection without waiting for the target.
- The hook thread is ready while the target is absent.
- The configured physical exit rule stops the executor while the target is absent.
- TUI `[X]` stops the executor from Programs and Debug pages in every target state.
- Debug `[C]` starts and stops capture while the target is absent without stopping the executor.
- Debug capture becomes trusted without a target and remains connected across target attachment, loss, and replacement.
- Target attachment enables routing only when foreground and not excluded.
- Target loss disables routing, cancels target-scoped work, releases owned outputs, and returns to discovery without recreating session components.
- Target replacement preserves variables, arrays, `PAUSE`, random state, the debug connection, and the active program runtime.
- Global-target behavior remains unchanged.
- Session shutdown leaves no active hook, runtime task, output thread, debug writer, pipe handle, target handle, or owned output.

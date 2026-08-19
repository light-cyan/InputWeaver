# Phase 1 Code Design

## Design Goal

The code must make input provenance an explicit boundary: accepted physical-candidate input may reach the diagnostic rule engine, while the program's own `SendInput` output remains visible to the target application but cannot re-enter the rule engine.

## Planned Architecture

```text
Windows input stream
        |
        v
Dedicated hook thread
        |
normalize and classify origin
        |
        +--> SelfInjected or ExternalInjected ----------------------> forward
        |
        +--> PhysicalCandidate
                  |
                  v
        physical state and emergency edge
                  |
                  +--> captured pair -------------------------------> suppress
                  |
                  v
        mode, first-down, fixed-source, and output-state checks
                  |
                  +--> no accepted rule ----------------------------> forward
                  |
                  v
        target PID and probe-class guard
                  |
                  +--> target mismatch -----------------------------> forward
                  |
                  v
        bounded action queue --> action worker --> SendInput with self tag
                                                     |
                                                     v
                                             Windows input stream
                                               |             |
                                  hook sees SelfInjected     +--> target after forwarding

hook diagnostic ring -------------------+
                                        +--> diagnostic worker
injection diagnostic ring --------------+
```

The separate `InputProbe.exe` process receives ordinary window messages and registered Raw Input so the externally visible result can be compared with the remapper's internal origin trace.

## Planned Source Layout

- `src/remapper_main.cpp`: process startup, command-line validation, self-tag creation, component lifetime, and exit status.
- `src/input_event.hpp`: normalized event, origin, device, transition, and decision types.
- `src/input_classifier.hpp`: pure origin-classification functions for `KBDLLHOOKSTRUCT` and `MSLLHOOKSTRUCT`.
- `src/hook_thread.hpp` and `src/hook_thread.cpp`: hook installation, message loop, callbacks, target guard, and unhooking.
- `src/process_context.hpp` and `src/process_context.cpp`: process existence, foreground PID, elevation, and integrity-level queries.
- `src/diagnostic_rules.hpp` and `src/diagnostic_rules.cpp`: the fixed F6-to-F7, F7-to-F8, F9-to-middle-button, and middle-button-to-F10 Phase 1 rules and paired capture state.
- `src/action_queue.hpp`: a fixed-capacity non-blocking queue for hook-thread-to-worker action batches.
- `src/input_injector.hpp` and `src/input_injector.cpp`: conversion of action batches to `INPUT` arrays and validated `SendInput` calls.
- `src/diagnostic_log.hpp` and `src/diagnostic_log.cpp`: per-producer fixed diagnostic rings, overflow counters, and asynchronous output.
- `src/input_probe_main.cpp`: Win32 probe window, PID display, Raw Input registration, and received-event logging.
- `src/phase1_tests.cpp`: dependency-free unit and state-machine tests with a simple failing exit code.
- `res/UniversalKeyRemapper.manifest`: explicit `asInvoker` and `uiAccess=false` process execution settings.
- `res/UniversalKeyRemapper.rc`: a Win32 resource script that embeds the execution manifest.

The exact split may be reduced during implementation when two files have no meaningful independent responsibility, but hook callbacks, rule logic, injection, and probe code must not be collapsed into one source file.

## Core Types

```cpp
enum class DeviceKind : unsigned char {
    Keyboard,
    Mouse
};

enum class InputOrigin : unsigned char {
    PhysicalCandidate,
    SelfInjected,
    ExternalInjected
};

enum class Transition : unsigned char {
    Down,
    Up,
    Move,
    VerticalWheel,
    HorizontalWheel
};

struct InputEvent {
    DeviceKind device;
    InputOrigin origin;
    Transition transition;
    DWORD code;
    DWORD scanCode;
    DWORD flags;
    DWORD mouseData;
    POINT position;
    DWORD timestamp;
    ULONG_PTR extraInfo;
};

struct Action {
    DeviceKind device;
    Transition transition;
    DWORD code;
    LONG valueX;
    LONG valueY;
};

struct ActionBatch {
    unsigned long long sourceSequence;
    unsigned long long outputStateGeneration;
    DWORD targetPid;
    DeviceKind outputDevice;
    DWORD outputCode;
    std::array<Action, 8> actions;
    std::size_t actionCount;
};
```

Phase 1 uses fixed-capacity batches so the hook callback does not allocate memory. Values not used by a particular device or transition are zero-initialized.

## Fixed Phase 1 Limits

- Maximum actions per batch: 8.
- Action queue capacity: 256 batches.
- Hook diagnostic ring capacity: 4096 records.
- Injection diagnostic ring capacity: 512 records.
- Probe display capacity: 2048 records.
- Optional JSONL maximum: 8 MiB; file output stops at the limit and reports truncation instead of growing or rotating.
- Graceful captured-release wait: 2000 milliseconds.
- Injection circuit breaker: disable new captures after 3 consecutive short or failed `SendInput` calls.
- Self-test countdown: 3 seconds using a non-blocking message timer.

These constants are Phase 1 acceptance values and may change only with corresponding updates to the plan, tests, and verification record.

## Self Tag

One nonzero `ULONG_PTR` tag is generated at process startup from process-local runtime values such as `QueryPerformanceCounter` and `GetCurrentProcessId`, mixed without storing a real pointer. The tag is an accidental-collision guard, not an authentication token.

Every keyboard and mouse `INPUT` produced by the injector receives the same current tag in its `dwExtraInfo` field, including key releases, mouse-button releases, shutdown cleanup, and self-test events.

## Origin Classification

```cpp
InputOrigin ClassifyKeyboard(const KBDLLHOOKSTRUCT& event, ULONG_PTR selfTag) noexcept {
    if ((event.flags & LLKHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }
    return event.dwExtraInfo == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}

InputOrigin ClassifyMouse(const MSLLHOOKSTRUCT& event, ULONG_PTR selfTag) noexcept {
    if ((event.flags & LLMHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }
    return event.dwExtraInfo == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}
```

The lower-integrity injected flags are recorded for diagnostics but do not identify the current process and do not affect self-tag matching.

## Hook Callback Contract

```text
if nCode is not HC_ACTION:
    call and return CallNextHookEx

start the local processing timer
initialize a local fixed-size diagnostic record
normalize the hook structure
classify the event origin

if origin is SelfInjected or ExternalInjected:
    return ForwardWithDiagnostic(record)

update physical edge and emergency-chord state

if this first-down event activates the physical emergency chord:
    disable new diagnostic matches
    request orderly shutdown
    return ForwardWithDiagnostic(record)

if the event belongs to an already captured physical source pair:
    update capture state
    return SuppressWithDiagnostic(record)

if diagnostic mode is inactive:
    return ForwardWithDiagnostic(record)

if the event is not a first-down edge:
    return ForwardWithDiagnostic(record)

look up the fixed diagnostic rule for this source control
if no rule matches:
    return ForwardWithDiagnostic(record)

if the intended output control conflicts with physical state:
    return ForwardWithDiagnostic(record)

if the target PID and probe class are not foreground:
    return ForwardWithDiagnostic(record)

construct a fixed action batch
if the action queue rejects the batch:
    return ForwardWithDiagnostic(record)

record the captured source pair
signal the action worker
return SuppressWithDiagnostic(record)
```

The callback decides suppression synchronously because Windows requires the return value immediately, but output timing, `SendInput`, and diagnostic formatting occur outside the callback. `ForwardWithDiagnostic` and `SuppressWithDiagnostic` finalize the rule, queue, decision, and local processing-duration fields before attempting one non-blocking diagnostic-ring insertion; the forward helper then calls `CallNextHookEx`, while the suppress helper returns a nonzero value.

## Diagnostic Rule State

The diagnostic engine maintains physical-candidate down state separately from observable operating-system state. Self-injected and external-injected events do not modify this state under the strict Phase 1 policy.

Each consumed physical source control is recorded until its matching release arrives. Repeated down messages for a captured control are consumed without scheduling another tap. The matching release is processed before the target guard and is consumed even if targeting or diagnostic mode changes after the initial press, preventing an unmatched release from reaching the target.

Each fixed Phase 1 action is a keyboard or mouse-button tap batch containing down followed by up. This avoids holding synthetic state across asynchronous boundaries while the ownership mechanism is first validated.

Physical F6 maps to an F7 tap and physical F7 maps to an F8 tap. Physical F9 maps to a middle-button click and a physical middle-button press maps to an F10 tap. The first rule in each pair proves that its self-tagged output does not activate the second rule, while a separate physical use of the second source proves that the second rule is active.

A diagnostic rule is eligible only when its intended output control is not already physically held. If the output conflicts with current physical-candidate state, the source is forwarded and no capture or action is created, reducing the risk that a synthetic tap release disturbs a real held key or mouse button.

For each diagnostic output control, the hook thread publishes one packed atomic value containing its physical down bit and a generation incremented when that control's physical-candidate state changes. Each batch identifies its output control and stores the observed generation. Before injection, the worker loads that control's current packed value once and cancels the batch if the generation changed or the down bit is set.

The worker recheck reduces but cannot eliminate the check-to-`SendInput` race because Windows provides no atomic operation that combines physical-state validation with injection. Phase 1 therefore limits these mappings to the probe and requires manual checks not to operate a mapped output control concurrently.

The first physical F12 down edge that completes Ctrl+Shift+F12 is evaluated before the target guard, disables new diagnostic matches, requests orderly shutdown, and is forwarded rather than captured. Other events continue through captured-pair handling even while the chord remains held, and injected events cannot activate the chord.

## Target Guard

Diagnostic mode requires `--target-pid <pid>`. Startup requires that PID to own a top-level window whose class is `UniversalKeyRemapper.Phase1InputProbe`. Only after a physical first-down edge matches one of the four fixed sources and passes its output-state check does the hook path call `GetForegroundWindow`, `GetWindowThreadProcessId`, and `GetClassNameW`; both the configured PID and probe class must match.

Before hooks become active, process-context validation opens and retains a handle to the target using only query and synchronization rights, confirms that the expected probe window exists, and compares token integrity levels. If the probe window is absent, the target handle cannot be opened, the target integrity level cannot be queried, or the target level is higher than the remapper's level, diagnostic suppression does not arm and startup reports the validation failure. This avoids knowingly swallowing a source event when UIPI would block its replacement output.

The retained handle must remain unsignaled during hook and worker target checks; this binds the run to the original probe process even if its numeric PID is later reused. The fixed window-class check protects against accidental targeting during Phase 1 but is not process authentication because another application could deliberately reuse the same class name.

The PID guard is not evaluated for already captured releases because the target must not receive a release for a source press that it never received.

Each accepted action batch stores the target PID. The action worker rechecks the foreground PID and probe window class immediately before injection and cancels the batch if either changed, preferring a dropped diagnostic action over input sent to the wrong application.

`SendInput` writes to the system input stream rather than to a PID, so foreground validation and injection cannot be one atomic operation. A focus switch can still occur after the worker's check; Phase 1 treats this as a documented limitation and permits diagnostic routing and self-test output only toward the dedicated probe.

The explicit startup self-test follows the same target and integrity rules as diagnostic mappings. After both hooks are ready, the hook thread schedules a non-blocking message timer for a visible three-second countdown only for `--self-test`, creates target-bound test batches when the timer expires, and the action worker cancels them unless the foreground window still matches the configured probe PID and fixed class. The message loop never sleeps for the countdown.

## Action Queue and Injection

The hook thread is the only producer of Phase 1 diagnostic action batches and the action worker is the only consumer. Startup self-test requests are posted to the hook thread as control messages, preserving a fixed-capacity single-producer/single-consumer ring buffer and keeping the callback non-blocking.

After the target and physical-state rechecks pass, the worker converts each batch to a zero-initialized fixed `INPUT` array, sets scan-code and extended-key flags where required, writes the self tag to every item, and calls `SendInput` once per batch.

The worker records requested and returned input counts plus `GetLastError` immediately after a short or failed send. A partial send is treated as an error and schedules best-effort cleanup for every synthetic down action that might have been inserted without its matching up action.

## Diagnostics

Hook diagnostics use fixed-size records. Recommended fields are sequence number, performance-counter timestamp, local hook-processing duration, device, transition, redacted control category, raw flags, lower-integrity flag, extra-information category, classified origin, foreground PID, rule ID, queue result, suppression result, and overflow counters.

Exact virtual-key, scan-code, and mouse-button values are retained only for F6, F7, F8, F9, F10, F12, Ctrl, Shift, the middle button, and program-generated self-test controls. Every other keyboard event is serialized as `OtherKeyboard` with its virtual-key and scan-code fields cleared, ordinary mouse movement is aggregated without absolute coordinates, and `dwExtraInfo` is reduced to `Zero`, `SelfTag`, or `OtherNonzero` rather than persisted as a raw value. Automated tests must verify that letter-key codes cannot appear in formatted diagnostics.

Each hook callback builds its record locally and attempts a non-blocking append only from its finalize-return helper, after the rule, queue, suppression, and local processing-duration fields are known. The action worker writes injection results to a separate bounded injection-diagnostic ring. Each ring has one producer and the diagnostic worker drains both; if either ring is full, its producer increments a separate dropped-record counter and continues.

Formatting and output occur on a diagnostic worker. Default output may use `OutputDebugString` and an optional bounded JSONL file opened before hooks are installed. JSONL output uses the same redaction before persistence and never retains arbitrary key-code sequences or unbounded input history.

## Thread and Lifetime Model

- Main thread: parse options, create shared state, start workers, request startup self-test through the hook thread, handle shutdown, and join threads.
- Hook thread: create its message queue, install both low-level hooks, publish readiness, pump messages, execute callbacks, and unhook before exit.
- Action worker: wait for queued batches, call `SendInput`, record results, and perform owned-state cleanup.
- Diagnostic worker: drain fixed diagnostic records and format output independently of action delivery.

Startup succeeds only after both hooks are installed and both workers report readiness. If either hook fails, the program reports the Win32 error, removes any installed hook, stops workers, and exits without entering a partial active state.

Shutdown first disables new rule matches and cancels new actions. During a bounded grace period, the hooks continue swallowing releases for already captured physical sources; after all captures resolve or the deadline expires, the action worker releases owned synthetic state, the main thread posts `WM_QUIT`, the hook thread unhooks, final diagnostics drain, and all threads join.

## Failure Policy

- Hook installation failure: inactive process with a nonzero exit code.
- Invalid target PID or command line: no hooks installed and a nonzero exit code.
- Unknown or insufficient target integrity level: diagnostic suppression remains inactive and startup returns a nonzero exit code.
- Target process exit: its retained handle becomes signaled, new captures are disabled, queued target-bound actions are cancelled, and orderly shutdown begins.
- Action queue full before capture: forward the source event and record overflow.
- Either diagnostic ring full: drop only that diagnostic record and increment its ring-specific counter.
- `SendInput` short or failed result: record the exact count and error, perform possible output cleanup, and trip the diagnostic action circuit breaker after a bounded threshold.
- Hook callback exception boundary: no exception may escape; the event is forwarded unless it belongs to a previously captured pair.
- Worker failure: disable new captures, release owned outputs, and initiate process shutdown.
- Low-level hook timeout or silent removal: input remains fail-open at the operating-system boundary; Phase 1 keeps callbacks bounded and records local processing duration but cannot reliably query or prove that an installed hook remains present after every event.

## Probe Design

`InputProbe.exe` registers the fixed `UniversalKeyRemapper.Phase1InputProbe` window class, creates a simple foreground Win32 window, displays its PID, registers keyboard and mouse devices with `RegisterRawInputDevices` in the default foreground mode without `RIDEV_NOLEGACY`, and observes `WM_KEYDOWN`, `WM_KEYUP`, system-key messages, mouse button and wheel messages, movement, `WM_INPUT`, timestamps, and `GetMessageExtraInfo` values.

After processing foreground `WM_INPUT` messages whose `GET_RAWINPUT_CODE_WPARAM(wParam)` value is `RIM_INPUT`, the probe calls `DefWindowProc` so Windows can perform the required cleanup. Traditional messages and Raw Input are labeled and displayed as separate streams because they are not assumed to contain identical events or suppression results.

The probe is an observer and never injects or suppresses input. It applies the same diagnostic-control allowlist and `OtherKeyboard` redaction before display or persistence, and its event display is bounded so high-rate mouse input cannot cause unbounded memory growth.

## Build Design

`script/build.bat` creates `bin/` if necessary, compiles the execution manifest with the MinGW resource compiler, and invokes G++ separately for the remapper, probe, and tests using C++20, warnings, Unicode Win32 definitions, and the required system libraries only. The script exits immediately on a failed compilation.

`script/test.bat` runs `bin/Phase1Tests.exe` and returns its exit code. The Phase 1 completion gate requires both build and test scripts to return zero.

The build must not compile `legacy/MouseHookPrototype.cpp` into any Phase 1 executable.

## Completion Evidence

Implementation results belong in `docs/phase-1/Verification.md`. That file must record the compiler version, exact build command, automated-test output, manual physical-input observations, integrity-level cases, stress duration, maximum observed local hook-processing time, queue and diagnostic overflow counts, known environment limits, and the Git working-tree state at verification time.

## Win32 References

- [SetWindowsHookExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw)
- [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
- [LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)
- [KBDLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct)
- [MSLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct)
- [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [Raw Input Overview](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input)

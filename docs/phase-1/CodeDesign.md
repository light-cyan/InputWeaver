# Phase 1 Code Design

## Design Goal

The implementation makes input provenance an explicit boundary: accepted physical-candidate input may reach the fixed Phase 1 rule engine, while every injected input is forwarded without becoming a new rule source.

## Runtime Flow

```text
CLI target selector
        |
        v
executable locator --> retained target context --> integrity and liveness checks
                                                    |
Windows input stream                                v
        |                                    foreground ownership
        v                                           |
dedicated low-level hook thread                     v
        |
normalize and classify origin
        |
        +--> SelfInjected or ExternalInjected ---------------------> forward
        |
        +--> PhysicalCandidate
                  |
                  +--> captured release or emergency edge
                  |
                  v
        fixed rule, output state, target, and pointer checks
                  |
                  +--> no accepted rule ---------------------------> forward
                  |
                  v
        transactional bounded queue --> action worker --> tagged SendInput
                                                              |
                                                              v
                                                     Windows input stream
```

## Source Modules

- `src/main.cpp`: console option parsing, mode selection, target resolution, runtime startup, status output, and shutdown.
- `src/platform/windows/process_locator.hpp` and `src/platform/windows/process_locator.cpp`: executable basename or absolute-path matching over a Windows process snapshot.
- `src/platform/windows/process_context.hpp` and `src/platform/windows/process_context.cpp`: retained process handle, image identity, integrity comparison, liveness, foreground ownership, and pointer-route ownership.
- `src/core/input_event.hpp`: normalized fixed-size event, action, origin, decision, and target-bound batch types.
- `src/platform/windows/input_classifier.hpp`: pure keyboard and mouse origin classification.
- `src/core/fixed_rules.hpp` and `src/core/fixed_rules.cpp`: fixed Phase 1 mappings, physical state, captured source pairs, repeats, output conflict checks, and emergency shutdown state.
- `src/core/action_queue.hpp`: fixed-capacity single-producer/single-consumer action ring with transactional single-batch and atomic multi-batch publication.
- `src/platform/windows/input_injector.hpp` and `src/platform/windows/input_injector.cpp`: keyboard and mouse `INPUT` preparation, tagging, send-result validation, and partial-send cleanup.
- `src/platform/windows/hook_thread.hpp` and `src/platform/windows/hook_thread.cpp`: hook lifecycle, callback normalization, action worker, final target checks, circuit breaker, owned-output cleanup, and runtime metrics.
- `src/diagnostics/diagnostic_log.hpp` and `src/diagnostics/diagnostic_log.cpp`: bounded operational records, optional redacted input trace, asynchronous formatting, JSONL byte limit, and drop counters.
- `tests/runtime_tests.cpp`: pure tests and Win32 integration tests, including a child process for target lifecycle coverage.

## Fixed Limits

- Maximum actions per batch: 8.
- Action queue capacity: 256 batches.
- Hook diagnostic ring capacity: 4096 records.
- Injection diagnostic ring capacity: 512 records.
- JSONL maximum: 8 MiB; output stops at the limit and reports truncation.
- Captured-release shutdown grace: 2000 milliseconds.
- Injection circuit breaker: 3 consecutive short or failed `SendInput` calls.

These values are Phase 1 acceptance constants and change only with matching tests and verification updates.

## Self Tag and Origin Classification

One nonzero process-specific tag is generated from local runtime values and folded into the portable `std::uint32_t` `SelfTag` type so it round-trips consistently through keyboard and mouse `dwExtraInfo` paths. The injector widens it to `ULONG_PTR`; the classifier first requires the Windows injected flag and then compares the low 32 bits. The value is never a pointer and is an accidental-collision guard rather than an authenticated identity.

Every keyboard and mouse `INPUT` produced by the injector receives the tag, including matching releases and output cleanup.

Keyboard input is `PhysicalCandidate` when `LLKHF_INJECTED` is clear, `SelfInjected` when the injected flag is set and the low 32 bits of `dwExtraInfo` equal the active tag, and `ExternalInjected` otherwise. Mouse input follows the same rule with `LLMHF_INJECTED`.

The lower-integrity injected flags are diagnostic attributes only; they do not identify the current process and do not replace tag matching.

## Hook Callback Contract

```text
if nCode is not HC_ACTION:
    call and return CallNextHookEx

normalize the event and classify its origin

if origin is SelfInjected or ExternalInjected:
    forward without source-state mutation or rule evaluation

update physical state and emergency-chord state

if the first physical edge activates Ctrl+Shift+F12:
    disable new captures, request shutdown, and forward

if the event belongs to an already captured source pair:
    update the pair and suppress

if fixed test rules are inactive or this is not a first-down edge:
    forward

look up the fixed rule and reject any physical output conflict
require the retained target to own the foreground window
require target pointer-route ownership for mouse-bound work

prepare an action batch and transactionally commit capture plus publication
if publication fails:
    forward

signal the action worker and suppress the source
```

Suppression is decided synchronously because Windows requires an immediate hook return value. Injection, JSON formatting, file output, and shutdown cleanup occur outside the callback.

## Rule and Physical State

The rule engine tracks physical-candidate state separately from the observable operating-system stream. Injected events never mutate this state.

After both hooks are installed, the hook thread conservatively seeds Phase 1 source, output, modifier, and middle-button state from `GetAsyncKeyState` so a control held before startup is not treated as a fresh edge.

Each consumed source remains captured through its matching release. Repeated down events for a captured source are consumed without scheduling another action, and the matching release remains paired even after target focus changes.

Every fixed output is a tap batch containing down followed by up. Before publication and again before injection, the implementation rejects an output that is physically held or whose physical-state generation changed.

Physical F6 maps to F7, F7 maps to F8, F9 maps to the middle button, and the physical middle button maps to F10. The first rule in each chain proves that injected output cannot activate the second rule.

## Target Discovery and Guard

The process locator accepts either a bare executable basename or an absolute DOS or UNC path. Matching is ordinal and case-insensitive. Absolute paths receive lexical Windows normalization, and malformed, relative path-like, or embedded-NUL selectors fail.

The locator takes one process snapshot, queries full paths for relevant live candidates, and returns `None`, `One`, `Ambiguous`, or `Error`. Matches are sorted by PID. A unique result is selected directly; if several instances match, the runtime selects one only when exactly one matching instance owns the foreground window, otherwise it lists the candidates and waits.

The selected result is opened with `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`. The context rechecks the selected image path through the retained handle, compares the current and target integrity levels, and retains the process handle so target exit and PID reuse are distinguishable.

New source capture is eligible only while `GetForegroundWindow` belongs to the target process. Each accepted action batch stores the resolved PID, and the action worker repeats foreground and liveness checks immediately before injection.

Mouse-button sources and mouse-output batches also resolve the foreground window thread's capture window or `WindowFromPoint` result and require its root window to belong to the target process.

`SendInput` writes to the system input stream rather than a PID. The repeated foreground guard makes mappings operationally target-scoped, but Windows provides no atomic operation that combines focus validation with injection, so a focus switch can still occur after the final check.

## Action Queue and Injection

The hook thread is the only action producer and the action worker is the only consumer. Transactional publication writes into an unpublished slot, commits source capture only while new captures remain enabled, and then publishes the write sequence.

The worker converts each batch into a zero-initialized fixed `INPUT` array, applies scan-code and extended-key flags, writes the self tag to every item, and calls `SendInput` once per batch.

Every short or failed send records requested and returned counts plus primary and cleanup errors. A partial send immediately attempts tagged releases for synthetic down actions that may have been inserted. Unresolved owned releases are retained in a fixed array, retried outside target routing, and force the circuit breaker open; shutdown performs bounded final retries and returns a nonzero status if state remains unresolved.

## Logging

Logging is enabled by `--log <jsonl-path>`. Its default operational stream contains rule matches, suppression, queue results, action cancellation, injection results, cleanup, failures, and circuit-breaker state without recording routine unrelated input. When no log path is supplied, no logging worker is started.

`--trace-input` requires a JSONL path and adds redacted normalized hook events, including forwarded input. Ordinary mouse movement is aggregated in groups of at most 64 without absolute coordinates.

Exact control values are retained only for F6, F7, F8, F9, F10, F12, Ctrl, Shift, and the middle button. Other keyboard events are stored as `OtherKeyboard` with virtual-key and scan-code fields cleared, and extra information is reduced to `Zero`, `SelfTag`, or `OtherNonzero`.

Hook and injection records use separate fixed-capacity single-producer/single-consumer rings. Producers drop only the new diagnostic record when a ring is full, increment the corresponding counter, and continue without blocking.

A logging worker performs formatting and bounded file output. Hook callbacks never allocate unbounded storage or perform file or console I/O.

## Thread and Lifetime Model

- Main thread: parse console options, resolve and validate the target when fixed rules are selected, start the runtime, report status, receive console stop events, and join workers.
- Hook thread: create the message queue, install both low-level hooks, publish readiness, normalize callbacks, and unhook before exit.
- Action worker: wait for action batches, repeat target and physical-state checks, call `SendInput`, record results, and clean owned output state.
- Logging worker: when logging is enabled, drain fixed records and write bounded JSONL independently of action delivery.

Startup succeeds only after required workers report readiness and both hooks are installed. Partial startup removes any installed hook, stops started workers, and exits with the relevant error.

Shutdown disables new matches while hooks continue pairing releases for already captured sources during a bounded grace period. The action worker stays alive until the hook producer signals completion, cancels late batches, retries owned releases, and exits before final log drain and thread join.

## Failure Policy

- Invalid CLI or target selector: no target-bound hooks are armed and the process returns a nonzero status.
- Missing target: the console waits and retries until the process starts or the user stops the program.
- Ambiguous target: exactly one foreground candidate may be selected; otherwise the console lists candidates and waits without arming rules.
- Target open, path query, liveness, or integrity failure: fixed rules remain inactive and startup reports the error.
- Target exit: new captures are disabled, queued target-bound actions are cancelled, and orderly shutdown begins.
- Foreground or pointer-route mismatch: the new source event is forwarded.
- Action queue rejection before capture commit: the source is forwarded and remains uncaptured.
- Logging ring overflow: only the diagnostic record is dropped and its counter increments.
- `SendInput` failure: possible partial state is cleaned, failure state is recorded, and repeated or unresolved failure opens the circuit breaker.
- Hook callback exception boundary: no exception escapes; the event is forwarded unless a previously captured pair requires suppression.
- Low-level hook timeout or silent removal: operating-system behavior remains fail-open; the implementation bounds callback work but cannot query every silent removal.

## Build Design

`script/build.bat` creates `bin/`, compiles the execution manifest, and builds `UniversalKeyRemapper.exe` and `Phase1Tests.exe` with C++20, Unicode Win32 definitions, strict warnings treated as errors, and Windows system libraries only.

`script/test.bat` runs `bin/Phase1Tests.exe` and returns its exit code. The build excludes `legacy/MouseHookPrototype.cpp`.

## Completion Evidence

`docs/phase-1/Verification.md` records the compiler, commands, outputs, target-selection cases, physical input observations, mouse self-tag result, integrity checks, stress metrics, known platform limits, and repository state used for Phase 1 completion.

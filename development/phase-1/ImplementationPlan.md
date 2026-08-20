# Phase 1 Implementation Plan

## Status

Phase 1 implementation is complete. The authoritative status is recorded in `AGENTS.md`.

## Objective

Build the smallest end-to-end Windows input pipeline that can observe keyboard and mouse events, classify their origin, emit tagged synthetic input, and prove that the remapper never treats its own output as a new mapping source.

## Included Scope

- Install `WH_KEYBOARD_LL` and `WH_MOUSE_LL` hooks on a dedicated message-loop thread.
- Normalize keyboard and mouse hook data into a fixed-size internal event model.
- Classify each event as `PhysicalCandidate`, `SelfInjected`, or `ExternalInjected`.
- Place one process-specific self tag in every keyboard and mouse `INPUT` emitted by the program.
- Forward injected events without updating physical source state or evaluating mapping rules.
- Run as a console application with observer and fixed-rule verification modes.
- Resolve a target by executable basename or absolute executable path and require one unique live match.
- Activate fixed test rules only while the resolved target process owns the foreground window.
- Require the target process to own the pointer route for physical mouse-button sources and synthetic mouse output.
- Retain the resolved target process handle, compare integrity levels, and stop target-bound work after the process exits.
- Provide bounded action queues, bounded logging, controlled shutdown, output cleanup, and fail-open behavior.
- Record operational JSONL events when `--log` is supplied and add redacted normalized input only when `--trace-input` is supplied.
- Provide dependency-free automated tests and canonical batch files for build, test, and run operations.

## Current CLI

Observer mode installs the hooks without suppressing or generating input:

```text
InputWeaver.exe [--log <jsonl-path>] [--trace-input]
```

Fixed-rule verification mode resolves the target process from an executable basename or absolute path:

```text
InputWeaver.exe --test-rules --target <exe-name-or-absolute-path> [--log <jsonl-path>] [--trace-input]
```

`--trace-input` requires `--log`. The `.krm` language is outside Phase 1, so fixed-rule verification mode is the current mapping entry point.

## Target Selection and Scope

- A selector without `\`, `/`, or `:` is matched case-insensitively against executable basenames.
- A path selector must be an absolute DOS or UNC executable path; separators and lexical `.` or `..` components are normalized before a case-insensitive full-path comparison.
- No match keeps the console in a retrying wait state. Multiple matches require exactly one matching foreground instance; otherwise candidates are listed and rules remain unarmed while the console waits.
- An invalid selector or relevant process-query failure prevents fixed rules from arming and produces a nonzero exit.
- A unique target is opened with query and synchronization rights, and its handle remains associated with the original process even if Windows later reuses the numeric PID.
- The hook callback checks that the target owns the foreground window before consuming a new source event.
- The action worker repeats the target and foreground checks immediately before `SendInput`.
- Mouse-bound rules additionally resolve capture or hit-test routing and require the resulting root window to belong to the target process.
- Captured source releases remain paired even if target focus changes after the corresponding source press was consumed.

The low-level hooks observe the interactive desktop because that is how the Windows hook APIs operate, but non-target events are forwarded without mapping. `SendInput` writes to the system input stream, so target checks restrict when an action is allowed rather than addressing a background process directly.

## Fixed Test Rules

- Physical F6 maps to one tagged F7 tap.
- Physical F7 maps to one tagged F8 tap.
- The F7 emitted by the F6 rule must be classified as `SelfInjected` and must not activate the F7 rule.
- Physical F9 maps to one tagged middle-button click.
- A physical middle-button press maps to one tagged F10 tap.
- The middle-button click emitted by the F9 rule must be classified as `SelfInjected` and must not activate the middle-button rule.
- Injected input carrying another tag is classified as `ExternalInjected`, forwarded, and ignored as a rule source.
- Physical Ctrl+Shift+F12 disables new captures and requests orderly shutdown independently of target focus; the chord itself is forwarded.

## Logging Modes

- `--log <jsonl-path>` enables bounded operational JSONL output for rule decisions, suppression, queue outcomes, action cancellation, injection results, cleanup, and circuit-breaker state.
- Operational logging omits unrelated input events and routine mouse movement.
- `--trace-input` expands the selected log with redacted normalized hook input, including forwarded events.
- Trace logging aggregates ordinary mouse movement without absolute coordinates and limits each aggregate to a bounded event count.
- Arbitrary keyboard input is stored as `OtherKeyboard` without virtual-key or scan-code values; exact values are retained only for Phase 1 controls.
- Hook callbacks never format JSON, write files, flush streams, or wait for the logging worker.

## Deliverables

- `bin/InputWeaver.exe`: console hook host, observer, target resolver, and fixed-rule remapper.
- `bin/InputWeaverTests.exe`: dependency-free automated tests with internal helper processes where process lifecycle coverage is required.
- `script/build.bat`: canonical Phase 1 build command.
- `script/test.bat`: canonical automated test command.
- `script/run_inputweaver.bat`: canonical fixed-rule console launch wrapper.
- `res/InputWeaver.manifest`: explicit `asInvoker` and `uiAccess=false` execution manifest.
- `res/InputWeaver.rc`: resource script that embeds the execution manifest.
- `docs/phase-1/Verification.md`: build, automated-test, and interactive verification record.

## Automated Verification

- Physical, self-injected, and external-injected classification passes for keyboard and mouse input.
- A matching extra-information value without the Windows injected flag remains `PhysicalCandidate`.
- Every generated keyboard and mouse input, including cleanup releases, contains the active self tag.
- Physical F6 produces exactly one F7 tap and never produces F8 through recursion.
- Physical F7 produces exactly one F8 tap.
- Physical F9 produces exactly one middle-button click and never produces F10 through recursion.
- A physical middle-button press produces exactly one F10 tap.
- Self-injected and external-injected inputs schedule no rule action.
- Repeats, paired capture, startup state seeding, output conflicts, physical-state generations, queue rejection, and concurrent capture disable remain fail-open or paired as specified.
- Action and diagnostic rings preserve FIFO order, enforce capacity, and report rejection or drop counters without blocking.
- Partial or failed injection triggers tagged cleanup, and repeated failure opens the circuit breaker and disables new captures.
- Operational logging excludes unrelated input, trace logging includes redacted input, mouse movement aggregation is bounded, and the JSONL byte limit is enforced.
- Executable basename and absolute-path selectors cover unique, missing, ambiguous, invalid, inaccessible, and exiting candidates.
- Target integrity comparison, retained-handle liveness, target exit, observer lifecycle, worker readiness, producer completion, cleanup, and orderly shutdown are covered.

## Interactive Verification

- Start a normal interactive target through its executable selector and confirm that fixed rules remain inactive while another process owns the foreground window.
- Bring the target to the foreground and confirm F6 produces F7 without F8, while physical F7 still produces F8.
- With the pointer routed to the target, confirm F9 produces one middle-button click without F10 and a physical middle-button press produces F10.
- Confirm that the keyboard and mouse outputs are classified as `SelfInjected` and schedule no additional action.
- Keep the target foreground while routing the pointer to another process and confirm mouse-bound rules remain fail-open.
- Confirm basename and absolute-path selectors resolve the intended process and ambiguous matches wait unless exactly one matching instance owns the foreground window.
- Confirm a normal-integrity target works with the default executable and a higher-integrity target requires an equally elevated remapper because of UIPI.
- Stop while a captured source is held and confirm no keyboard or mouse control remains stuck.
- Exercise sustained keyboard repeat and high-rate mouse movement while recording hook duration, queue rejection, diagnostic drop, and JSONL truncation metrics.
- Compare a default operational log with a `--trace-input` log and confirm routine unrelated mouse input appears only in the trace and remains aggregated.

## Completion Gate

Phase 1 may be marked `Complete` only after both executables build with warnings treated as errors, `script/test.bat` passes, the current CLI and target-selection checks pass, physical keyboard and mouse mappings pass, real mouse output is confirmed as `SelfInjected`, stress and integrity checks pass, and `docs/phase-1/Verification.md` records the commands and results.

## Safety Rules

- Unexpected states, inactive targeting, selector failures, queue rejection, and logging failure forward input unless a valid paired capture already requires suppression.
- A new source event is consumed only after its action batch has been accepted by the queue and its capture state has committed.
- Target integrity is validated before fixed rules arm, every send result is checked, and failed output cleanup disables new captures.
- Hook callbacks do not sleep, parse files, allocate unbounded memory, write files, wait on workers, or call external programs.
- Injected events are never suppressed merely because they are ignored as rule sources.
- The physical Ctrl+Shift+F12 stop path remains independent of target focus.
- Low-level hook timeout removal is fail-open and may be silent, so callback work remains bounded and stress evidence records observed local duration.

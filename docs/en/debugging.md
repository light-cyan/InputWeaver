<a id="section-debugging-and-troubleshooting"></a>

# Debugging and troubleshooting

[简体中文](../zh/debugging.md)

[Documentation home](README.md) · [Using the interface](tui.md)

First check whether input was observed, then whether conditions matched, and finally whether actions completed. The Debug page shows these stages together; Console and logs provide specific errors.

<a id="section-starting-a-debugging-session"></a>

## Starting a debugging session

1. Select the program to inspect on the Program page and leave source editing.
2. Press `T` to enable Trace and Debug. To check matching and action calculations first, also press `S` for a dry run.
3. Press Space. The interface enters Debug, briefly shows `STARTING`, and starts the executor.
4. Bring the target application to the foreground and trigger the input to inspect.
5. Return to Debug to inspect EVENTS, variables, and action records.

```weave
TARGET = "notepad.exe";
number count = 0;

F6:down => set(count, count + 1) tap(B);
```

With this example, pressing F6 while Notepad is in the foreground records input in EVENTS, increases `count` in VARIABLES, and shows the rule's execution in ACTION EXECUTIONS. During a dry run, Notepad receives the original F6, while B output is simulated.

<a id="section-five-interactive-areas"></a>

## Five interactive areas

| Area | What to inspect |
| --- | --- |
| EVENTS | Key, mouse button, and meter tick events, and whether input passes through or is suppressed |
| INPUT STATE | Currently held keys, input origins, and live mouse state |
| METERS | Current meter progress and the latest completed intervals |
| VARIABLES | Current built-in settings, user variables, and arrays |
| ACTION EXECUTIONS | Which rule matched, what ran, and whether it completed or was canceled |

HEALTH at the bottom shows connection, capture, and execution problems. `Tab` cycles through the five areas; use arrow keys, `PageUp`, `PageDown`, `Home`, and `End` to scroll within an area, and `Esc` to return to area selection.

<a id="section-reading-events"></a>

## Reading EVENTS

Each row contains TIME, SOURCE, EVENT, ORIG, PASS, and COUNT.

| Column | Meaning |
| --- | --- |
| TIME | Capture time |
| SOURCE | Key, mouse button, or meter name |
| EVENT | Event type, such as `down`, `up`, `AGAIN`, or `tick` |
| ORIG | Input origin |
| PASS | `PASS` means pass through; `DROP` is the suppression decision calculated by the rules |
| COUNT | Number of merged meter ticks; `_` for ordinary keys and buttons |

| Origin label | Meaning |
| --- | --- |
| `PHY` | Identified as physical input |
| `ECHO` | Simulated input from the current executor |
| `EXT` | Simulated input from another source |
| `INIT` | A key or button already held when capture started |

`AGAIN` means another press report arrived while the control was already held; `NO-DOWN` means a release arrived before capture established the matching held state. Consecutive repeated presses with the same source and disposition are merged, as are consecutive ticks from the same meter.

During a dry run, `DROP` means normal execution would suppress the input; actual physical input still passes through. An input row only confirms observation. Whether a user rule matches also depends on the target, pause state, and input origin.

<a id="section-inspecting-variables-and-mouse-state"></a>

## Inspecting variables and mouse state

At the top of VARIABLES, four read-only settings appear in a dimmer color: `TAP_DURATION` (default press duration), `ACTION_GAP` (gap between actions), `MOUSE_IDLE_TIMEOUT` (mouse idle timeout), and `RAND_SEED` (random seed). These are the values used by the running program, including defaults for settings omitted from the source. Durations include units, and the random seed is shown as a full integer.

User variables and arrays follow, including zero values, `off` values, and empty arrays. Long arrays show their length and some elements from the beginning and end. HEALTH shows `PAUSE`: `on` enables ordinary rules, and `off` pauses them.

INPUT STATE shows held keys and their origins, followed by mouse coordinates, displacement, wheel amounts, movement state, and idle time. Paired wheel amounts appear in horizontal, then vertical order.

In METERS, the current row shows progress, interval, and coordinates; the `@` row below shows the latest completion record, or `empty` if there is none. The interface displays at most one decimal place, while program calculations retain their original precision. [Meter fields](mouse.md)

<a id="section-checking-action-completion"></a>

## Checking action completion

Each ACTION EXECUTIONS record includes the triggering event, match time, condition `AS`, and actions `ACT`. A rule without a condition shows `always`. Full mappings also produce execution records.

| Color | Status |
| --- | --- |
| Cyan | Running or waiting |
| Dark green | Completed |
| Red | Failed |
| Yellow | Canceled |

Target changes, pausing, stopping, and runtime faults can cancel the affected actions. Conditions and actions in the record correspond to the source program; scroll to inspect longer action sequences.

<a id="section-capture-versus-stopping"></a>

## Capture versus stopping

`C` stops capture or starts a new capture while the executor continues running. Restarting capture clears old interface records and builds a new view from current state; program variables, arrays, meters, and pause state persist.

`X` stops the current Debug executor and clears the debug view. If the executor ends on its own, the interface retains its final state for inspection; press `X` afterward to clear it, or start a new debugging session.

`Complete snapshot` in HEALTH means the final state at shutdown was fully obtained; `Best-effort snapshot` means some of the last data may be missing. If input records arrive too quickly to process, Debug reports capture overflow and attempts to fetch current state again before continuing to display updates.

<a id="section-common-problems"></a>

## Common problems

| Symptom | What to check |
| --- | --- |
| Compilation fails | Inspect red SOURCE highlights or Console line and column diagnostics; check names, types, semicolons, and brackets |
| Compiled file cannot be read or has an incompatible version | Recompile the `.weave` source with the compiler from the same release package, then run the new artifact; see [compiled file versions](command-line.md#section-compiled-file-version-mismatch) |
| Startup reports an unavailable raw control | Check platform support for the control's intended use and overlapping encodings of the same physical input; see [Windows control capabilities](windows.md#section-control-encodings-and-support) |
| Waiting for the target | Check that the application is running and `TARGET` is its actual executable name; if several instances match, bring the intended one to the foreground |
| EVENTS shows input but no action appears | Check that the target is foreground, the mouse position belongs to it, `PAUSE` is `on`, the input is physical, and the `when` condition is satisfied |
| Rules do not trigger while operating the TUI | The host automatically excludes the TUI; test in the target application |
| An action completes without visible keyboard or mouse output | Check `S` or `--dry-run`, target foreground status, and output position |
| Output characters differ from expectations | Check keyboard layout, input method, Caps Lock, and physical modifier keys still held |
| Actions turn yellow or repetition stops | Check target changes, pausing, and stopping; canceled actions need new input to trigger again |
| Array or meter expressions report errors | Check indices, array length, `pop` on an empty array, `@name.valid`, and positive meter intervals |
| One evaluation error ends the whole executor | Check expressions in conditions, scalar `set`, `wait`, and control flow; division by zero or out-of-range access there requests an executor stop; see [error handling](running.md#section-expression-and-action-errors) |
| An `exec` launch is rejected | Enable `P` for this run or pass `--allow-exec` |
| Edited source has not changed runtime behavior | Stop and run again, and confirm compilation succeeds |
| Macros continue after closing the window | Restore the interface from the tray and press `X`, or choose `Exit InputWeaver` in the tray menu |

The target application and InputWeaver normally both run as a regular user; different privilege levels can affect input injection. For injection failures, first align their privileges, then inspect the specific Console and log errors.

<a id="section-saving-diagnostic-logs"></a>

## Saving diagnostic logs

Choose Logging in PROGRAM INFORMATION or use `--log` on the command line. For input and output traces, choose `Input Trace` or add `--trace-input`. [Command-line examples](command-line.md)

Each line of a JSONL file is one record. Every record has `schema`, `schema_version`, `session_id`, and Unix-millisecond `time_unix_ms` fields; records from one execution share a session ID. Logs may contain program paths, target and excluded-process selectors, target image paths, and process IDs. Input traces may additionally contain keys and mouse positions; review the relevant content before sharing logs.

Common runtime issues include:

| Record or metric | Meaning and troubleshooting direction |
| --- | --- |
| `SessionStart`, `ConfigurationResolved` | Startup option summary and the resolved effective target configuration |
| `StartupFailure` | `.weavec` loading, target resolution, or executor-component startup failed; inspect `stage`, `code`, `win32_error`, and `detail` |
| `TargetSearchStarted`, `TargetSearchWaiting`, `TargetSearchAmbiguous` | Target search started, has not found a process, or found multiple candidates |
| `TargetSearchFailure` | Target discovery or validation failed; inspect `code` and `win32_error` |
| `TargetFound`, `TargetAttached`, `TargetLost`, `TargetAttachFailure` | Target discovery, attachment, exit, or attachment failure with the available PID and image path |
| `SessionStop` | Session stop reason and executor exit code |
| `TargetEligibilityChange` | Target eligibility changed; check foreground window switching |
| `PhysicalStateSynchronization` | An input source established its initial released baseline |
| `PredicateFault` | Condition or meter interval evaluation failed; check the [scope of the error](running.md#section-expression-and-action-errors) at its location |
| `TaskExpressionFault` | Action argument evaluation failed; inspect the expression and the final task or executor state |
| `TaskActionFault` | An action operation failed, such as `pop` on an empty array or limited array growth; inspect the record location and details |
| `TransactionCapacity` | Too much simultaneous work rejects new work; too many completed intervals in one mouse report stops the executor; see [resource limits](running.md#section-resource-limits) |
| `TaskBudgetExceeded` | Too much uninterrupted action execution; check loops and waits |
| `OutputRateExceeded` | Output is too dense; increase gaps or reduce output |
| `Cancellation` | A task was canceled due to target, pause, stop, or another cause |
| `injection_failures` | Number of Windows input injection failures |
| `transaction_rejections` | Number of times new work matched by one event was rejected together due to capacity |
| `rejected_array_growth` | Array growth was rejected by capacity or allocation limits |

A single log file is limited to 8 MiB. At shutdown, inspect the final statistics in `Diagnostic log stopped.`: dropped counts and `jsonl_truncated` indicate whether the log is complete. [Resource limits and handling](running.md)

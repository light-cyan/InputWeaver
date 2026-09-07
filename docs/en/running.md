<a id="section-runtime-behavior-and-limits"></a>

# Runtime behavior and limits

[简体中文](../zh/running.md)

[Documentation home](README.md)

This page explains how target changes, pausing, concurrent actions, and resource shortages affect a program. Target selection, foreground window, and application exclusion descriptions apply to the Windows executor. For language syntax, see [Mappings and rules](rules.md) and [Actions and control flow](actions.md); for supported Windows keyboard and mouse operations, see [Windows executor](windows.md).

<a id="section-selecting-a-target"></a>

## Selecting a target

`TARGET` in the source supplies the default target; the interface's Target setting or command-line options can override it. An application target accepts an executable filename or absolute path. Global execution uses `GLOBAL`, the interface's `Global`, or the command-line option `--target-global`.

Windows compares process names and normalized paths without regard to case. A single matching instance is selected; if several match, the foreground instance is preferred, and unresolved ambiguity makes the executor wait for a clear selection. Once selected, the executor binds to that specific process. Bringing another process with the same name to the foreground does not change the binding; target discovery resumes only after the bound process exits.

Application-scoped execution requires the target to own the foreground window; mouse input also checks whether the pointer position belongs to the target. While the target is absent, the executor keeps waiting and observing input. Debug and physical exit rules remain available; ordinary mappings and actions start accepting new input when the target becomes eligible.

<a id="section-excluding-an-application"></a>

## Excluding an application

`--exclude` specifies one executable filename or absolute path. While the excluded application is in the foreground, ordinary rules and mappings ignore input, physical input passes through, and new keyboard and mouse output stops. This applies to both application and global targets.

Exit rules take priority over exclusion: a match still suppresses input and stops the executor. During a dry run, physical input always passes through.

The host automatically sets `--exclude InputWeaverTUI.exe` for every executor it starts, so you can edit and stop programs. Cleanup output still releases keys that the program already holds.

<a id="section-switching-windows-and-restarting-the-target"></a>

## Switching windows and restarting the target

When the target loses foreground eligibility, running or waiting actions are canceled, active mappings are cleared, and held output is released. After switching back, new input triggers new work.

For example, holding A establishes `A -> B`; switching to another window releases output B. After returning to the target, release A and press it again to establish a new mapping.

When the target process exits, the executor clears the affected work and waits for the target to reappear. Once a target is found again, the same running program continues: user variables, arrays, and `PAUSE` state persist, canceled actions need a new trigger, and meter statistics start afresh.

<a id="section-pausing-stopping-and-running-again"></a>

## Pausing, stopping, and running again

| Operation | Actions and output | Variables, arrays, and random sequence |
| --- | --- | --- |
| `PAUSE` state change | Cancels existing tasks, clears mappings, and releases output | Preserved |
| Loss of target eligibility | Cancels target-related work and releases output | Preserved |
| Stop the executor | Ends the current run and releases output | Current run ends |
| Run the program again | Starts from the initial state declared in the source | Reinitialized |

`PAUSE` set to `on` enables ordinary rules; `off` pauses them. A `pause` rule toggles it in the source. Stop with an exit rule, `X` in the interface, or `Exit InputWeaver` in the tray menu. [Interface operations](tui.md)

<a id="section-keys-already-held-at-startup"></a>

## Keys already held at startup

At startup, the executor reads the physical state of controls used by the program. Initialization lets conditions determine which keys are already held, but does not itself trigger a `down` event.

Some raw controls can establish their state only from later events. These sources initially pass input through and establish a baseline after observing a physical release. When testing a new program, release the relevant keys first, then perform a complete press and release.

<a id="section-triggering-several-rules-together"></a>

## Triggering several rules together

All rule conditions for the same input read the same variable and array state. Once matching finishes, nonempty actions from matching rules are queued in source order. Each execution of a rule is called a task.

A task continues through adjacent actions until it finishes, is canceled, encounters a positive wait, or yields at the end of a loop iteration. Other tasks can run at these points and read or write shared variables.

`wait(0ms)`, zero gaps, and zero-duration `tap` actions continue immediately. A loop yielding does not mean a fixed amount of time has passed; use an explicit positive `wait` to control output timing.

Each task releases the output it holds. If several tasks hold the same key, the final release is sent only when the last holder releases it. This maintains shared output state, and means overlapping taps can combine into one hold.

<a id="section-what-a-dry-run-checks"></a>

## What a dry run checks

A dry run preserves input observation, rule matching, task execution, variable changes, pause, exit, and debugging. Physical input always passes through; keyboard and mouse output only simulates success.

Consecutive mouse output actions calculate their next step from a simulated pointer position; new physical mouse input realigns it. `Mouse.x` and `Mouse.y` still show the actual pointer position.

`exec` still requires authorization for the current run, and simulates success after checks pass. `DROP` in Debug and suppression counts in statistics describe the decision that normal execution would make. [Debugging and troubleshooting](debugging.md)

<a id="section-expression-and-action-errors"></a>

## Expression and action errors

Evaluation errors such as division by zero, out-of-range array indices, nonfinite numbers, and duration overflow have different effects depending on where they occur. Constant errors detectable during compilation produce compile diagnostics. At runtime, missing meter completion records have special handling; other evaluation errors are handled according to their location:

| Error location or situation | Runtime handling |
| --- | --- |
| Reading an `@name` field before any completion record exists | The condition does not match, or the current action task ends, with a diagnostic; `@name.valid` itself remains readable |
| Conditions of ordinary rules, full mappings, pause rules, or exit rules | Reports a diagnostic and requests that the whole executor stop |
| Right-hand expression of scalar `set`, duration of `wait`, condition of `if` or `while`, or count of `repeat` | Reports a diagnostic and requests that the whole executor stop |
| Index or value expressions in array actions, `pop` on an empty array, or failed array growth | Reports a diagnostic, ends the current task, and releases its output; other tasks continue |
| `release` of a control not held by the current task | Reports a diagnostic, ends the current task, and releases its remaining output; subsequent actions do not run |
| Argument evaluation for `move_by`, `move_to`, `scroll`, or `scroll_horizontal` | Reports a diagnostic, ends the current task, and releases its output; other tasks continue |
| A dynamic meter interval fails to evaluate or is not positive | Reports a diagnostic, clears that meter's unfinished progress, and retries on later eligible input; the latest completion record is retained |

Stopping the executor cancels all tasks, clears mappings, and releases program-held output. When only the current task ends, earlier variable and array changes remain; restarting the executor restores the declared initial state.

The same kind of error can have different effects. For example, the array read in `set(count, values[index])` is part of a scalar assignment expression, so an out-of-range index requests an executor stop; an index error in `set(values[index], 1)` belongs to an array action and ends only that task. Guard divisors, index ranges, array lengths, and accesses requiring `@name.valid`. [Finding diagnostics](debugging.md)

<a id="section-resource-limits"></a>

## Resource limits

Controls, variables, rules, and concurrent tasks have limits. A program that exceeds limits at startup reports required and available quantities; exceeding limits during execution can reject newly triggered actions, cancel the current task, or stop the whole executor.

The following limits commonly matter when troubleshooting complex macros:

| Item | Default limit |
| --- | ---: |
| Controls used | 4096 |
| Variables of each scalar type | 4096 |
| Arrays | 4096 |
| Total memory used by all arrays | 64 MiB |
| Distinct source controls used for mappings | 4096 |
| Ordinary rules for one event, including press rules from full mappings | 256 |
| Exit or pause rules for one event | 64 each |
| Concurrent action tasks | 256 |
| Output controls held by one task | 256 |
| Ordinary outputs per task before an actual wait | 4096 |
| Ordinary output rate | 2048 per second |
| Meter intervals completed by one physical mouse report | 1024 |

If newly triggered tasks or pending work exceed capacity, tasks and mapping operations matched by the same event are rejected together, with `TransactionCapacity`; if it is a physical input event, that input passes through. The original mouse event and each tick are handled separately: rejecting one tick's work does not undo other accepted work or change the original mouse report's suppression decision. Reduce the number of rules triggered by one input, or check for many unfinished tasks.

If one physical mouse report produces more than 1024 completed intervals, the executor reports `TransactionCapacity` and requests a full stop, canceling all tasks and releasing output; that report passes through.

Multiple conditional mappings of one source control count as one source. For example, `A -> C when LCtrl == held;` and `A -> B;` together use one mapping source, while counting as two press rules toward the `A:down` rule limit. Aliases such as `A` and `Keyboard.A` are counted together.

Presses, repeated presses, mouse movement, and scrolling count as ordinary output; cleanup releases do not use this allowance. The current task is canceled if uninterrupted actions or loops reach the execution limit, or if uninterrupted output exceeds its limit. An actual positive wait resets the task's uninterrupted execution and output counts; `wait(0ms)` and merely starting another loop iteration do not. Exceeding the ordinary output rate cancels the task or mapping that produces excess output and cleans up its output.

For `TaskBudgetExceeded`, check waits in long action sequences and loops; for `OutputRateExceeded`, lower output frequency. If array growth is rejected, check for continuous appending without cleanup. [Finding logs](debugging.md)

<a id="section-finding-failure-information"></a>

## Finding failure information

In startup errors, `code` identifies the failure category, `subject` identifies the particular limit, and `required` and `available` give the program's requirement and executor capacity.

For runtime problems, inspect HEALTH, ACTION EXECUTIONS, and Console in Debug; enable logging to preserve information. See [Debugging and troubleshooting](debugging.md) for detailed input and action checks.

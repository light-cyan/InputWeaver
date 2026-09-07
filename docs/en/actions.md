<a id="section-actions-and-control-flow"></a>

# Actions and control flow

[简体中文](../zh/actions.md)

[Documentation home](README.md) · [Previous: Input mappings and rules](rules.md) · [Next: Mouse and meters](mouse.md)

A rule's arrow is followed by a sequence of actions. Actions run in written order to press keys, wait, change variables, move the pointer, or start programs.

<a id="section-sequence-actions"></a>

## Sequence actions

```weave
TARGET = "notepad.exe";

F6:down => tap(H) wait(200ms) tap(I);
```

F6 taps H, waits 200 milliseconds after releasing it, then taps I. Adjacent actions can be on one line or multiple lines. Their actual timing is controlled by `tap`, `wait`, `gap()`, and `|`.

<a id="section-press-release-and-tap"></a>

## Press, release, and tap

| Action | Effect |
| --- | --- |
| `press(A)` | Holds output A; the current task is responsible for releasing it |
| `release(A)` | Releases A held by the current task |
| `tap(A)` | Presses A, holds it for `TAP_DURATION`, then releases it |

This program performs Ctrl+C within one task:

```weave
F6:down =>
    press(LCtrl)
    tap(C)
    release(LCtrl);
```

Keep `press` and `release` in the actions of the same rule. Every trigger creates its own task, so a `release` in another rule cannot release a key held by the earlier task. Releasing a control the current task does not hold reports an error and ends that task; subsequent actions do not run. To hold and release an output with its source key, use a [complete mapping](rules.md), such as `A -> B;`.

A task releases its remaining held outputs when it completes, is cancelled, or fails. If multiple tasks or mappings hold the same output key, the key is released only when its final holder releases it. Overlapping `tap` actions can therefore appear as one longer hold.

These actions also accept mouse buttons, such as `tap(Mouse.Left)`.

<a id="section-waits-and-default-gaps"></a>

## Waits and default gaps

```weave
TAP_DURATION = 30ms;
ACTION_GAP = 100ms;

F6:down => tap(A) | tap(B) gap() tap(C);
```

`|` and `gap()` are equivalent. Here they wait 100 milliseconds after A and B are released. `wait` can use a time variable or expression:

```weave
duration delay = 80ms;

F6:down => tap(A) wait(delay * 2) tap(B);
```

Pausing, changing targets, or stopping can interrupt a wait. `0ms` continues immediately. For periodic execution, put a positive wait inside the loop.

<a id="section-change-variables-and-arrays"></a>

## Change variables and arrays

| Action | Purpose |
| --- | --- |
| `set(count, count + 1)` | Updates a scalar variable with a value of the same type |
| `set(values[index], 10)` | Updates an existing array element |
| `toggle(enabled)` | Switches between `on` and `off` |
| `toggle(gates[index])` | Toggles an existing `state` array element |
| `append(values, 10)` | Appends an element of the same type |
| `pop(values, last)` | Removes the last element and writes it to a scalar variable of the same type |
| `clear(values)` | Empties an array |

```weave
number count = 0;
number[] history = [];
number last = 0;

F6:down =>
    set(count, count + 1)
    append(history, count);

F7:down =>
    if history.length > 0 then
        pop(history, last)
    end;
```

Array indices must be within the current length, and `pop` requires a nonempty array. Each modification action completes its own read and write. Other tasks can change shared variables after a wait or a loop yields execution.

<a id="section-choose-actions-with-a-condition"></a>

## Choose actions with a condition

`if` evaluates its condition when execution reaches it. A true condition selects the `then` branch; otherwise the `else` branch runs. Close the selection with `end`.

```weave
number count = 0;

F6:down =>
    set(count, count + 1)
    if count >= 3 then
        tap(C)
        set(count, 0)
    else
        tap(B)
    end;
```

Every three presses of F6 produce B, B, then C. This `if` reads the value just written by the preceding `set`. Omit `else` when you only need actions for a true condition.

<a id="section-repeat-a-fixed-number-of-times"></a>

## Repeat a fixed number of times

```weave
F6:down =>
    repeat 3 do
        tap(B)
        wait(100ms)
    end;
```

`repeat` reads its count once on entering the loop. Positive fractions round down, so `3.8` means three iterations. Zero and negative counts mean zero iterations. The count can also be a `number` variable or expression.

<a id="section-continue-while-a-condition-holds"></a>

## Continue while a condition holds

This program repeatedly clicks the left mouse button while F6 is held:

```weave
F6:down =>
    while F6 == held do
        tap(Mouse.Left)
        wait(100ms)
    end;
```

`while` checks its condition at the start of each iteration. Releasing F6 allows the current iteration to finish; the next check exits the loop. Pausing, losing the target, or stopping cancels the task directly.

`if`, `repeat`, and `while` can be nested and mixed with ordinary actions. Each inner structure has its own `end`, and the whole event rule ends with a semicolon.

<a id="section-repeated-triggers-and-concurrent-tasks"></a>

## Repeated triggers and concurrent tasks

Triggering a rule again while its previous actions are waiting creates another task. Tasks share variables but keep their own wait state, loop progress, and held outputs.

If a macro should normally run only once until it finishes, use a state variable as a gate:

```weave
state busy = off;

F6:down when busy == off =>
    set(busy, on)
    repeat 3 do tap(B) wait(100ms) end
    set(busy, off);

F7:down => set(busy, off);
```

`busy` is a user variable. Cancelling a task also cancels a pending `set(busy, off)`, so the example provides F7 for a manual reset. Closely spaced inputs can still match before the earlier task writes `busy`. See [Runtime behavior and limits](running.md) for the exact execution order.

<a id="section-launch-an-external-program"></a>

## Launch an external program

```weave
F6:down => exec("notepad.exe");
```

A program using `exec` requires permission on each run: enable NEXT RUN option `P` in the interface, or pass `--allow-exec` on the command line.

A command can contain arguments and paths with spaces, such as `exec("\"C:\\Tools\\My Helper.exe\" --mode quick")`. See [Language basics](language.md) for string escapes.

After the process starts successfully, execution continues with the following actions. The external process has its own lifetime. The Windows executor parses and launches the executable directly. Explicitly launch a command interpreter when you need its pipes or redirection. See [Windows executor](windows.md#section-executable-lookup-and-working-directories) for executable lookup, relative paths, and the child's working directory.

During a dry run, `exec` still requires permission but only simulates a successful launch. [Command-line options](command-line.md)

<a id="section-mouse-actions"></a>

## Mouse actions

`move_by(dx, dy)` moves the pointer relatively, `move_to(x, y)` moves it to screen coordinates, `scroll(amount)` and `scroll_horizontal(amount)` scroll, and `restart(name)` resets a meter. See [Mouse and meters](mouse.md) for coordinates, units, and examples.

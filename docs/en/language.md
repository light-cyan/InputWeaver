<a id="section-language-basics"></a>

# Language basics

[简体中文](../zh/language.md)

[Documentation home](README.md) · [Next: Input mappings and rules](rules.md)

A Weave program usually consists of target settings, variable declarations, and input rules. Settings choose where the program runs, variables store state, and rules describe what to do when an input occurs.

```weave
TARGET = "notepad.exe";

state enabled = on;
number count = 0;

F6:down => toggle(enabled);
A:down when enabled == on => tap(B) set(count, count + 1);
```

F6 toggles `enabled`. While it is `on`, each press of A taps B and increments `count`. `state` and `number` are variable types, `when` introduces the trigger condition, and the actions follow the arrow.

The short examples below can go inside a program that already sets `TARGET`, or run on their own after choosing an application in the interface's Target field. Stop and run again after each edit to observe the updated source.

<a id="section-source-format"></a>

## Source format

Source uses UTF-8. Keywords, names, strings, and punctuation use ASCII characters; comments may contain Chinese text. Names are case-sensitive: `count` and `Count` are different names, and the builtin `TARGET` must be uppercase.

Variable names start with an English letter, followed by letters, digits, or underscores, such as `enabled`, `step2`, and `fire_delay`. Declare variables, arrays, and meters before use. Avoid keywords, builtin values, and key names when naming them.

End each top-level setting, declaration, mapping, or rule with `;`. Newlines and indentation control layout. A rule containing several actions has one semicolon at the end of the whole rule.

```weave
// A short comment.
TARGET = "notepad.exe";

/* A comment can span
   more than one line. */
F6:down =>
    tap(H)
    wait(100ms)
    tap(I);
```

<a id="section-reserved-words-and-names"></a>

### Reserved words and names

Variables, arrays, and meters share one namespace. Declaration names are case-sensitive and must be unique. The language uses the words and fixed spellings below; avoid them and [named controls](rules.md#section-key-name-reference) when declaring your own names.

| Category | Reserved words or fixed spellings |
| --- | --- |
| Declarations | `state`, `number`, `duration`, `meter` |
| Settings and builtin values | `TARGET`, `TAP_DURATION`, `ACTION_GAP`, `MOUSE_IDLE_TIMEOUT`, `RAND_SEED`, `RAND01`, `PAUSE`, `Mouse` |
| Constants | `GLOBAL`, `on`, `off`, `held`, `idle` |
| Rules and meter periods | `exit`, `pause`, `when`, `every` |
| Event suffixes | `down`, `again`, `up`, `move`, `wheel`, `horizontalwheel`, `tick` |
| Control flow | `if`, `then`, `else`, `end`, `repeat`, `do`, `while` |
| Logical operators | `and`, `or`, `not` |
| Key and wait actions | `press`, `release`, `tap`, `wait`, `gap` |
| Variable and array actions | `set`, `toggle`, `append`, `pop`, `clear` |
| Process, mouse, and meter actions | `exec`, `move_by`, `move_to`, `scroll`, `scroll_horizontal`, `restart` |
| Raw controls | `HID.Usage`, `Windows.VirtualKey`, `Windows.ScanCode`, `Linux.Key`, `MacOS.KeyCode` |
| Scan-code prefixes | `E0`, `E1` |

A qualified control name combines a namespace and member, such as `Keyboard.A`. When choosing a key representation, also check that the platform supports its intended use. See [Windows executor](windows.md) for supported Windows input and output.

<a id="section-properties-and-fields"></a>

### Properties and fields

Read properties and fields with a dot, such as `values.length`, `Mouse.x`, and `path.progress`. A meter's completed interval uses a spelling such as `@path.dx`.

| Owner | Property or field names |
| --- | --- |
| Array | `length` |
| Current `Mouse` state | `x`, `y`, `dx`, `dy`, `wheel_x`, `wheel_y`, `moving`, `idle_time` |
| Meter, depending on its type and interval view | `x`, `y`, `dx`, `dy`, `wheel_x`, `wheel_y`, `moving`, `start_x`, `start_y`, `distance`, `period`, `progress`, `remaining`, `valid` |

Property and field names are recognized in the context of their owner and are also available for user declarations, such as `number length = 0;` and `number dx = 0;`. `values.length` reads an array's length; standalone `length` reads a user variable with that name. An array named `length` exposes its length as `length.length`. See [Current mouse state](mouse.md#section-reading-current-mouse-state) and [Meter fields](mouse.md#section-field-reference) for field types and availability.

<a id="section-configure-a-program"></a>

## Configure a program

Settings appear at the top level, at most once each.

| Setting | Default | Syntax and purpose |
| --- | --- | --- |
| `TARGET` | A target is required at runtime | `"notepad.exe"` selects an application; `GLOBAL` selects global operation. The interface and command line can override it |
| `TAP_DURATION` | `30ms` | How long one `tap` holds a key, from `0ms` to `1min` |
| `ACTION_GAP` | `10ms` | Wait inserted by `\|` and `gap()`, from `0ms` to `1min` |
| `MOUSE_IDLE_TIMEOUT` | `80ms` | Positive time without physical movement after which the mouse is considered idle |
| `RAND_SEED` | `0` | Random seed, a decimal integer from `0` to `18446744073709551615` |

```weave
TARGET = "notepad.exe";
TAP_DURATION = 40ms;
ACTION_GAP = 100ms;

F6:down => tap(H) | tap(I);
```

H and I are each held for 40 milliseconds. After H is released, the program waits 100 milliseconds before pressing I.

`TARGET` also accepts an absolute executable path, such as `TARGET = "C:\\Tools\\Editor.exe";`. See [Runtime behavior and limits](running.md) for target selection and window changes.

<a id="section-three-variable-types"></a>

## Three variable types

| Type | Stores | Declaration example |
| --- | --- | --- |
| `state` | `on` or `off` | `state enabled = on;` |
| `number` | Integers or fractions | `number distance = 24.5;` |
| `duration` | Nonnegative time | `duration delay = 80ms;` |

Initialize a declaration with a literal of the corresponding type, such as `number count = 0;`. Use `set` to change a variable at runtime and `toggle` to switch a `state`. Variables are shared by the whole running program.

```weave
number count = 0;
duration delay = 100ms;
state enabled = off;

F1:down => set(count, count + 1);
F2:down => set(delay, delay + 10ms);
F3:down => toggle(enabled);
```

Numbers use decimal notation, such as `12`, `0.5`, and `-2.5`. A `number` uses double-precision floating point and must remain finite.

A time value places a number immediately before its unit: `ms` for milliseconds, `s` for seconds, or `min` for minutes. Examples include `30ms`, `1.5s`, and `2min`. Time has nanosecond precision.

<a id="section-comparisons-and-conditions"></a>

## Comparisons and conditions

`when` and `if` require a condition. Use comparisons to turn variables or key states into conditions:

| Expression | Meaning |
| --- | --- |
| `enabled == on` | The state variable is on |
| `count >= 3` | The number is at least 3 |
| `LCtrl == held` | Left Ctrl is physically held |
| `Mouse.Left == idle` | The left mouse button is physically released |
| `count != 0` | The number is nonzero |

`on` and `off` are `state` values; `held` and `idle` are key states. Comparisons and logical operations produce Boolean conditions for `when`, `if`, and `while`. Use a comparison such as `enabled == on` to include a `state` variable in a condition. Scalar declarations use `state`, `number`, or `duration`.

`and` requires both sides, `or` requires at least one side, and `not` reverses the condition. Parentheses make grouping explicit:

```weave
state enabled = on;

F6:down when enabled == on and (LCtrl == held or RCtrl == held) => tap(B);
```

`and` and `or` evaluate from left to right and skip the right side once the result is determined. This can protect array accesses or completed meter data.

<a id="section-number-and-time-operations"></a>

## Number and time operations

Numbers support `+`, `-`, `*`, `/`, and `%`, where `%` gives the remainder. Compare number sizes with `<`, `<=`, `>`, and `>=`. `state`, `number`, `duration`, and key states each support equality comparisons between values of the same type.

Time supports addition and subtraction of time values, multiplication by a number, and division by a number. For example, `delay + 20ms`, `delay * 2`, and `delay / 2` all produce time values.

```weave
duration delay = 100ms;
number count = 0;

F6:down =>
    wait(delay / 2)
    tap(B)
    set(count, (count + 1) % 10);
```

Time subtraction is clamped to `0ms`; negative results from time scaling are also clamped to `0ms`. Fractional nanoseconds are truncated toward zero. Division or remainder by zero, an infinite numeric result, and time overflow are expression errors.

<a id="section-type-and-operator-reference"></a>

### Type and operator reference

Use the type combinations below. An array element read produces its element type, `number` or `state`; an array's `.length` is a `number`.

| Operation | Operand types | Result type | Example |
| --- | --- | --- | --- |
| Unary `+`, `-` | `number` | `number` | `-count` |
| `+`, `-`, `*`, `/`, `%` | Two `number` values | `number` | `count % 10` |
| `+`, `-` | Two `duration` values | `duration` | `delay + 20ms` |
| `*` | `duration` and `number`, in either order | `duration` | `2 * delay` |
| `/` | `duration` on the left, `number` on the right | `duration` | `delay / 2` |
| `<`, `<=`, `>`, `>=` | Two `number` values | Boolean condition | `count >= 3` |
| `==`, `!=` | Two `state` values | Boolean condition | `enabled == on` |
| `==`, `!=` | Two `number` values | Boolean condition | `count != 0` |
| `==`, `!=` | Two `duration` values | Boolean condition | `delay == 100ms` |
| `==`, `!=` | Two key-state values | Boolean condition | `LCtrl == held` |
| `and`, `or` | Two Boolean conditions | Boolean condition | `count > 0 and enabled == on` |
| `not` | One Boolean condition | Boolean condition | `not (count == 0)` |

Assign an expression of the same type as its destination: `set(delay, 100ms)` assigns time, while `set(count, 100)` assigns a number. Time equality uses `==` and `!=`. To compare thresholds by size, store values expressed in the same unit in `number` variables.

Precedence, from highest to lowest, is parentheses; unary `+`, `-`, and `not`; `*`, `/`, and `%`; `+` and `-`; size comparisons; equality comparisons; `and`; then `or`. Operators of the same precedence evaluate from left to right. Add parentheses to make long conditions clear.

<a id="section-arrays"></a>

## Arrays

An array stores values of one element type, `number` or `state`. Initialize it with brackets, read or change elements with zero-based indices, and read its current length with `.length`.

```weave
number[] values = [2, 4, 8];
state[] gates = [on, off];
number index = 0;

F1:down when index >= 0 and index < values.length =>
    set(values[index], values[index] + 1);
F2:down => toggle(gates[0]);
```

`values[0]` is the first element. Indices are nonnegative numbers; fractions round down, so `values[1.9]` reads the second element. The rounded index must be below the current length.

If another task can change a shared array or index, check the bounds again inside the action and keep the check and access consecutive. `when` checks state at matching time; other tasks can still change that state while the action is queued.

Arrays can grow and shrink:

```weave
number[] values = [];
number last = 0;

F1:down => append(values, 10);
F2:down =>
    if values.length > 0 then
        pop(values, last)
    end;
F3:down => clear(values);
```

`append` adds an element at the end, `pop` removes the last element and writes it to a scalar variable of the same type, and `clear` empties the array. The example checks the length inside the action and immediately performs `pop`, preventing another task from clearing the array between the check and operation.

An array's length changes with its contents. Use `set(values[index], value)` to update an existing element and `append` to add one. [Array action behavior](actions.md)

<a id="section-readable-builtin-values"></a>

## Readable builtin values

| Name | Type | Purpose |
| --- | --- | --- |
| `TAP_DURATION` | `duration` | Default key hold time |
| `ACTION_GAP` | `duration` | Default action gap |
| `MOUSE_IDLE_TIMEOUT` | `duration` | Mouse idle timeout |
| `RAND01` | `number` | Each read produces a random number greater than or equal to 0 and less than 1 |
| `PAUSE` | `state` | Ordinary-rule switch: `on` enables rules, `off` pauses them; initially `on` |

Expressions can read these values. Change user variables with `set` and `toggle`; change `PAUSE` with a dedicated [pause rule](rules.md).

```weave
RAND_SEED = 42;
number sample = 0;

F6:down => set(sample, RAND01) wait(50ms + 100ms * sample) tap(B);
```

Each `RAND01` read advances the random sequence. To reuse one sample in several places, store it in a variable as above. Restarting the program returns to the start of the seed's sequence. Pausing and cancelling actions preserve the sequence position. Concurrent actions sample in their actual execution order, which affects the values each receives.

<a id="section-strings"></a>

## Strings

Target paths and `exec` commands use double-quoted strings. Supported escapes are `\\`, `\"`, `\n`, `\r`, and `\t`; write a path backslash as `\\`.

```weave
TARGET = "C:\\Tools\\Editor.exe";

F6:down => exec("\"C:\\Tools\\Helper.exe\" --mode quick");
```

See [Actions and control flow](actions.md) for `exec` permission and argument handling. Continue with [Input mappings and rules](rules.md) to apply variables and conditions to real input.

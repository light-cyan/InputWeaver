# Weave V3 Language Definition

## Status

This document defines the Phase 13 Weave V3 source-language contract. The implementation phase promotes this contract to `docs/grammar.md` when the compiler, compiled-program format, runtime, and editor support it together.

## Complete Example

```weave
TARGET = "game.exe";
TAP_DURATION = 30ms;
ACTION_GAP = 10ms;

state combat = off;
number cursor = 0;
number popped = 0;
duration fireGap = 80ms;
number[] values = [2, 4, 8];
state[] gates = [on, off];

A -> B when combat == on;

A:down when cursor < values.length and values[cursor] > 0 ~>
    set(values[cursor], values[cursor] - 1)
    | set(cursor, cursor + 1);

F1:down when LCtrl == held =>
    if gates.length == 0 then
        append(gates, off)
    else
        toggle(gates[0])
    end;

F2:down =>
    append(values, 16)
    repeat 3.8 do tap(Mouse.Left) | end;

F3:down when values.length > 0 => pop(values, popped);
F4:down => clear(values);

pause Pause:down ~> toggle;
exit F12:down when LCtrl == held and LShift == held;
```

## Source Text

Source files are valid UTF-8. Language tokens, identifiers, control names, and string contents accept ASCII only. Valid non-ASCII text can appear in comments. Embedded NUL bytes are invalid inside and outside strings.

Spaces, tabs, carriage returns, and line feeds separate tokens without terminating statements. Every complete top-level item ends with `;`.

Line comments begin with `//` and end before the next line break or at end of file. Block comments begin with `/*` and end with `*/`; they can cross lines but cannot be nested.

## Names and Reserved Words

Names are case-sensitive. Identifiers begin with an ASCII letter and continue with ASCII letters, decimal digits, or `_`.

The reserved words and intrinsic names are `TARGET`, `TAP_DURATION`, `ACTION_GAP`, `PAUSE`, `GLOBAL`, `state`, `number`, `duration`, `exit`, `pause`, `when`, `on`, `off`, `held`, `idle`, `toggle`, `down`, `again`, `up`, `and`, `or`, `not`, `press`, `release`, `tap`, `wait`, `gap`, `set`, `append`, `pop`, `clear`, `length`, `exec`, `if`, `then`, `else`, `end`, `do`, `while`, `repeat`, `E0`, and `E1`.

User variable and array names cannot use a reserved word, intrinsic name, scan-prefix name, or unprefixed named control name. Binding therefore distinguishes a declared storage identifier from a control reference without introducing a separate source spelling.

`on`, `off`, `held`, and `idle` are constants. Source highlighting renders all four with the constant category.

## Literals

Number literals are decimal integers or decimal fractions such as `0`, `12`, and `3.5`. Scientific notation and decimal points without digits on both sides are invalid. Unary `-` forms negative values; number declarations also accept the sign directly before the initializer.

Duration literals consist of a nonnegative number literal followed immediately by `ms`, `s`, or `min`, such as `30ms`, `1.5s`, and `2min`. The result must be exactly representable as nonnegative signed 64-bit nanoseconds.

Hexadecimal integers are accepted only as raw-control constructor arguments. They use a lowercase `0x` prefix followed by at least one hexadecimal digit.

String literals use double quotes. The escape set is `\\`, `\"`, `\n`, `\r`, and `\t`. Physical line breaks and non-ASCII source characters are invalid inside strings.

Array literals use brackets and contain comma-separated literals of their declared element type. An empty array literal is `[]`.

## Formal Grammar

The following grammar uses ISO-style EBNF: commas mean concatenation, `|` separates alternatives, brackets enclose an optional sequence, and braces enclose a sequence repeated zero or more times.

```ebnf
compilation-unit = { top-level-item } ;

top-level-item =
      target-setting
    | tap-duration-setting
    | action-gap-setting
    | state-declaration
    | number-declaration
    | duration-declaration
    | state-array-declaration
    | number-array-declaration
    | mapping
    | exit-rule
    | pause-rule
    | event-rule
    ;

target-setting       = "TARGET", "=", target-selector, ";" ;
target-selector      = string-literal | "GLOBAL" ;
tap-duration-setting = "TAP_DURATION", "=", duration-literal, ";" ;
action-gap-setting   = "ACTION_GAP", "=", duration-literal, ";" ;

state-declaration  = "state", identifier, "=", state-literal, ";" ;
number-declaration = "number", identifier, "=", signed-number-literal, ";" ;
duration-declaration = "duration", identifier, "=", duration-literal, ";" ;

state-array-declaration  = "state", "[", "]", identifier, "=", state-array-literal, ";" ;
number-array-declaration = "number", "[", "]", identifier, "=", number-array-literal, ";" ;
state-array-literal      = "[", [ state-literal, { ",", state-literal } ], "]" ;
number-array-literal     = "[", [ signed-number-literal, { ",", signed-number-literal } ], "]" ;

mapping = control-reference, "->", control-reference, [ condition ], ";" ;

exit-rule = "exit", event, [ condition ], ";" ;

pause-rule   = "pause", event, [ condition ], pause-arrow, pause-effect, ";" ;
pause-arrow  = "=>" | "~>" ;
pause-effect = "on" | "off" | "toggle" ;

event-rule       = event, [ condition ], rule-arrow, action-flow, ";" ;
event            = control-reference, ":", event-transition ;
event-transition = "down" | "again" | "up" ;
condition        = "when", expression ;
rule-arrow       = "=>" | "=>>" | "~>" | "~>>" ;

action-flow = { action-item } ;

action-item =
      input-action
    | wait-action
    | gap-action
    | set-action
    | toggle-action
    | append-action
    | pop-action
    | clear-action
    | exec-action
    | if-action
    | repeat-action
    | while-action
    ;

input-action      = input-action-name, "(", control-reference, ")" ;
input-action-name = "press" | "release" | "tap" ;
wait-action       = "wait", "(", expression, ")" ;
gap-action =
      "gap", "(", ")"
    | "|"
    ;
set-action        = "set", "(", writable-target, ",", expression, ")" ;
toggle-action     = "toggle", "(", writable-state-target, ")" ;
append-action     = "append", "(", array-reference, ",", expression, ")" ;
pop-action        = "pop", "(", array-reference, ",", writable-scalar-reference, ")" ;
clear-action      = "clear", "(", array-reference, ")" ;
exec-action       = "exec", "(", string-literal, ")" ;

if-action =
    "if", expression, "then", action-flow,
    [ "else", action-flow ],
    "end" ;

repeat-action = "repeat", expression, "do", action-flow, "end" ;
while-action  = "while", expression, "do", action-flow, "end" ;

expression                = or-expression ;
or-expression             = and-expression, { "or", and-expression } ;
and-expression            = equality-expression, { "and", equality-expression } ;
equality-expression       = relational-expression, { equality-operator, relational-expression } ;
relational-expression     = additive-expression, { relational-operator, additive-expression } ;
additive-expression       = multiplicative-expression, { additive-operator, multiplicative-expression } ;
multiplicative-expression = unary-expression, { multiplicative-operator, unary-expression } ;
unary-expression          = primary-expression | unary-operator, unary-expression ;

equality-operator       = "==" | "!=" ;
relational-operator     = "<" | "<=" | ">" | ">=" ;
additive-operator       = "+" | "-" ;
multiplicative-operator = "*" | "/" | "%" ;
unary-operator          = "+" | "-" | "not" ;

primary-expression =
      state-literal
    | control-state-literal
    | number-literal
    | duration-literal
    | scalar-value-reference
    | control-reference
    | array-element
    | array-length
    | "(", expression, ")"
    ;

writable-target          = writable-scalar-reference | array-element ;
writable-state-target    = writable-state-reference | state-array-element ;
writable-scalar-reference = identifier ;
writable-state-reference = identifier ;
scalar-value-reference   = identifier | builtin-value ;
builtin-value            = "TAP_DURATION" | "ACTION_GAP" | "PAUSE" ;

array-reference    = identifier ;
array-element      = identifier, "[", expression, "]" ;
state-array-element = identifier, "[", expression, "]" ;
array-length       = identifier, ".", "length" ;

state-literal         = "on" | "off" ;
control-state-literal = "held" | "idle" ;

control-reference = raw-control | named-control ;
named-control      = control-segment, { ".", control-segment } ;
control-segment    = ASCII-letter, { ASCII-letter | digit | "_" } ;

raw-control =
      hid-usage
    | windows-virtual-key
    | windows-scan-code
    | linux-key
    | macos-key-code
    ;

hid-usage           = "HID.Usage", "(", unsigned-code, ",", unsigned-code, ")" ;
windows-virtual-key = "Windows.VirtualKey", "(", unsigned-code, ")" ;
windows-scan-code   = "Windows.ScanCode", "(", unsigned-code, [ ",", scan-prefix ], ")" ;
linux-key           = "Linux.Key", "(", unsigned-code, ")" ;
macos-key-code      = "MacOS.KeyCode", "(", unsigned-code, ")" ;
scan-prefix         = "E0" | "E1" ;
unsigned-code       = decimal-integer | hexadecimal-integer ;
hexadecimal-integer = "0x", hex-digit, { hex-digit } ;

signed-number-literal = [ "-" ], number-literal ;
number-literal        = decimal-integer, [ ".", decimal-integer ] ;
duration-literal      = number-literal, duration-unit ;
duration-unit         = "ms" | "s" | "min" ;

identifier      = ASCII-letter, { ASCII-letter | digit | "_" } ;
decimal-integer = digit, { digit } ;
digit           = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
hex-digit       = digit | "A" | "B" | "C" | "D" | "E" | "F" | "a" | "b" | "c" | "d" | "e" | "f" ;
ASCII-letter    = ASCII-upper | ASCII-lower ;
ASCII-upper     = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J" | "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" | "S" | "T" | "U" | "V" | "W" | "X" | "Y" | "Z" ;
ASCII-lower     = "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j" | "k" | "l" | "m" | "n" | "o" | "p" | "q" | "r" | "s" | "t" | "u" | "v" | "w" | "x" | "y" | "z" ;
string-literal  = ? an ASCII string token following the escape rules above ? ;
```

## Values and Storage

User scalar variables have the types `state`, `number`, and `duration`. Scalar declarations use a literal initializer and precede their first use.

| Type | Representation | Initializer example |
| --- | --- | --- |
| `state` | `on` or `off` | `state combat = off;` |
| `number` | finite binary64 | `number count = -2.5;` |
| `duration` | nonnegative signed 64-bit nanoseconds | `duration delay = 80ms;` |

User arrays have the element types `state` and `number`. Each array is a named, program-scoped mutable storage object initialized from a same-typed literal list. Array identity is fixed by its declaration, while its logical length changes through array actions.

Array expressions produce element values or a `number` length. Arrays are storage objects rather than expression values, so scalar operators apply to their elements and length rather than to whole arrays.

`PAUSE` is an intrinsic `state` initialized to `on`. `TAP_DURATION` and `ACTION_GAP` are intrinsic `duration` values. Intrinsic values are readable and are excluded from writable targets.

## Expression Types and Operators

Operator precedence from highest to lowest is parentheses, unary `+`, unary `-`, and `not`, then `*`, `/`, and `%`, then binary `+` and `-`, then `<`, `<=`, `>`, and `>=`, then `==` and `!=`, then `and`, and finally `or`. Binary operators associate from left to right.

| Operation | Operand types | Result |
| --- | --- | --- |
| Unary `+`, `-` | `number` | `number` |
| `not` | Boolean | Boolean |
| `+`, `-`, `*`, `/`, `%` | `number`, `number` | `number` |
| `+`, `-` | `duration`, `duration` | `duration` |
| `*` | `duration`, `number` or `number`, `duration` | `duration` |
| `/` | `duration`, `number` | `duration` |
| `<`, `<=`, `>`, `>=` | `number`, `number` | Boolean |
| `==`, `!=` | two values of the same `state`, `number`, `duration`, or control-state type | Boolean |
| `and`, `or` | Boolean, Boolean | Boolean |

Boolean and control-state are expression-only types. `on` and `off` are `state` constants. `held` and `idle` are control-state constants. A control reference evaluates to its current control-state value, enabling expressions such as `A == held`, `Mouse.Left == idle`, and `A == B`.

Weave defines actions rather than user or predicate functions. Parenthesized action forms are valid only inside an action flow; state-variable and control-state queries are ordinary expressions such as `combat == on` and `A == held`.

`and` and `or` evaluate from left to right with short-circuit behavior. A bounds guard such as `index < values.length and values[index] > 0` avoids evaluating the element access when the index is outside the logical array length.

Number operations produce finite results. Division and modulo require a nonzero divisor. Duration subtraction has a `0ms` lower bound; duration scaling truncates fractional nanoseconds toward zero and clamps negative results to `0ms`; overflow is a fault.

## Array Semantics

An array index expression is evaluated once. Its finite `number` result must be nonnegative, is rounded down with `floor`, and must be less than the array's logical length. A negative or out-of-range index is a runtime expression fault.

`array.length` is a read-only `number` expression and is available in every expression context, including mapping conditions, event-rule conditions, PAUSE conditions, and exit conditions.

V3 introduces no separate integer or index type. Array addressing and length use finite `number` values, and the language imposes no fixed declared maximum array length; physical growth remains subject to runtime storage capacity.

`set(array[index], value)` updates one existing element. The normalized index and right-hand expression are evaluated against one task-state snapshot, and the element update is atomic.

`toggle(array[index])` updates one existing `state` element atomically.

`append(array, value)` evaluates a value of the array element type and appends it atomically as the new last element.

`pop(array, target)` requires a nonempty array and a writable scalar target of the element type. It removes the last element and stores that value into the target as one atomic action.

`clear(array)` sets the logical length to zero atomically.

Arrays are shared by event tasks in the same way as scalar variables. A rule condition reads the event-start snapshot. An action-level `if`, `while`, `set`, `toggle`, `append`, `pop`, or `clear` observes task execution state.

## Repeat Semantics

`repeat limit do ... end` evaluates its finite `number` limit once when the loop is entered. The iteration count is `max(0, floor(limit))`. The loop runs with integer iteration positions from zero through count minus one.

Examples include three iterations for `repeat 3 do ... end`, three iterations for `repeat 3.8 do ... end`, and zero iterations for limits below one.

## Control Names and States

Control names are case-sensitive. Every short keyboard name also accepts its `Keyboard.` prefix.

```text
A ... Z
F1 ... F24
Esc Enter Space Tab Backspace Delete Insert Home End PageUp PageDown
ArrowLeft ArrowRight ArrowUp ArrowDown
LCtrl RCtrl LShift RShift LAlt RAlt
Pause CapsLock NumLock ScrollLock
Digit0 ... Digit9
Numpad0 ... Numpad9
NumpadAdd NumpadSubtract NumpadMultiply NumpadDivide NumpadDecimal
```

Named mouse controls are `Mouse.Left`, `Mouse.Right`, `Mouse.Middle`, `Mouse.X1`, and `Mouse.X2`.

Named consumer controls are `Consumer.PlayPause`, `Consumer.ScanNextTrack`, `Consumer.ScanPreviousTrack`, `Consumer.Stop`, `Consumer.Mute`, `Consumer.VolumeUp`, and `Consumer.VolumeDown`.

Platform-specific named controls are `Windows.Keyboard.IMEOn`, `Linux.Keyboard.Compose`, and `MacOS.Keyboard.Fn`.

Raw controls use the established fixed storage ranges for HID usages, Windows virtual keys and scan codes, Linux keys, and macOS key codes. The compiler validates control identity and numeric range; program activation validates backend observation, query, and injection capabilities.

An event transition is `down`, `again`, or `up`. Runtime physical state is updated before event conditions are evaluated, so the event control is already `held` during its `down` condition and already `idle` during its `up` condition.

Injected input is excluded from user-rule matching and physical-state updates. Input is forwarded without user-rule dispatch while the selected target is ineligible.

## Mappings and Rules

`source -> target;` maps the complete down, again, and up lifecycle. Its optional condition is evaluated on the source `down` edge. The first matching mapping is latched until release.

Event-rule arrows retain their delivery and scan behavior:

| Arrow | Physical input | Scan after match |
| --- | --- | --- |
| `=>` | consume | stop |
| `=>>` | consume | continue |
| `~>` | forward | stop |
| `~>>` | forward | continue |

All conditions for one physical event read the same event-start scalar and array state. Actions begin after matching completes, so updates from one newly created task do not alter later conditions for that same event.

PAUSE rules run before mappings and ordinary event rules. Exit rules run before target qualification, PAUSE rules, mappings, and ordinary event rules.

## Actions and Task Control Flow

Action items execute in source order, and adjacent actions have no implicit delay. Use `|` between actions when `ACTION_GAP` should elapse before the following action.

| Syntax | Effect |
| --- | --- |
| `press(control)` | Acquires output ownership and sends a press when ownership changes from zero to one. |
| `release(control)` | Releases output ownership held by the current task; releasing an unowned control faults the task. |
| `tap(control)` | Acquires output ownership, waits for `TAP_DURATION`, and releases ownership. |
| `wait(expression)` | Evaluates a `duration` and performs a cancellable wait. |
| `gap()` | Performs a cancellable wait for `ACTION_GAP`. |
| `set(target, expression)` | Stores a same-typed value in a writable scalar or existing array element. |
| `toggle(target)` | Toggles a writable scalar `state` or existing `state` array element. |
| `append(array, expression)` | Appends a same-typed element. |
| `pop(array, target)` | Removes the last element and stores it in a same-typed writable scalar. |
| `clear(array)` | Sets the array length to zero. |
| `exec("command")` | Requests a platform process launch with a compiled nonempty command string. |

`if condition then ... else ... end` evaluates a Boolean expression when the task reaches it. `repeat` uses the normalized iteration count defined above. `while condition do ... end` reevaluates a Boolean condition before each iteration.

Loop back edges yield scheduler execution but do not advance time. Timed actions and action separators suspend the task for their defined duration.

Each matched nonempty event rule creates an independent task. Tasks share scalar variables, arrays, and output ownership. Cancellation releases task-owned outputs while retaining program-scoped scalar and array state for the active program generation.

## Compilation and Validation

`InputWeaverCompiler.exe validate source.weave` performs source loading, shared lexical analysis, parsing, name binding, type checking, lowering checks, and compiled-program validation without publishing a `.weavec` artifact.

`InputWeaverCompiler.exe compile source.weave output.weavec` performs the same checks and publishes the compiled artifact only after successful validation.

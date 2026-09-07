<a id="section-input-mappings-and-rules"></a>

# Input mappings and rules

[简体中文](../zh/rules.md)

[Documentation home](README.md) · [Previous: Language basics](language.md) · [Next: Actions and control flow](actions.md)

<a id="section-replace-one-key-with-another"></a>

## Replace one key with another

```weave
TARGET = "notepad.exe";

A -> B;
```

`A -> B;` is a complete mapping. Pressing A holds B, repeated presses while A remains held repeat B, and releasing A releases B. For a mapping that lasts as long as the source is held, use `->`.

A mapping can have a condition:

```weave
A -> C when LCtrl == held;
A -> B;
```

Pressing A while left Ctrl is held selects C; otherwise it selects B. The choice is fixed at the initial press. Releasing left Ctrl later still leaves this press mapped to C. Mappings are checked from top to bottom, using the first matching one.

<a id="section-act-when-input-occurs"></a>

## Act when input occurs

```weave
F6:down => tap(B);
```

This rule taps B on the initial press of F6. Keyboard event suffixes select the trigger moment:

| Suffix | When it occurs | Common use |
| --- | --- | --- |
| `down` | The source changes from released to held | Start a macro or toggle a variable |
| `again` | Another press report arrives while the source is held | Respond to keyboard auto-repeat |
| `up` | The source changes from held to released | Act on release |

The current input's physical state is already updated when conditions are checked: its key is `held` on `down` and `idle` on `up`. Program-generated input can appear in Debug, while rule matching uses physical input.

<a id="section-what-the-arrows-control"></a>

## What the arrows control

Consuming an input prevents that physical input from reaching the target application. Forwarding lets the application receive it. Continuing the scan means checking subsequent rules for the same input.

| Arrow | Physical input | Continue checking after a match |
| --- | --- | --- |
| `=>` | Consume | Stop |
| `=>>` | Consume | Continue |
| `~>` | Forward | Stop |
| `~>>` | Forward | Continue |

For example, `A:down ~> tap(B);` preserves the original A input and also taps B; `A:down => tap(B);` consumes this A press and performs only the B action.

An unmet condition continues to the next rule. Once any matching rule chooses to consume the input, that input is consumed; later forwarding rules can still perform their actions.

```weave
number count = 0;

F6:down ~>> set(count, count + 1);
F6:down => tap(B);
```

One press of F6 triggers a counting task and a key task. Because the second rule consumes the input, the target application does not receive this F6 press.

Empty actions are also useful. For example, `F6:down =>;` consumes only the initial press. To block the whole press-and-release cycle, write rules for `down`, `again`, and `up`:

```weave
F6:down =>;
F6:again =>;
F6:up =>;
```

<a id="section-key-combination-conditions"></a>

## Key-combination conditions

A key combination consists of one triggering event and the physical states of other keys. For example, hold either Ctrl key and press F6:

```weave
F6:down when LCtrl == held or RCtrl == held => tap(B);
```

`when` only reads the modifier state. Physical Ctrl remains held in this example, so the application may interpret the B output as Ctrl+B. Consider both the trigger condition and the modifier state the application receives.

<a id="section-matching-order-for-one-input"></a>

## Matching order for one input

A physical input is handled in this order: exit rules first; then target and exclusion conditions, pause rules, and the ordinary-rule switch; then mappings and ordinary event rules.

On the source's initial press, mappings and ordinary rules are checked in source order. An established mapping handles its own `again` and `up` before later ordinary rules add actions.

All rule conditions for one input read the same variable and array state. Actions execute after matching, so a `set` in an earlier rule does not change later conditions for that input:

```weave
number count = 0;

F6:down ~>> set(count, count + 1);
F6:down when count >= 3 => tap(B);
```

Starting from `count = 0`, the fourth F6 press satisfies the second rule: the previous three presses have already raised `count` to 3 when that input starts. To decide immediately using a newly changed value, place the update and `if` in the same rule's actions. [Actions and control flow](actions.md)

<a id="section-pause-and-resume"></a>

## Pause and resume

```weave
pause Pause:down => toggle;

A -> B;
```

Pause switches between enabled and paused operation. The builtin `PAUSE` starts at `on`, meaning ordinary mappings and rules are enabled; `off` means paused.

A pause rule starts with `pause`, followed by a key event and optional `when` condition. Use `=>` or `~>` to consume or forward the input, then `on`, `off`, or `toggle`. The first matching pause rule takes effect immediately, and this input triggers no later rules or mappings.

Changing `PAUSE` cancels executing and waiting actions, clears active mappings, and releases program-held outputs. New input triggers fresh work after resuming; user variables and arrays retain their values. [Runtime behavior and limits](running.md)

<a id="section-exit-the-executor"></a>

## Exit the executor

The default exit combination is `Ctrl+Shift+F12`; either side of Ctrl and Shift is accepted. Define your own exit behavior with a top-level `exit` rule:

```weave
exit F12:down when LCtrl == held and LShift == held;
```

Explicit exit rules replace the default combination with the program's own list of exit shortcuts. You can define several; the first matching rule consumes the input and stops the executor. Exit rules take priority over target eligibility and ordinary rules, and also work while paused.

<a id="section-key-name-reference"></a>

## Key-name reference

Names are case-sensitive. Short keyboard names and names prefixed with `Keyboard.` are equivalent, such as `A` and `Keyboard.A`.

| Category | Names |
| --- | --- |
| Letters | `A` through `Z` |
| Function keys | `F1` through `F24` |
| Main keyboard digits | `Digit0` through `Digit9` |
| Numeric keypad digits | `Numpad0` through `Numpad9` |
| Editing and navigation | `Esc`, `Enter`, `Space`, `Tab`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown` |
| Arrows | `ArrowLeft`, `ArrowRight`, `ArrowUp`, `ArrowDown` |
| Modifiers | `LCtrl`, `RCtrl`, `LShift`, `RShift`, `LAlt`, `RAlt` |
| State keys | `Pause`, `CapsLock`, `NumLock`, `ScrollLock` |
| Keypad operators | `NumpadAdd`, `NumpadSubtract`, `NumpadMultiply`, `NumpadDivide`, `NumpadDecimal` |
| Mouse buttons | `Mouse.Left`, `Mouse.Right`, `Mouse.Middle`, `Mouse.X1`, `Mouse.X2` |
| Media keys | `Consumer.PlayPause`, `Consumer.ScanNextTrack`, `Consumer.ScanPreviousTrack`, `Consumer.Stop`, `Consumer.Mute`, `Consumer.VolumeUp`, `Consumer.VolumeDown` |

Mouse buttons support complete mappings, `down` and `up` events, and `held` and `idle` conditions. Examples include `Mouse.X1 -> LCtrl;` and `Mouse.Left:down when LShift == held => tap(B);`. Mouse movement and scrolling use [dedicated events](mouse.md).

<a id="section-select-keys-by-raw-encoding"></a>

### Select keys by raw encoding

Raw controls use namespaced encodings. `HID.Usage(page, usage)` takes a USB HID Usage Page and Usage ID. Page ranges from `1` to `0xFFFF`; usage ranges from `0` to `0xFFFF`. The source below has the same control identity as the named control `A`:

```weave
HID.Usage(0x07, 0x04) -> B;
```

Codes may be decimal or hexadecimal with a lowercase `0x` prefix. An in-range code also needs platform support for its use in a rule: receiving input, querying `held` or `idle`, or outputting a key. See [Windows executor](windows.md#section-control-encodings-and-support) for virtual keys, scan codes, and their support. You can first observe actual input in [Debug](debugging.md) before choosing a code.

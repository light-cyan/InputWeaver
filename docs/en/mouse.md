<a id="section-mouse-and-meters"></a>

# Mouse and meters

[简体中文](../zh/mouse.md)

[Documentation home](README.md) · [Previous: Actions and control flow](actions.md)

Mouse buttons use the same rules as keyboard keys. Mouse movement and wheels also provide position, displacement, scroll amounts, and meters for actions triggered by distance or continuous movement time.

<a id="section-mouse-buttons"></a>

## Mouse buttons

```weave
Mouse.X1 -> LCtrl;
Mouse.X2:down => tap(Mouse.Left);
```

The first rule maps side button X1 to left Ctrl; the second clicks the left button when X2 is pressed. Button names are `Mouse.Left`, `Mouse.Right`, `Mouse.Middle`, `Mouse.X1`, and `Mouse.X2`. Each physical button press report triggers `down`, and each release report triggers `up`; a keyboard generates `again` when another press report arrives while the key is already held. [Full rule reference](rules.md)

<a id="section-moving-the-pointer-and-scrolling"></a>

## Moving the pointer and scrolling

```weave
F6:down => move_by(100, 0);
F7:down => move_to(500, 300);
F8:down => scroll(1);
F9:down => scroll_horizontal(-1);
```

| Action | Units and direction |
| --- | --- |
| `move_by(dx, dy)` | Moves relative to the pointer position when output actually occurs; pixels, positive to the right and down |
| `move_to(x, y)` | Moves to absolute pixel coordinates on the Windows virtual desktop |
| `scroll(amount)` | Vertical scrolling; one wheel notch is 1, positive upward |
| `scroll_horizontal(amount)` | Horizontal scrolling; one wheel notch is 1, positive to the right |

Arguments can be number expressions. The following coordinate and output eligibility behavior applies to the Windows executor. Displays to the left of or above the primary display can have negative coordinates. A destination outside the desktop, in a gap between displays, or outside the cursor confinement area is adjusted to a reachable display pixel; absolute coordinates are rounded to the nearest pixel. [Windows input and output](windows.md)

Fractional relative movement accumulates: two consecutive `move_by(0.5, 0)` actions can add up to one pixel. Fractional scrolling also accumulates, separately for vertical and horizontal scrolling. Cancellation, such as pausing or losing the target, clears these remainders; absolute movement clears the fractional relative movement remainder.

Application-scoped execution also checks whether the output position belongs to the target: movement checks both its start and destination, while scrolling checks the pointer position at output time. The target application's own mouse and scrolling behavior also affects the result.

<a id="section-responding-to-physical-movement-and-scrolling"></a>

## Responding to physical movement and scrolling

```weave
number last_dx = 0;
number last_wheel = 0;

Mouse:move ~> set(last_dx, Mouse.dx);
Mouse:wheel ~> set(last_wheel, Mouse.wheel_y);
```

| Event | Trigger |
| --- | --- |
| `Mouse:move` | A physical mouse movement report arrives |
| `Mouse:wheel` | A physical vertical wheel report arrives |
| `Mouse:horizontalwheel` | A physical horizontal wheel report arrives |

These events support all four arrows: `=>`, `=>>`, `~>`, and `~>>`. Suppressing arrows let the program handle the original mouse report; passing arrows also deliver the original report to the application.

<a id="section-reading-current-mouse-state"></a>

## Reading current mouse state

| Field | Type | Meaning |
| --- | --- | --- |
| `Mouse.x`, `Mouse.y` | `number` | Currently observed screen coordinates in pixels |
| `Mouse.dx`, `Mouse.dy` | `number` | Physical displacement in the latest numeric mouse report |
| `Mouse.wheel_x`, `Mouse.wheel_y` | `number` | Horizontal and vertical scroll amounts in the latest numeric mouse report, in notches |
| `Mouse.moving` | `state` | Whether physical movement has continued recently |
| `Mouse.idle_time` | `duration` | Time since the last physical movement |

Every movement or wheel report updates displacement and wheel fields together, setting components unrelated to that report to zero. Keyboard and mouse button events leave these fields unchanged.

`MOUSE_IDLE_TIMEOUT` defaults to `80ms`. After that much time without physical movement, `Mouse.moving` becomes `off`, while `Mouse.idle_time` keeps increasing. Set another positive duration at the top level, for example `MOUSE_IDLE_TIMEOUT = 120ms;`.

<a id="section-acting-after-each-distance-interval"></a>

## Acting after each distance interval

```weave
meter path = Mouse:move every 24;
number steps = 0;

path:tick ~> set(steps, steps + 1);
```

`meter` declares a meter. Here, `path` accumulates the physical mouse path and produces a `path:tick` every 24 pixels, increasing `steps` by one.

Distance is the sum of the lengths of each movement segment. Moving 12 pixels right and then 12 pixels left also totals 24 pixels; ordinary pauses preserve an unfinished interval's remainder. One large movement can cross several intervals and produce several ticks in order.

A meter tick uses `~>` or `~>>`. It reports a completed interval; the arrow on the `Mouse:move` rule determines whether to suppress the original movement.

An eligible physical mouse report first updates all meters, then matches the original mouse event rules, and then matches tick rules in meter declaration order and interval completion order. Actions from matching rules start after all these matches finish; waiting or looping can still interleave tasks.

<a id="section-measuring-continuous-movement-time"></a>

## Measuring continuous movement time

```weave
MOUSE_IDLE_TIMEOUT = 80ms;
meter pulse = Mouse:move every 100ms;
number pulses = 0;

pulse:tick ~> set(pulses, pulses + 1);
```

A time meter starts timing at the first eligible movement. Subsequent movements belong to the same continuous movement period while their gaps remain below the idle timeout; subsequent reports account for any intervals crossed in between. A stationary mouse does not produce ticks on its own.

Once a pause reaches `MOUSE_IDLE_TIMEOUT`, the unfinished time interval is cleared, and the next movement starts a new period. This measures continuous movement; for ordinary timed repetition, use `repeat` or `while` with `wait`.

<a id="section-measuring-scroll-amounts"></a>

## Measuring scroll amounts

```weave
meter vertical = Mouse:wheel every 1;
meter horizontal = Mouse:horizontalwheel every 0.25;
number wheel_steps = 0;

vertical:tick ~> set(wheel_steps, wheel_steps + 1);
horizontal:tick ~> tap(ArrowRight);
```

A wheel meter's interval is a positive number of wheel notches. Progress preserves direction: half a notch up followed by half a notch down cancels out. A tick occurs when a full interval is reached in either direction; read the completed interval's scroll amount to determine the direction.

<a id="section-current-and-completed-intervals"></a>

## Current and completed intervals

`path.field` reads the current interval being accumulated; `@path.field` reads the completed interval selected by this rule. For example:

```weave
meter path = Mouse:move every 24;

path:tick ~> wait(100ms) move_to(@path.start_x, @path.start_y);
```

After waiting 100 milliseconds, this action moves the pointer back to the start of the path segment that triggered the tick. Even if the mouse keeps moving during the wait, `@path` still refers to the completion record originally selected for this task.

In `path:tick`, `@path` is the interval that triggered it; in ordinary keyboard or mouse rules, `@path` is the latest completed interval when the event matches. References to other meters, `@name`, are also selected at match time. Current fields, `path.field` and `Mouse.field`, instead read current state when the action evaluates the expression.

Before any interval has completed, `@path.valid` is `off`. Guard access to other completed fields:

```weave
meter path = Mouse:move every 24;

F6:down when @path.valid == on => move_to(@path.start_x, @path.start_y);
```

<a id="section-field-reference"></a>

### Field reference

| Meter view | Readable fields |
| --- | --- |
| Current movement interval | `start_x`, `start_y`, `x`, `y`, `dx`, `dy`, `distance`, `moving` |
| Completed movement interval | `start_x`, `start_y`, `x`, `y`, `dx`, `dy`, `distance`, `valid` |
| Current wheel interval | `x`, `y`, `wheel_x`, `wheel_y` |
| Completed wheel interval | `x`, `y`, `wheel_x`, `wheel_y`, `valid` |

Every current interval also has `period`, `progress`, and `remaining`: its threshold, accumulated progress, and remaining amount. Completed intervals have `period`. These quantities are `number` for distance and wheel meters, and `duration` for time meters. `moving` and `valid` are `state`; coordinates, displacement, and distance are `number`.

For movement intervals, `distance` is path length, `dx` and `dy` are accumulated physical displacement, and the start and end points are the corresponding screen coordinates. When movement is suppressed or output actions move the pointer, the coordinate difference can differ from accumulated physical displacement.

<a id="section-changing-interval-length"></a>

## Changing interval length

```weave
number stride = 24;
meter path = Mouse:move every stride;

F6:down => set(stride, 48);
F7:down => set(stride, 12);
```

An interval expression can use previously declared variables, arrays, and arithmetic. It is evaluated once when each interval starts, and that interval keeps the resulting value. Completing an interval immediately locks the next interval's threshold, even when the remainder is zero. Consequently, changing `stride` in a tick action happens after the next interval has already started; the new value takes effect when a later interval evaluates the expression again.

The interval must be positive. If a dynamic expression fails, the executor reports a meter problem, clears unfinished progress, and retries on the next eligible input; it retains the latest completed record.

<a id="section-resetting-a-meter"></a>

## Resetting a meter

```weave
meter path = Mouse:move every 24;

F6:down => restart(path);
```

`restart(path)` clears this meter's current interval and latest completion record. Tasks already created retain their own selected `@path` records.

Restarting the program, toggling pause, or losing target eligibility also clears meter statistics. Starting and stopping Debug capture changes only the observation view; running statistics continue.

<a id="section-observing-meters-in-debug"></a>

## Observing meters in Debug

METERS shows current progress, interval length, and coordinates, with the latest completed interval on the `@` row below. EVENTS shows ticks and their counts; consecutive ticks from the same meter can be merged for display. ACTION EXECUTIONS keeps each action's triggering relationship. [Debugging and troubleshooting](debugging.md)

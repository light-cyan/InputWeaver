# Implementation Structure and Support Reuse

## Root Support

`src/support/` contains primitives independent of product domains and operating systems.

| File | Facility |
| --- | --- |
| `bit_mix.hpp` | 64-bit mixing function. |
| `bounded_mpmc_queue.hpp` | Fixed-capacity lock-free multiple-producer multiple-consumer queue. |
| `callback_ref.hpp` | Non-owning nullable noexcept callback reference. |
| `fixed_spsc_ring.hpp` | Fixed-capacity single-producer single-consumer ring. |
| `little_endian.hpp` | Little-endian unsigned integer encoding and decoding. |
| `utf8.hpp` | UTF-8 validation. |

## Windows Support

`src/platform/windows/support/` contains reusable Win32 primitives without compiler, runtime, debug, application, or TUI policy.

| File | Facility |
| --- | --- |
| `ordinal_string.hpp` | Windows ordinal case-insensitive string comparison. |
| `unique_handle.hpp` | Movable Win32 `HANDLE` ownership. |
| `atomic_file.hpp/.cpp` | Sibling temporary naming and atomic file writing and replacement. |
| `command_line.hpp/.cpp` | Win32 child-process argument quoting and command-line assembly. |
| `text_encoding.hpp/.cpp` | Strict UTF-8 and UTF-16 conversion. |

## Placement Rules

A structure belongs in `src/support/` only when it is reused across modules and contains no App, TUI, compiler, runtime, debug, input, program, or operating-system semantics. A Win32-only structure without product-domain policy belongs in `src/platform/windows/support/`.

Canvas cells, vertical viewports, stateful horizontally scrolling line editing, distributed column layout with outer spacing, width-aware balanced column-row packing, text layout, and color-scheme parsing are shared by TUI regions but remain UI-domain facilities under `src/ui/tui/support/`.

## App and TUI Boundaries

```text
src/app/
    program catalog, import decisions, executor policy, console history, debug-session orchestration

src/ui/tui/
    page state, keyboard intents, rendering, navigation

src/ui/tui/support/
    canvas, viewport, line editor, text layout, color scheme

src/platform/windows/app/
    program library, compiler and executor child processes, redirected output, WindowsDebugClient lifecycle

src/platform/windows/tui/
    executable entry, console input, virtual-terminal output, resize handling, color-resource loading
```

`src/app/` does not depend on the TUI. `src/ui/tui/` reads application snapshots and submits application commands. Win32 file, process, debug-client, and terminal APIs remain in their corresponding `src/platform/windows/` modules.

## Reuse

- Child processes and pipes use `UniqueHandle`.
- Windows display-name comparison uses `EqualOrdinalIgnoreCase`.
- UTF-8 metadata validation uses `IsValidUtf8`.
- Compiler and App publication reuse atomic Windows file support while retaining their own domain interfaces.
- Fixed JSON color parsing and the validated color model remain under TUI support; Windows resource lookup and reading remain under the Windows TUI adapter.
- `BoundedMpmcQueue` and `FixedSpscRing` do not own the variable-length scrollable Console history; the App owns that bounded string history according to its state model.

The App and TUI reuse the existing compiler, executor, and DebugClient interfaces. The executor provides dry-run behavior and the debug protocol provides wall-clock capture timestamps.

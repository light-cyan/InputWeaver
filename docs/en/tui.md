<a id="section-using-the-interface"></a>

# Using the interface

[简体中文](../zh/tui.md)

[Documentation home](README.md) · [Getting started](getting-started.md)

Run `InputWeaverHost.exe` to open the interface. The Program page manages programs and source, Console shows output, and Debug observes input and actions.

<a id="section-navigating-pages-and-areas"></a>

## Navigating pages and areas

| Current page | Key | Destination |
| --- | --- | --- |
| Program | `[` | Console |
| Program | `]` | Debug |
| Console | `]` or `Esc` | Program |
| Debug | `[` | Program |

Program and Debug contain several areas. Select an area with the arrow keys, press `Enter` to operate its contents, and press `Esc` to return to area selection. `Tab` enters the next area directly, wrapping to the first after the last.

`Esc` returns one level at a time: leave editing, leave full-page document view or the current area, and finally return to the tray from Program area selection. Switching pages preserves each page's selection, scrolling, and document view. Input boxes and dialogs have their own keys; square brackets entered while editing source are text.

<a id="section-creating-and-importing-programs"></a>

## Creating and importing programs

After entering the PROGRAM list on the left of the Program page:

| Key | Operation |
| --- | --- |
| Up and Down | Select a program |
| `A` | Open Add Program |
| `D` | Confirm deletion of the selected program |
| `M` | Reorder the list |

In Add Program, use Left and Right to choose `New Blank`, `Import .weave`, or `Cancel`, then press `Enter`.

`New Blank` first opens a name box; confirming the name creates empty source and enters SOURCE. `Import .weave` accepts an existing source file; type its path, paste with `Ctrl+V`, or drop a file onto the window. Import copies the source into the program library and adds it to the list after successful validation and compilation. Subsequent edits affect the library copy.

If a name conflicts, overwrite, import with another name, or cancel. Overwriting preserves the existing entry's position and run settings. Deleting first stops the selected program, then removes its source, compiled file, and entry; logs are retained.

While reordering, move with Up and Down, save with `Enter`, or discard with `Esc`.

<a id="section-name-target-and-logging"></a>

## Name, target, and logging

PROGRAM INFORMATION at the top right of Program has three fields. Enter the area, select a field with Up and Down, and press `Enter` to edit it.

| Field | Content |
| --- | --- |
| Name | Program name, unique within the library without regard to case |
| Target | Use the source target, a specified application, or global execution |
| Logging | Choose the logging level |

For Target, `Compiled` uses `TARGET` from the source; `Executable` uses the entered executable filename or absolute path; `Global` runs globally. After choosing `Executable`, enter the target in the box on the same row, press `Enter` to save, or `Esc` to return to target type selection. [Target behavior](running.md)

Logging offers `Off`, `Operational`, and `Input Trace`. The latter two record operational logs and logs with input and output traces, respectively. Files are saved under `programs\logs\`; Console shows each run's log path. [Logging details](debugging.md)

<a id="section-browsing-and-editing-source"></a>

## Browsing and editing source

Use `Tab` to enter SOURCE. It shows line numbers, the current line, and syntax colors. COMPILED DUMP is a text view of compiled content for inspecting compilation results.

| Browsing key | Operation |
| --- | --- |
| Up and Down | Move the current line |
| Left and Right | Scroll horizontally |
| `PageUp`, `PageDown` | Scroll by page |
| `Home`, `End` | Go to the first or last line |
| `E` | Edit source |
| `Z` | Expand the document area to fill the interface |
| `V` | Switch between Source and Compiled Dump |

Pressing `E` in Compiled Dump switches to Source and starts editing. Full-page document view preserves the window size; press `Esc` to return to the split view.

<a id="section-editing-keys"></a>

### Editing keys

| Key | Operation |
| --- | --- |
| Arrow keys, `PageUp`, `PageDown` | Move the caret |
| `Home`, `End` | Move to the start or end of the current line |
| `Enter` | Insert a newline |
| `Tab` | Insert four spaces |
| `Backspace`, `Delete` | Delete text or the selection |
| `Shift` with a movement key | Extend the selection |
| `Ctrl+C`, `Ctrl+X`, `Ctrl+V` | Copy, cut, paste |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo |
| `Esc` | Save and leave editing |

In full-page editing, the first `Esc` leaves editing; the second returns to the split view. Source size is limited to 16 MiB.

<a id="section-automatic-saving-and-validation"></a>

### Automatic saving and validation

Source is saved and validated automatically about 400 milliseconds after typing stops. It is also saved before leaving editing, switching programs, generating a dump, running, or deleting.

Error lines use a dark red background; the specific error range is bright red and underlined. Automatic validation keeps the current editing page open; failed saving, explicit compilation, or startup switches to Console to show output.

When Space starts a program, the interface checks whether the compiled file corresponds to the current source and recompiles if needed. Editing source leaves a running executor using the program it started with; stop and run again to use the changes.

<a id="section-running-and-stopping"></a>

## Running and stopping

NEXT RUN contains options for the next run. On the Program page outside editing:

| Key | Interface option | Effect |
| --- | --- | --- |
| `T` | Trace and Debug | Enables debugging and enters Debug after startup |
| `S` | Skip Simulated Input | Dry run: retains rule and action calculations, passes physical input through, and simulates output |
| `P` | Authorize Execution Permission | Allows this run to launch external processes with `exec` |
| Space | Run | Runs the selected program with the current options |
| `X` | Stop | Stops the selected program |

After successful startup, NEXT RUN options return to `OFF`. Pressing Space on an already running program preserves its current run; stop and run again after changing next-run options.

Each program can have one executor running; different programs can run together. In the list, `[RUN]` means ordinary execution, `[DBG]` debugging, `[DRY]` a dry run, and `[EXEC]` authorized external process launching.

The application manages one Debug executor at a time. Starting Debug for another program first stops the previous Debug executor. Debug startup waits about 500 milliseconds to let you release the start key; `STARTING` appears during this delay, and `X` cancels it. [Debugging operations](debugging.md)

<a id="section-reading-console"></a>

## Reading Console

Console combines application, compiler, and executor output and retains the latest 2048 raw output lines. Program and source labels at the beginning of each line help distinguish messages from different programs.

Use Up and Down to scroll by line, `PageUp` and `PageDown` by page, and `Home` and `End` to reach the oldest or newest content. An ordinary program's startup failure, standard error output, or abnormal exit switches here; an abnormal Debug exit stays on Debug for final-state inspection, with related output still available in Console.

<a id="section-window-and-tray"></a>

## Window and tray

The window can be moved and resized. Minimizing preserves its taskbar button; closing it, or pressing `Esc` from Program area selection, returns it to the tray while running executors continue working.

Click or double-click the tray icon to restore the interface. Right-click offers `Show TUI`, `Hide TUI`, and `Exit InputWeaver`. `Exit InputWeaver` exits InputWeaver and stops all Weave programs managed by the interface.

Programs started through the interface automatically exclude `InputWeaverTUI.exe`, so ordinary rules and mappings ignore input used to operate the interface. This also applies to global programs; [exit rules still take priority](running.md#section-excluding-an-application). Debug can still show this raw input; test ordinary rules with the target application in the foreground.

<a id="section-backing-up-and-moving-the-program-library"></a>

## Backing up and moving the program library

The program library is the `programs` folder beside `InputWeaverHost.exe`. Exit InputWeaver and copy the whole folder; restore it beside `InputWeaverHost.exe` in the new installation to preserve source, names, ordering, and run settings.

Its `.weave` files contain source, and its `logs` subfolder contains enabled runtime logs. To import a single source file, follow [Creating and importing programs](#section-creating-and-importing-programs).

<a id="section-changing-colors"></a>

## Changing colors

The color file is `res\InputWeaverTUI.colors.json` beside `InputWeaverHost.exe`, using `#RRGGBB` colors. Preserve its fields and structure, change colors, then exit through the tray and restart InputWeaver. Invalid formatting or fields produce a specific startup error.

Array properties, current mouse state, and meter fields in source all use the `syntax_variable` color, such as `values.length`, `Mouse.x`, `path.progress`, and `@path.dx`; dots use `text`. The mouse button `Mouse.Left` and `Mouse` in the event source `Mouse:move` use `syntax_control`. For property and field naming rules, see [Language basics](language.md#section-properties-and-fields).

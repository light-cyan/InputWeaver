<a id="section-inputweaver-documentation"></a>

# InputWeaver documentation

[简体中文](../zh/README.md)

InputWeaver remaps keyboard and mouse input and uses Weave to describe key macros, conditional actions, and mouse gestures. Create, edit, and run programs in the interface, or compile and run `.weave` files from the command line.

<a id="section-start-here"></a>

## Start here

For your first program, follow [Your first input mapping](getting-started.md): make A produce B in Notepad, then learn how to run and stop it. If you already have a `.weave` file, see the import steps in [Interface operation](tui.md).

<a id="section-learn-weave"></a>

## Learn Weave

Read these pages in order. Each explains the syntax and its effects through runnable examples.

| Document | What it explains |
| --- | --- |
| [Language basics](language.md) | Program structure, variables, arrays, time, and expressions |
| [Input mappings and rules](rules.md) | Key selection, key combinations, and the `->`, `=>`, and `~>` arrows |
| [Actions and control flow](actions.md) | Key sequences, waits, loops, variable updates, and process launch |
| [Mouse and meters](mouse.md) | Mouse position, pointer movement, scrolling, and actions triggered by distance or movement duration |

<a id="section-use-and-reference"></a>

## Use and reference

| Document | What it explains |
| --- | --- |
| [Interface operation](tui.md) | Program management, source editing, next-run options, the tray, and library backup |
| [Command line](command-line.md) | Source validation, compilation, execution, targets, dry runs, and logs |
| [Debugging and troubleshooting](debugging.md) | Input, variable, meter, and action records, and why a program may not take effect |
| [Runtime behavior and limits](running.md) | How target changes, pausing, stopping, concurrent actions, and resource limits affect programs |
| [Windows executor](windows.md) | Windows control encodings and input/output support, executable lookup, and working directories |

<a id="section-files-and-program-names"></a>

## Files and program names

| Name | Purpose |
| --- | --- |
| `.weave` | Editable Weave source file |
| `.weavec` | Compiled file loaded by the executor |
| `InputWeaverHost.exe` | Interface and tray entry point |
| `InputWeaverTUI.exe` | Interface component started automatically by `InputWeaverHost.exe` |
| `InputWeaverCompiler.exe` | Validates and compiles source and produces a text view of the compiled content |
| `InputWeaver.exe` | Executor that runs `.weavec` files |

When you press Space to run a program in the interface, InputWeaver checks whether its source has changed and compiles it first if needed. See [Command line](command-line.md) for the equivalent commands.

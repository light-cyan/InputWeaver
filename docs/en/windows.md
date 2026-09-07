<a id="section-windows-executor"></a>

# Windows executor

[简体中文](../zh/windows.md)

[Documentation home](README.md) · [Input mappings and rules](rules.md) · [Command line](command-line.md)

This page covers Windows control encodings, input/output support, and external-process path handling. See [Language basics](language.md), [Input mappings and rules](rules.md), and [Actions and control flow](actions.md) for Weave types, rules, and action semantics.

<a id="section-control-encodings-and-support"></a>

## Control encodings and support

Windows-specific controls use the `Windows.` namespace. The compiler accepts the raw encoding ranges below. Write codes in decimal or hexadecimal with a lowercase `0x` prefix.

| Syntax | Encoding range |
| --- | --- |
| `Windows.VirtualKey(code)` | `0` to `0xFF` |
| `Windows.ScanCode(code)` | `0` to `0xFF`, ordinary scan code |
| `Windows.ScanCode(code, E0)` | `0` to `0xFF`, E0 extended scan code |
| `Windows.ScanCode(code, E1)` | `0` to `0xFF`, E1 extended scan code |

```weave
Windows.VirtualKey(0x41) -> B;
```

This maps Windows virtual key `0x41` to B. `Windows.Keyboard.IMEOn` is the named spelling of virtual key `0x16`.

A control used as an event source must support receiving that input. A control used in a `held` or `idle` condition must support physical-state queries. A control used by `press`, `release`, `tap`, or as a mapping target must support output. Unsupported uses produce an error when the executor starts. Windows supports the following:

| Control representation | Input observation | Physical-state queries | Output |
| --- | --- | --- | --- |
| [Named keyboard and media keys](rules.md#section-key-name-reference) | Physical reports produce `down`, `again`, and `up` | Supported | Press, release, and repeated press |
| `Mouse.Left`, `Mouse.Right`, `Mouse.Middle`, `Mouse.X1`, `Mouse.X2` | Physical reports produce `down` and `up` | Supported | Corresponding mouse button |
| `HID.Usage(page, usage)` corresponding to these named controls | Same as the named control | Same as the named control | Same as the named control |
| `Windows.VirtualKey(code)`, code `1` to `0xFF` | Corresponding keyboard or mouse reports | Supported | Keyboard virtual-key output or the corresponding mouse button |
| Ordinary or E0 `Windows.ScanCode`, code `1` to `0xFF` | Keyboard reports matching the scan code and prefix | Supported when convertible to a Windows virtual key | Scan-code output |
| Raw E1 scan code, such as `Windows.ScanCode(0x45, E1)` for Pause | Matches scan code and prefix; Pause uses `0x45` | Supported when convertible to a Windows virtual key; Pause has special handling | Input only |

Windows supports the HID spellings of the keyboard keys above, five mouse buttons, and seven media keys. Their Usage Pages are `0x07`, `0x09`, and `0x0C`, respectively. Raw Windows virtual keys and scan codes use nonzero codes at runtime. Raw E1 representations are for input observation; use a named control to output Pause, such as `tap(Pause)`.

Some raw scan codes can receive events but cannot be converted to a virtual key for initial physical-state queries. Such sources first need an observed physical release to establish their input baseline. Using one in a `held` or `idle` condition fails the startup capability check. [Key state at startup](running.md#section-keys-already-held-at-startup)

Use one control representation consistently for the same physical input. `A`, `Keyboard.A`, and `HID.Usage(0x07, 0x04)` are interchangeable aliases; `Windows.VirtualKey(0x41)` is a different representation. Using both that representation and `A` as input sources or state queries causes Windows startup to reject the overlapping inputs. Once chosen, a representation can be reused across multiple rules.

Named keyboard controls generally output scan codes, whereas `Windows.VirtualKey` outputs virtual keys. The resulting character also depends on the keyboard layout, input method, and modifier state. See [Runtime behavior and limits](running.md) for target-window and pointer-position checks, and [Mouse and meters](mouse.md) for coordinates and scroll amounts.

<a id="section-executable-lookup-and-working-directories"></a>

## Executable lookup and working directories

On Windows, `exec` extracts the executable name from the command string and launches the process directly. Quote executable paths that contain spaces. Inside a Weave string, write a quotation mark as `\"` and a backslash as `\\`.

| Executable spelling | Lookup |
| --- | --- |
| Absolute path, such as `C:\Tools\Helper.exe` | Uses the specified file |
| Relative path containing a directory, such as `.\tools\Helper.exe` | Resolves against the executor process's current working directory |
| Filename, such as `Helper.exe` | Searches the executor directory, executor working directory, Windows system directory, the `System` directory under Windows, Windows directory, and directories in `PATH`, in that order |

An ordinary executable name without an extension gains `.exe`. Executors launched by the host use the product directory as their working directory. Executors launched from a command line inherit the working directory of that command-line environment.

The child process starts with its resolved executable directory as its working directory. For example:

```weave
F6:down => exec("\"C:\\Tools\\Helper.exe\" --config settings.json");
```

Helper's working directory is `C:\Tools`. If Helper interprets relative filename arguments against that directory, `settings.json` refers to `C:\Tools\settings.json`. Pass an absolute path when an argument must identify a specific location.

Pipes and redirection are handled by a command interpreter only when you explicitly launch that interpreter. After process creation succeeds, `exec` continues with subsequent actions. See [Launch an external program](actions.md#section-launch-an-external-program) for run permission and dry-run behavior.

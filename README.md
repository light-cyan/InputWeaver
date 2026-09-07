<a id="section-inputweaver"></a>

# InputWeaver

**English** | [简体中文](README.zh-CN.md)

Context-aware input mapping and macro engine.

InputWeaver uses the Weave language to turn keyboard and mouse input into mappings, conditional macros, and actions triggered by mouse movement. Create and debug programs in the built-in editor, or compile and run them from the command line.

[Documentation](docs/en/README.md)

<a id="section-features"></a>

## Features

- Map keyboard keys and mouse buttons for a specific application or globally, preserving press, hold, and release behavior.
- Combine conditions, variables, arrays, waits, and loops to build macros that respond to program and physical input state.
- Measure mouse travel, continuous movement time, and wheel input to trigger actions at configurable intervals.
- Edit source with syntax highlighting and automatic validation; inspect input, variables, meters, and action execution in the debugger.
- Use a dry run to check matching and action calculations while physical input passes through and output is simulated.

<a id="section-a-small-weave-program"></a>

## A small Weave program

```weave
TARGET = "notepad.exe";

A -> B;
F6:down => tap(H) | tap(I);
```

While Notepad is the active target, A acts as B, and pressing F6 taps H followed by I. `TARGET` selects the application, `->` defines a complete key mapping, and `|` inserts a gap between actions. The default `Ctrl+Shift+F12` shortcut stops the executor.

<a id="section-getting-started"></a>

## Getting started

The current executor and interface run on Windows 10 or Windows 11 x64.

For a packaged build, extract the entire archive into a writable folder. Keep the four executables and the `res` folder together, then launch `InputWeaverHost.exe`. Follow the [first-program tutorial](docs/en/getting-started.md) to create and run a program. Closing the window leaves programs running in the tray; press `X` on the Program page outside editing to stop the selected program, or choose `Exit InputWeaver` in the tray menu to stop all programs managed by the interface.

For command-line use, save the example as `demo.weave` and run these PowerShell commands from the directory containing the executables:

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
.\InputWeaver.exe --program .\demo.weavec
```

The compiler creates a `.weavec` file that the executor loads independently. See the [command-line guide](docs/en/command-line.md) for target overrides, dry runs, and logging.

<a id="section-build-from-source"></a>

## Build from source

Build on Windows with a MinGW-w64 toolchain providing `g++` and `windres` on `PATH`. The project uses C++20 and is built with GCC 15.1.0. Windows products use the C++ standard library and Windows API.

From the repository root in PowerShell:

```powershell
.\script\build_products.bat
.\bin\InputWeaverHost.exe
```

Product executables and the interface color resource are written under `bin/`.

| Command | Purpose |
| --- | --- |
| `script\verify_project.bat` | Build products and tests, run tests and documentation checks, analyze source, audit dependencies, and check the working diff |
| `script\verify_docs.bat` | Build the compiler and check bilingual page structure, local links, and code examples |
| `script\package_release.bat` | Build products, validate documentation, and create the portable Windows x64 package |

Verification requires Git and PowerShell; packaging also uses MinGW-w64 `objdump` to check executable dependencies. The package is written to `bin/release/InputWeaver-windows-x64.zip` and includes both documentation languages.

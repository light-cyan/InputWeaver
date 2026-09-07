<a id="section-command-line"></a>

# Command line

[简体中文](../zh/command-line.md)

[Documentation home](README.md)

The command line is useful when writing source in an external editor or compiling and launching programs from scripts. The product commands below use PowerShell syntax and run from the directory containing the product EXE files.

<a id="section-validate-source"></a>

## Validate source

```powershell
.\InputWeaverCompiler.exe validate .\demo.weave
```

Success displays `Source is valid.`. Failure reports the file, line, column, and error. Validation checks the source; compile it to `.weavec` when you want to run it.

<a id="section-compile"></a>

## Compile

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
```

Omit the output path to create a `.weavec` file with the same base name beside the source:

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave
```

The destination is replaced only after successful compilation. Compile again after saving source changes so that the executor can run the new version.

<a id="section-compiled-file-version-mismatch"></a>

### Compiled file version mismatch

If you see `artifact magic or format version differs from the current WEAVEC format`, first check that you selected a compiled `.weavec` file. Then regenerate it from `.weave` source using the compiler supplied with your executor.

Before updating the release files, stop the host and executors. Use product files from the same release, then recompile and run in that directory:

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
.\InputWeaver.exe --program .\demo.weavec
```

For programs managed in the interface, importing the current `.weave` source again regenerates the compiled file. To keep the existing configuration, save the current source to a `.weave` file whose base name matches the existing entry, import it, and choose overwrite when prompted about the name conflict. The current source is in the [program library](tui.md#section-backing-up-and-moving-the-program-library); see [Create and import](tui.md#section-creating-and-importing-programs) for the steps.

If the regenerated file still cannot be read, check that the EXE files, compilation destination, and `--program` path correspond to each other. Obtain matching product files from a complete release package.

<a id="section-run"></a>

## Run

```powershell
.\InputWeaver.exe --program .\demo.weavec
```

The executor uses `TARGET` from the compiled program. If the target application has not started, it displays a waiting message. Once the application starts and owns the foreground window, eligible inputs begin triggering rules.

<a id="section-override-the-target"></a>

### Override the target

```powershell
.\InputWeaver.exe --program .\demo.weavec --target notepad.exe
.\InputWeaver.exe --program .\demo.weavec --target "C:\Tools\My Editor.exe"
.\InputWeaver.exe --program .\demo.weavec --target-global
```

`--target` and `--target-global` select the target for this run and override the source `TARGET`. Choose one of them per run. Quote filenames or paths that contain spaces. [Target selection and runtime state](running.md)

<a id="section-exclude-an-application"></a>

### Exclude an application

```powershell
.\InputWeaver.exe --program .\demo.weavec --target-global --exclude InputWeaverTUI.exe
```

An exclusion accepts an executable filename or absolute path. While the excluded process is in the foreground, ordinary rules and mappings do not respond to input, physical input passes through, and new key and mouse output stops; [exit rules still take priority](running.md#section-excluding-an-application). Launching from the interface sets the TUI exclusion shown above automatically.

<a id="section-dry-run"></a>

## Dry run

```powershell
.\InputWeaver.exe --program .\demo.weavec --dry-run
```

A dry run still matches rules, changes variables, waits, and evaluates exit conditions. Physical input always passes through, and key and mouse output is simulated. Use the interface's `T` and `S` options together to also see graphical debugging information. [Debugging and troubleshooting](debugging.md)

<a id="section-allow-external-processes"></a>

## Allow external processes

```powershell
.\InputWeaver.exe --program .\demo.weavec --allow-exec
```

A compiled program containing `exec` requires permission for this run. For a dry run of such a program, provide both options:

```powershell
.\InputWeaver.exe --program .\demo.weavec --dry-run --allow-exec
```

Process launch is then simulated as successful; no external process is started.

<a id="section-save-logs"></a>

## Save logs

```powershell
.\InputWeaver.exe --program .\demo.weavec --log .\run.jsonl
.\InputWeaver.exe --program .\demo.weavec --log .\run.jsonl --trace-input
```

The first command saves operational logs; the second also records physical input and output traces. Use `--trace-input` together with `--log`. Each launch overwrites the specified log file. Choose different filenames to retain multiple runs. [Log fields and common problems](debugging.md)

<a id="section-stop-and-get-help"></a>

## Stop and get help

The default physical exit combination is `Ctrl+Shift+F12`. A source program with explicit `exit` rules uses those exit rules instead. [Exit rules](rules.md)

```powershell
.\InputWeaver.exe --help
```

Running the compiler without arguments displays usage for its three commands: `validate`, `compile`, and `dump`.

<a id="section-inspect-compiled-content"></a>

## Inspect compiled content

```powershell
.\InputWeaverCompiler.exe dump .\demo.weave
```

`dump` checks and compiles the source, then prints a text view of the compiled content. Use it to inspect rules and control references. Its input is a `.weave` source file.

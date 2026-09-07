<a id="section-your-first-input-mapping"></a>

# Your first input mapping

[简体中文](../zh/getting-started.md)

[Documentation home](README.md)

This tutorial creates a small program that maps A to B in Windows Notepad. You will learn how to edit, run, and stop a Weave program.

<a id="section-1-open-inputweaver"></a>

## 1. Open InputWeaver

On Windows 10 or Windows 11 x64, extract the complete release package to a writable folder. Keep its four EXE files and the `res` folder together, then run `InputWeaverHost.exe`.

The interface opens on the Program page. PROGRAM on the left lists programs, PROGRAM INFORMATION at the upper right shows the name and run configuration, and SOURCE at the lower right contains the source.

Use `Tab` to move between these three regions. You can also select a region with the arrow keys and press `Enter` to enter it; `Esc` goes back one level.

<a id="section-2-create-a-program"></a>

## 2. Create a program

1. Enter the PROGRAM region on the left and press `A` to open Add Program.
2. Select `New Blank` with the left and right arrow keys and press `Enter`. Confirm or change the name, then press `Enter` to create the program.
3. Creation enters the SOURCE region. Press `E` to edit.
4. Type the complete program below, or paste it with `Ctrl+V`.

```weave
TARGET = "notepad.exe";

A -> B;
```

`TARGET` selects the application where the rules apply. `A -> B;` handles pressing, holding, and releasing A as the corresponding states of B. End each complete statement with a semicolon.

Press `Esc` to save and leave editing. Pausing typing for about 400 milliseconds also saves and validates the source. If red markers appear, check names, quotation marks, and semicolons, and open Console for details if needed.

<a id="section-3-run-and-observe"></a>

## 3. Run and observe

Open Notepad. On InputWeaver's Program page, select the program you just created, leave all three NEXT RUN options at `OFF`, and press Space to run it.

Bring Notepad to the foreground. With ordinary English input, pressing A should type `b`; the resulting character still depends on the keyboard layout, input method, Shift, and Caps Lock. In other applications, A works normally.

A running program has a `[RUN]` marker in the list. InputWeaver excludes its own interface from ordinary input handling so that you can continue operating it. [Target and exclusion behavior](running.md)

<a id="section-4-stop-the-program"></a>

## 4. Stop the program

There are three common ways to stop:

- Return to the Program page, select the program, leave editing, and press `X`.
- Press the default physical exit combination, `Ctrl+Shift+F12`, to stop this executor.
- Right-click the InputWeaver tray icon and select `Exit InputWeaver` to exit InputWeaver and stop all Weave programs managed by the interface.

Closing the interface window leaves programs running in the tray. Click the tray icon to reopen the interface.

<a id="section-5-add-a-key-macro"></a>

## 5. Add a key macro

Stop the executor, then replace the source with this program:

```weave
TARGET = "notepad.exe";

A -> B;
F6:down => tap(H) | tap(I);
```

Save and press Space to run again. In Notepad, F6 presses H and then I. Here, `down` means the initial press, `tap` presses and releases a key, `|` inserts a gap, and `=>` consumes the F6 input.

Continue with [Language basics](language.md), or go directly to [Input mappings and rules](rules.md) to learn about conditions, key combinations, and the different arrows.

# Windows Executable Resolution

## Scope

This document defines the Windows runtime adapter contract for `Exec`. The immutable program retains one authored UTF-8 command string. Resolution and process creation occur on the cooperative task thread and do not add fields to `CompiledProgram`.

## Command decoding and token extraction

- The adapter decodes the complete authored command as strict UTF-8 and rejects an embedded NUL, invalid UTF-8, an empty command, or a command exceeding the `CreateProcessW` 32,767-character limit including the terminator.
- Token extraction skips leading spaces and tabs. A token beginning with `"` ends at the next `"`; the closing quote must be followed by a space, tab, or the end of the command. An unquoted token ends at the first space or tab and cannot contain `"`.
- Quotation marks delimit the executable token only. They are removed from the token used for resolution, while the complete decoded authored command remains unchanged for `lpCommandLine`.

## Path resolution

- A token containing `\`, `/`, or `:` is path-qualified. The adapter resolves it with `GetFullPathNameW`, requires an existing non-directory file, and does not add an extension.
- A bare token keeps an existing extension. A bare token without an extension receives `.exe` unless it ends with `.`.
- A bare token is searched in the documented `CreateProcessW` order: the InputWeaver executable directory, the parent process current directory, the 32-bit Windows system directory, the legacy `System` directory, the Windows directory, and each directory in `PATH` order.
- Resolution does not use App Paths, `ShellExecuteW`, `PATHEXT`, a command interpreter, the `.weave` source directory, or the parent directory of the authored command.
- The resolved path must identify a non-directory file. The child working directory is the containing directory of that resolved absolute path.

## `CreateProcessW` contract

- `lpApplicationName` is the resolved absolute executable path.
- `lpCommandLine` is a mutable buffer containing the complete authored command after strict UTF-8 decoding.
- Process and thread security attributes are null, handle inheritance is disabled, creation flags are zero, and the environment pointer is null so the child inherits the InputWeaver environment.
- `lpCurrentDirectory` is the resolved executable's containing directory.
- A successful call closes the returned thread and process handles immediately and completes the action without waiting for initialization or exit.
- A batch file or shell expression runs only when the authored executable token explicitly names a command interpreter such as `cmd.exe`.

## Failure and cancellation

- Token, decoding, resolution, containing-directory, and `CreateProcessW` failures end only the current task and publish the bounded launch diagnostic.
- Cancellation is checked before resolution and again immediately before `CreateProcessW`; either check prevents process creation. After `CreateProcessW` succeeds, the runtime retains no child handle and later cancellation does not terminate or wait for the child.

## Verification

`tests/runtime/windows_runtime_adapter_tests.cpp` covers quoted paths with spaces, absolute paths, bare-name search, explicit command-interpreter invocation, resolution failure, creation failure, cancellation after resolution and before creation, the exact native parameter contract, working-directory observation by a child, immediate return, and normal, E0, E1, and layout-sensitive control recipes. `tests/runtime/program_runtime_tests.cpp` covers cancellation before and after the `Exec` action.

The native API behavior used by this policy follows the Microsoft documentation for [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw), [MapVirtualKeyW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapvirtualkeyw), and [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

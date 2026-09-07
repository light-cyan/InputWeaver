# Phase 1 Verification Record

## Status

Phase 1 is `Complete` as of 2026-08-19. The CLI, executable locator, logging policy, 32-bit self tag, and responsibility-separated runtime build and pass automated and static analysis. Real keyboard and mouse injection is classified as `SelfInjected` without recursive rule activation.

## Environment

- Verification date: 2026-08-19.
- Platform: Windows interactive desktop session.
- Compiler: `g++.exe (x86_64-win32-seh-rev0, Built by MinGW-Builds project) 15.1.0`.
- Resource compiler: `GNU windres (GNU Binutils) 2.44`.
- Dependencies: C++ standard library and Windows system APIs only.

## Current Console Interface

Observer mode:

```text
InputWeaver.exe [--log <jsonl-path>] [--trace-input]
```

Fixed Phase 1 rules:

```text
InputWeaver.exe --test-rules --target <exe-name-or-absolute-path> [--log <jsonl-path>] [--trace-input]
```

The target selector resolves a live process by case-insensitive executable basename or normalized absolute DOS or UNC path. A unique result is selected directly; multiple results require exactly one matching foreground instance, otherwise the console waits. Fixed rules are eligible only while the selected process owns the foreground window, and mouse-bound work additionally requires target pointer-route ownership.

## Canonical Build

Command:

```text
cmd /c script\build.bat
```

The canonical build produces:

- `bin/InputWeaver.exe`.
- `bin/InputWeaverTests.exe`.

The build uses C++20, optimization, Unicode Win32 APIs, `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`, and Windows system libraries only. The remapper embeds an `asInvoker`, `uiAccess=false` execution manifest.

Result on 2026-08-19: all three build stages completed successfully, producing the console executable and automated test executable with warnings treated as errors.

## Canonical Automated Test

Command:

```text
cmd /c script\test.bat
```

The current automated suite covers:

- Keyboard and mouse physical, self-injected, and external-injected classification.
- Self-tag continuity through keyboard, mouse-button, relative-movement, wheel, cleanup, and classifier paths.
- F6-to-F7, F7-to-F8, F9-to-middle-button, and middle-button-to-F10 mappings without recursive actions.
- Repeat handling, paired capture, emergency stop, output conflicts, startup state seeding, physical generation checks, and fail-open queue rejection.
- SPSC capacity, FIFO wraparound, concurrent ordering, transactional publication, producer completion, and shutdown drain behavior.
- Complete, partial, failed, and cleanup-failed injections; owned release retry; failure circuit breaking; and unresolved-state reporting.
- Operational event selection, input-trace expansion, keyboard redaction, mouse movement aggregation, diagnostic ring drops, JSONL drain, and byte-limit truncation.
- Executable basename and absolute-path resolution, invalid relative paths, embedded-NUL rejection, no match, and current-process discovery.
- Target integrity, retained-handle liveness, target exit, foreground and pointer-route rejection, observer lifecycle, hook installation, worker readiness, unhooking, and thread join.

Result on 2026-08-19: `All Phase 1 automated tests passed.`

## Interactive Observations

The first physical-input run observed these fixed-rule results:

- Physical F6 produced F7 without activating the F7-to-F8 rule.
- Physical F7 produced F8.
- Physical F9 produced a middle-button action without activating the middle-button-to-F10 rule.
- A physical middle-button press produced F10.
- Mouse-bound mapping remained inactive when its route did not belong to the selected target.
- All four attempted action batches reported complete `SendInput` counts without primary, cleanup, or circuit-breaker errors.

The current 32-bit tag retest recorded 14 generated middle-button taps as 28 `SelfInjected` hook events with `SelfTag`. No injected mouse event was classified as external or entered rule evaluation. The run recorded 43 complete injection batches with no short send, error, cancellation, cleanup, or circuit-breaker activation.

## Additional Manual Checks

- Resolve an ordinary interactive target once by executable basename and once by absolute executable path.
- Confirm that a missing selector waits and that an ambiguous basename waits unless exactly one matching instance owns the foreground window.
- Keep another process in the foreground and confirm every fixed rule remains inactive.
- Bring the selected target to the foreground and confirm F6 produces F7 without F8, while physical F7 produces F8.
- Route the pointer to the selected target and confirm F9 produces a middle-button click without F10, while a physical middle-button press produces F10.
- Keep the target foreground while routing the pointer to another process and confirm mouse-bound sources remain fail-open.
- Compare a default `--log` file with a `--trace-input` file and confirm unrelated routine input appears only in the trace and mouse movement is aggregated.
- Hold each mapped output before pressing its source and confirm the source remains fail-open without disturbing the held control.
- Stop while a source is captured and confirm no synthetic keyboard or mouse state remains held.
- Confirm a normal-integrity target works with the default remapper and a higher-integrity target is rejected until the remapper runs at an equal integrity level.
- Sustain keyboard repeat and high-rate mouse movement, then record duration, `max_hook_us`, action queue rejections, hook log drops, injection log drops, JSONL bytes, and truncation state.

## Known Boundaries

- `dwExtraInfo` prevents accidental self-recursion but is not an authenticated input identity.
- A kernel filter or virtual HID path can produce device-style events without a user-mode injected flag, so a low-level hook cannot prove that every `PhysicalCandidate` came from a human-operated device.
- `SendInput` and foreground, pointer-route, or physical-generation checks are not one atomic Windows operation; focus, capture, pointer, or physical state can change after the final check.
- The implementation affects the current interactive desktop and does not address a background process, secure desktop, another session, System-integrity interface, kernel-only input path, or application that rejects `SendInput`.
- Low-level hook timeout removal may be silent; the runtime minimizes callback work and reports observed duration but cannot query every silent removal.

## Repository State

The Phase 1 design is committed as `3aa8c3e`, and the verified target-scoped input runtime baseline is committed as `aa33991`. Generated files under `bin/` are ignored by Git.

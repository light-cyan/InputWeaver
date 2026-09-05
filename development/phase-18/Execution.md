# Phase 18: Windows Mouse Input and Output

## Inputs and scope

Use the shared [mouse language design](../phase-16/Grammar.md), the current [language contract](../../docs/grammar.md), and runtime checkpoint `3188e90`. Connect the Windows hooks, runtime ports, output queue, injection, and dry-run execution. Phase 19 owns Debug snapshots, semantic occurrence correlation, and frontend presentation.

## Input and coordinates

- Keep hook coordinates, cursor queries, desktop geometry, and pointer-target checks in physical virtual-desktop pixels, including negative coordinates. Establish DPI awareness for executor threads before input/output starts.
- Normalize movement against the last delivered pointer position, seeded when hooks become ready. Forwarded mouse reports update that baseline; consumed movement keeps the baseline at its starting position. Injected movement updates the baseline without contributing physical deltas or source progress. Dry-run uses the actual forwarded decision for baseline tracking.
- Decode each wheel report's signed high word and divide by `WHEEL_DELTA`, preserving fractional detents. Reuse the existing four-component delta tuple and raw-event dispatch.
- Keep a movement cycle's accumulated displacement separately from its boundary coordinates. Interpolate each included report from its actual segment start and preserve the first cycle origin. This keeps consumed movement and injected pointer relocation from distorting physical displacement and path length. For uninterrupted forwarded physical motion, displacement still equals endpoint minus origin.
- Poll current pointer position through the route port. Physical movement history and the latest delta tuple remain governed by the runtime's physical-input semantics.

## Common output path

- Carry pointer operations and numeric parameters in the existing Windows output queue. Resolve relative destinations on the output thread immediately before injection; share generation, shutdown, target, diagnostics, and circuit-breaker handling with control outputs.
- Put pointer preparation in a focused Windows component. Use one numeric remainder state for relative pixel movement and both wheel axes, shared across tasks in the same generation. Accepted output commits remainder changes; skipped output contributes nothing. Generation changes clear remainders. Absolute movement resets relative movement remainders.
- Convert relative movement to an absolute pixel destination and inject with `MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK`. This preserves pixel semantics independently of Windows pointer acceleration. Convert vertical/horizontal detents to their corresponding wheel flags and native wheel units.
- Normalize finite destinations to reachable monitor pixels within the cursor clipping rectangle. Select the nearest reachable pixel for desktop gaps and out-of-desktop requests, then encode virtual-desktop absolute coordinates. Keep fractional relative requests until they produce a whole pixel; absolute targets round to the nearest pixel.
- Targeted movement checks both its execution-time origin and normalized destination; wheel output checks its execution-time pointer target. Preserve foreground, exclusion, generation, and safe-release checks.
- Dry-run performs the same preparation and routing with a simulated pointer position. Consecutive relative outputs use the previous simulated result. Physical mouse reports rebase that simulated position; output never becomes physical movement or alters the latest physical delta tuple. Skip native injection at the final boundary.

## Execution and verification

1. Connect pointer queries and input normalization; add positive adapter scenarios for movement, suppression, injected baseline updates, signed fractional wheel reports, and source coordinates.
2. Implement the focused pointer preparation component and common output publication. Test multi-task fractional movement, both wheel axes, absolute/relative coordinates, negative monitor origins, desktop edges and gaps, generation reset, and dry-run sequences with a fake native sender.
3. Connect execution-time target checks and native injection. Verify the compiled mouse program through the Windows adapters without sending test input to the user's desktop.
4. Update current runtime documentation, record results, and commit the completed phase after these checks:

```bat
script\build_executor.bat
script\build_executor_tests.bat
script\test_executor.bat
script\build_runtime_tests.bat
script\test_runtime.bat
script\analyze_all.bat
script\audit_dependencies.bat
git diff --check
```

The cross-phase gate remains `script\verify_project.bat` after Phase 19.

## Native contracts

- [MSLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct): per-monitor-aware hook coordinates and signed wheel data.
- [MOUSEINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput): virtual-desktop absolute coordinates, wheel units, and relative acceleration.
- [GetPhysicalCursorPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getphysicalcursorpos) and [GetClipCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipcursor): current physical position and cursor bounds.

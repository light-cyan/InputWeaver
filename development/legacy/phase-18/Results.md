# Phase 18 Results

## Delivered

- Physical-pixel cursor queries and target hit testing, per-monitor DPI awareness, delivered-position input normalization, and signed fractional wheel reports.
- Separate accumulated physical displacement and cycle boundary coordinates, preserving reported movement totals when movement is consumed or the pointer is relocated by output.
- Pointer requests in the common Windows output queue, with execution-time origin/destination routing and the existing generation, shutdown, exclusion, and circuit-breaker checks.
- Focused pointer preparation for relative/absolute movement, monitor gaps, desktop edges, cursor clipping, pixel-center native encoding, and both wheel axes. Fractional output persists across tasks within a generation and commits only with successful output.
- Dry-run pointer simulation that chains relative destinations, accepts physical rebasing, and preserves a newer physical observation when an earlier output completes.
- Current language and runtime boundary documentation, including the existing 8192-item Windows output queue capacity.

## Verification

All commands below completed successfully on 2026-09-05:

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

Static analysis and dependency auditing covered 69 source implementations. Positive scenarios cover consumed/forwarded input baselines, physical movement after injection, fractional wheel decoding, two-axis fractional pixel output, native tagging and coordinate encoding, negative monitor coordinates, gaps, desktop/clipping edges, target-point visitation, sub-native wheel accumulation, generation changes, simulated output sequences, and physical rebasing. A compiled mouse-rule fixture exercises Windows activation, input normalization, task dispatch, the shared output queue, cross-task fractions, pointer preparation, and a fake native sender. Test output does not move the user's desktop pointer.

## Handoff

Phase 19 carries current Mouse/source state, completed occurrence identities, and fixed task selections through Debug, JSONL diagnostics, and the TUI. Final cross-phase verification remains `script\verify_project.bat`.

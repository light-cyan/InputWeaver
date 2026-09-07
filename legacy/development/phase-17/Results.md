# Phase 17 Results

## Delivered

- A shared mouse accumulator for distance, duration, and signed wheel periods, with interpolated boundaries, retained remainders, dynamic period latching, and independent source phases.
- Live Mouse fields, current source fields, and task-owned completed selections through the expression VM. Multiple subscriptions share a source; each tick keeps its exact completion across waits and restart, while cross-source reads select the latest same-input completion.
- Source restart, pause/target/activation lifecycle handling, and physical observation independent of source resets.
- All four pointer actions through the existing ordered output envelope, cancellation generation, routing, and budgets. Pointer output remains independent of held-control ownership.
- Focused accumulator and runtime integration tests, current language/runtime documentation, and the connection-teardown notification required by the existing Debug shutdown tests.

## Verification

All commands below completed successfully on 2026-09-05:

```bat
script\build_runtime_tests.bat
script\test_runtime.bat
script\build_executor.bat
script\build_executor_tests.bat
script\test_executor.bat
script\build_compiler.bat
script\build_compiler_tests.bat
script\test_compiler.bat
script\analyze_all.bat
script\audit_dependencies.bat
git diff --check
```

Static analysis and dependency auditing covered 68 source implementations. The mouse tests cover distance overshoot and origins, wheel cancellation and fractional detents, duration continuity and phase, period latching, multiple sources and subscriptions, fixed own/cross-source selections, first access after wait, live reads, source restart, qualification changes, and pointer request order and parameters.

The initial runtime test run exposed an existing disconnected-client shutdown race. Signaling connection completion after joining its writer resolved it; the complete runtime suite subsequently passed, including the existing Windows Debug server tests.

## Handoff

Phase 18 connects physical Windows observation and pointer output to the runtime ports, including coordinate normalization, output fractions, desktop reachability, routing, and dry-run pointer state. Phase 19 supplies semantic mouse snapshots and occurrence identities through Debug and its frontend. The full cross-phase gate remains `script\verify_project.bat`.

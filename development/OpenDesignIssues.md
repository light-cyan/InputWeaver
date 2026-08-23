# Active Design Issues

## Purpose

This register contains unresolved design decisions that can change current compiler, runtime, platform, or user-visible behavior. The language specification, compiled-program contract, and successor-phase plans remain authoritative for settled behavior. A phase may implement unaffected work while an issue is open, but it may not satisfy an affected completion gate until the issue is resolved and its decision is incorporated into the owning documents and tests.

## Resolution procedure

- Record the selected invariant and ownership boundary in the authoritative specification or plan.
- Update `CompiledProgram` only when the decision requires immutable compiler-to-runtime data.
- Add deterministic tests that distinguish the selected behavior at its safety and semantic boundaries.
- Remove the resolved issue from this active register after the authoritative documents and tests contain the decision.

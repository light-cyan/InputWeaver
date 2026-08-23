# Active Design Issues

## Purpose

This register contains unresolved design decisions that can change current compiler, runtime, platform, or user-visible behavior. The language specifications, the shared program implementation under `src/program/`, and `development/Handoff.md` describe settled current behavior. Work may proceed outside an issue's affected boundary, but an affected verification gate cannot pass until the decision is incorporated into its owning specification, implementation, and tests.

## Resolution procedure

- Record the selected invariant and ownership boundary in the authoritative specification or plan.
- Update `CompiledProgram` only when the decision requires immutable compiler-to-runtime data.
- Add deterministic tests that distinguish the selected behavior at its safety and semantic boundaries.
- Remove the resolved issue from this active register after the authoritative documents and tests contain the decision.

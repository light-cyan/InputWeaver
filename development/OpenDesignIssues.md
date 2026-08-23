# Active Design Issues

## Purpose

This register contains unresolved design decisions that can change current compiler, runtime, platform, or user-visible behavior. The language specification, compiled-program contract, and successor-phase plans remain authoritative for settled behavior. A phase may implement unaffected work while an issue is open, but it may not satisfy an affected completion gate until the issue is resolved and its decision is incorporated into the owning documents and tests.

## Resolution procedure

- Record the selected invariant and ownership boundary in the authoritative specification or plan.
- Update `CompiledProgram` only when the decision requires immutable compiler-to-runtime data.
- Add deterministic tests that distinguish the selected behavior at its safety and semantic boundaries.
- Remove the resolved issue from this active register after the authoritative documents and tests contain the decision.

## ODI-004: Native executable resolution for `exec`

- Status: `Open`.
- Owner: Windows runtime adapter.
- Fixed semantics: `exec` runs on the task thread without an implicit shell or process wait; the authored command and arguments are preserved for the native process API; the child working directory is the resolved executable's containing directory and is independent of the InputWeaver executable, `.weave` source, and parent working directories.
- Decision required: define deterministic extraction and native resolution of the executable token before process creation, including quoted executable paths, absolute and relative paths, native search rules, extensions, error reporting, and the exact `CreateProcessW` parameter contract.
- Required evidence: quoted paths with spaces, absolute paths, search-path resolution, explicit shell invocation, resolution failure, creation failure, working-directory observation by the child, immediate task continuation, and cancellation before and after creation.

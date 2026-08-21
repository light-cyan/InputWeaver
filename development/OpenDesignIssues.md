# Active Design Issues

## Purpose

This register contains unresolved design decisions that can change current compiler, runtime, platform, or user-visible behavior. The language specification, compiled-program contract, and successor-phase plans remain authoritative for settled behavior. A phase may implement unaffected work while an issue is open, but it may not satisfy an affected completion gate until the issue is resolved and its decision is incorporated into the owning documents and tests.

## Resolution procedure

- Record the selected invariant and ownership boundary in the authoritative specification or plan.
- Update `CompiledProgram` only when the decision requires immutable compiler-to-runtime data.
- Add deterministic tests that distinguish the selected behavior at its safety and semantic boundaries.
- Remove the resolved issue from this active register after the authoritative documents and tests contain the decision.

## ODI-001: Pause resume control path

- Status: `Open`.
- Owners: application and runtime; compiler ownership applies if the decision changes Weave syntax or rule classification.
- Fixed semantics: `PAUSE[off]` bypasses ordinary user rules and forwards physical input; a real pause transition advances the cancellation generation, invalidates old tasks and publications, wakes waits, clears mappings, and releases program output ownership; the physical force-stop path remains independent.
- Decision required: define how a physical input can restore `PAUSE` to `on` while ordinary user rules are bypassed. The viable boundary is either an application-level pause control evaluated before the pause guard or a restricted declarative pause-control rule class with explicit compiler and contract representation.
- Required evidence: enter and leave pause from physical input, injected input cannot resume the runtime, no ordinary macro executes while paused, an off-on cycle cannot revive old tasks, and force stop remains available.

## ODI-002: Stable event snapshot mechanism

- Status: `Open`.
- Owner: runtime.
- Fixed semantics: physical state is updated before predicate evaluation; every rule scanned for one physical event reads one stable logical state; tasks selected by that event cannot affect later predicates for the same event; the hook path cannot allocate or retry without a bound.
- Decision required: select the preallocated publication and read mechanism for mutable user values and built-in state, including its bounded contention behavior and controlled failure path.
- Required evidence: deterministic source-order predicate tests, concurrent task-thread writes, bounded hook latency under stress, no torn typed value, and fail-open behavior before event transaction commit.

## ODI-003: Shared control catalog and Windows output recipes

- Status: `Open`.
- Owners: compiler binding and Windows runtime adapter.
- Fixed semantics: source control names bind to platform-neutral `ControlRef` identities; `ControlRequirement` declares event-source, physical-state, output-down/up, and output-repeat uses; activation rejects unsupported uses before hook installation.
- Decision required: define the shared catalog interface, canonical logical or physical identity of every Weave v1 control, keyboard layout behavior, repeat capability, normal and extended scan-code recipes, E0 and E1 exceptions, and mouse button recipes.
- Required evidence: every Weave v1 control binds deterministically, compiler and runtime agree on each identity and capability, unsupported uses fail activation, and real Windows loopback tests cover normal, extended, exceptional, layout-sensitive, and mouse controls.

## ODI-004: Native executable resolution for `exec`

- Status: `Open`.
- Owner: Windows runtime adapter.
- Fixed semantics: `exec` runs on the task thread without an implicit shell or process wait; the authored command and arguments are preserved for the native process API; the child working directory is the resolved executable's containing directory and is independent of the InputWeaver executable, `.weave` source, and parent working directories.
- Decision required: define deterministic extraction and native resolution of the executable token before process creation, including quoted executable paths, absolute and relative paths, native search rules, extensions, error reporting, and the exact `CreateProcessW` parameter contract.
- Required evidence: quoted paths with spaces, absolute paths, search-path resolution, explicit shell invocation, resolution failure, creation failure, working-directory observation by the child, immediate task continuation, and cancellation before and after creation.
